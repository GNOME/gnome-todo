/* gtd-markdown-buffer.c
 *
 * Copyright © 2018 Vyas Giridharan <vyasgiridhar27@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define G_LOG_DOMAIN "GtdMarkdownRenderer"

#include "gtd-debug.h"
#include "gtd-markdown-renderer.h"

#define ITALICS_1 "*"
#define ITALICS_2 "_"
#define BOLD_1    "__"
#define BOLD_2    "**"
#define STRIKE    "~~"
#define HEAD_1    "#"
#define HEAD_2    "##"
#define HEAD_3    "###"
#define LIST      "+"

struct _GtdMarkdownRenderer
{
  GObject              parent_instance;

  GHashTable          *populated_buffers;
};


static void          on_text_buffer_weak_notified_cb             (gpointer            data,
                                                                  GObject            *where_the_object_was);

static void          on_text_changed_cb                          (GtkTextBuffer       *buffer,
                                                                  GParamSpec          *pspec,
                                                                  GtdMarkdownRenderer *self);


G_DEFINE_TYPE (GtdMarkdownRenderer, gtd_markdown_renderer, G_TYPE_OBJECT)


/*
 * Auxiliary methods
 */

static void
apply_link_tags (GtdMarkdownRenderer *self,
                 GtkTextBuffer       *buffer,
                 GtkTextTag          *link_tag,
                 GtkTextTag          *url_tag,
                 GtkTextIter         *text_start,
                 GtkTextIter         *text_end)
{
  GtkTextIter symbol_start;
  GtkTextIter symbol_end;
  GtkTextIter target_start;
  GtkTextIter target_end;
  GtkTextIter iter;

  GTD_ENTRY;

  iter = *text_start;

  /*
   * We advance in pairs of [...], and inside the loop we check if the very next character
   * after ']' is '('. No spaces are allowed. Only if this condition is satisfied that we
   * claim this to be a link, and render it as such.
   */
  while (gtk_text_iter_forward_search (&iter, "[", GTK_TEXT_SEARCH_TEXT_ONLY, &symbol_start, &target_start, text_end) &&
         gtk_text_iter_forward_search (&target_start, "]", GTK_TEXT_SEARCH_TEXT_ONLY, &target_end, &symbol_end, text_end))
    {
      GtkTextIter url_start;
      GtkTextIter url_end;

      iter = symbol_end;

      /* Advance a single position */
      url_start = symbol_end;

      /* Only consider valid if the character after ']' is '(' */
      if (gtk_text_iter_get_char (&url_start) != '(')
        continue;

      /*
       * Try and find the matching (...), if it fails, iter is set to the previous ']' so
       * we don't enter in an infinite loop
       */
      if (!gtk_text_iter_forward_search (&iter, "(", GTK_TEXT_SEARCH_TEXT_ONLY, NULL, &url_start, text_end) ||
          !gtk_text_iter_forward_search (&iter, ")", GTK_TEXT_SEARCH_TEXT_ONLY, &url_end, NULL, text_end))
        {
          continue;
        }

      /* Apply both the link and url tags */
      gtk_text_buffer_apply_tag (buffer, link_tag, &target_start, &target_end);
      gtk_text_buffer_apply_tag (buffer, url_tag, &url_start, &url_end);

      iter = url_end;
    }
}

static void
apply_markdown_tag (GtdMarkdownRenderer *self,
                    GtkTextBuffer       *buffer,
                    GtkTextTag          *tag,
                    const gchar         *symbol,
                    GtkTextIter         *text_start,
                    GtkTextIter         *text_end,
                    gboolean             paired)
{
  GtkTextIter symbol_start;
  GtkTextIter symbol_end;
  GtkTextIter iter;

  iter = *text_start;

  while (gtk_text_iter_forward_search (&iter, symbol, GTK_TEXT_SEARCH_TEXT_ONLY, &symbol_start, &symbol_end, text_end))
    {
      GtkTextIter tag_start;
      GtkTextIter tag_end;

      tag_start = symbol_start;
      tag_end = symbol_end;

      /* Iter is initially at the end of the found symbol, to avoid infinite loops */
      iter = symbol_end;

      if (paired)
        {
          /*
           * If the markdown tag is in the form of pairs (e.g. **bold**, __italics__, etc), then we should
           * search the symbol twice. The first marks the start and the second marks the end of the section
           * of the text that needs the tag.
           *
           * We also ignore the tag if it's not contained in the same line of the start.
           */
          if (!gtk_text_iter_forward_search (&tag_end, symbol, GTK_TEXT_SEARCH_TEXT_ONLY, NULL, &tag_end, text_end) ||
              gtk_text_iter_get_line (&tag_end) != gtk_text_iter_get_line (&symbol_start))
            {
              continue;
            }
        }
      else
        {
          /*
           * If the markdown tag is not paired (e.g. ## header), then it is just applied at the start of
           * the line. As such, we must search for the symbol - and this is where the tag starts - but move
           * straight to the end of the line.
           */
          gtk_text_iter_forward_to_line_end (&tag_end);

          /* Only apply this tag if this is the start of the line */
          if (gtk_text_iter_get_line_offset (&tag_start) != 0)
            continue;
        }

      /* Apply the tag */
      gtk_text_buffer_apply_tag (buffer, tag, &tag_start, &tag_end);

      /*
       * If we applied the tag, jump the iter to the end of the tag. We are already guaranteed
       * to not run into infinite loops, but this skips a bigger section of the buffer too and
       * can save a tiny few cycles
       */
      iter = tag_end;
    }
}

static void
populate_tag_table (GtdMarkdownRenderer *self,
                    GtkTextBuffer       *buffer)
{
  gtk_text_buffer_create_tag (buffer,
                              "italic",
                              "style",
                              PANGO_STYLE_ITALIC,
                              NULL);

  gtk_text_buffer_create_tag (buffer,
                              "bold",
                              "weight",
                              PANGO_WEIGHT_BOLD,
                              NULL);

  gtk_text_buffer_create_tag (buffer,
                              "head_1",
                              "weight",
                              PANGO_WEIGHT_BOLD,
                              "scale",
                              PANGO_SCALE_XX_LARGE,
                              NULL);

  gtk_text_buffer_create_tag (buffer,
                              "head_2",
                              "weight",
                              PANGO_WEIGHT_BOLD,
                              "scale",
                              PANGO_SCALE_SMALL,
                              NULL);

  gtk_text_buffer_create_tag (buffer,
                              "head_3",
                              "weight",
                              PANGO_WEIGHT_BOLD,
                              "scale",
                              PANGO_SCALE_SMALL,
                              NULL);

  gtk_text_buffer_create_tag (buffer,
                              "strikethrough",
                              "strikethrough",
                              TRUE,
                              NULL);

  gtk_text_buffer_create_tag (buffer,
                              "list-indent",
                              "indent",
                              20,
                              NULL);

  gtk_text_buffer_create_tag (buffer,
                              "url",
                              "foreground",
                              "blue",
                              "underline",
                              PANGO_UNDERLINE_SINGLE,
                              NULL);

  gtk_text_buffer_create_tag (buffer,
                              "link-text",
                              "weight",
                              PANGO_WEIGHT_BOLD,
                              "foreground",
                              "#555F61",
                              NULL);

  /*
   * Add a weak ref so we can remove from the map of populated buffers when it's
   * finalized.
   */
  g_object_weak_ref (G_OBJECT (buffer), on_text_buffer_weak_notified_cb, self);

  /* Add to the map of populated buffers */
  g_hash_table_add (self->populated_buffers, buffer);
  g_signal_connect (buffer, "notify::text", G_CALLBACK (on_text_changed_cb), self);

  g_debug ("Added buffer %p to markdown renderer", buffer);
}

static void
render_markdown (GtdMarkdownRenderer *self,
                 GtkTextBuffer       *buffer)
{
  GtkTextTagTable *tag_table;
  GtkTextIter start;
  GtkTextIter end;

  GTD_ENTRY;

  /* TODO: render in idle */

  /* Wipe out the previous tags */
  gtk_text_buffer_get_start_iter (buffer, &start);
  gtk_text_buffer_get_end_iter (buffer, &end);

  gtk_text_buffer_remove_all_tags (buffer, &start, &end);

  /* Apply the tags */
  tag_table = gtk_text_buffer_get_tag_table (buffer);

#define TAG(x) gtk_text_tag_table_lookup(tag_table, x)

  apply_markdown_tag (self, buffer, TAG ("bold"), BOLD_2, &start, &end, TRUE);
  apply_markdown_tag (self, buffer, TAG ("bold"), BOLD_1, &start, &end, TRUE);
  apply_markdown_tag (self, buffer, TAG ("italic"), ITALICS_2, &start, &end, TRUE);
  apply_markdown_tag (self, buffer, TAG ("italic"), ITALICS_1, &start, &end, TRUE);
  apply_markdown_tag (self, buffer, TAG ("head_3"), HEAD_3, &start, &end, FALSE);
  apply_markdown_tag (self, buffer, TAG ("head_2"), HEAD_2, &start, &end, FALSE);
  apply_markdown_tag (self, buffer, TAG ("head_1"), HEAD_1, &start, &end, FALSE);
  apply_markdown_tag (self, buffer, TAG ("strikethrough"), STRIKE, &start, &end, TRUE);
  apply_markdown_tag (self, buffer, TAG ("list_indent"), LIST, &start, &end, FALSE);

  apply_link_tags (self, buffer, TAG ("link-text"), TAG ("url"), &start, &end);

#undef TAG

  GTD_EXIT;
}

/*
 * Callbacks
 */

static void
on_text_buffer_weak_notified_cb (gpointer  data,
                                 GObject  *where_the_object_was)
{
  GtdMarkdownRenderer *self = GTD_MARKDOWN_RENDERER (data);

  g_hash_table_remove (self->populated_buffers, where_the_object_was);

  g_debug ("Buffer %p died and was removed from markdown renderer", where_the_object_was);
}


static void
on_text_changed_cb (GtkTextBuffer       *buffer,
                    GParamSpec          *pspec,
                    GtdMarkdownRenderer *self)
{
  render_markdown (self, buffer);
}

static void
gtd_markdown_renderer_class_init (GtdMarkdownRendererClass *klass)
{
}

void
gtd_markdown_renderer_init (GtdMarkdownRenderer *self)
{
  self->populated_buffers = g_hash_table_new (g_direct_hash, g_direct_equal);
}

GtdMarkdownRenderer*
gtd_markdown_renderer_new (void)
{
  return g_object_new (GTD_TYPE_MARKDOWN_RENDERER, NULL);
}

void
gtd_markdown_renderer_add_buffer (GtdMarkdownRenderer *self,
                                  GtkTextBuffer       *buffer)
{
  g_return_if_fail (GTD_IS_MARKDOWN_RENDERER (self));

  GTD_ENTRY;

  /* If the text buffer is not poopulated yet, do it now */
  if (!g_hash_table_contains (self->populated_buffers, buffer))
    populate_tag_table (self, buffer);

  render_markdown (self, buffer);

  GTD_EXIT;
}
