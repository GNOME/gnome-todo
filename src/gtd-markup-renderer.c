/* gtd-markup-buffer.c
 *
 * Copyright (C) 2017 Vyas Giridharan <vyasgiridhar27@gmail.com>
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

#include "gtd-markup-renderer.h"

#define ITALICS_1 "*"
#define ITALICS_2 "_"
#define BOLD_1    "__"
#define BOLD_2    "**"
#define STRIKE    "~~"
#define HEAD_1    "#"
#define HEAD_2    "##"
#define HEAD_3    "###"
#define LIST      "+"

struct _GtdMarkupRenderer
{
  GObject             parent_instance;
  GtkTextBuffer      *buffer;
  GtkTextTag         *italic;
  GtkTextTag         *bold;
  GtkTextTag         *head_1;
  GtkTextTag         *head_2;
  GtkTextTag         *head_3;
  GtkTextTag         *list_indent;
  GtkTextTag         *strikethrough;
  GtkTextTag         *link;
  GtkTextTag         *link_text;
};

G_DEFINE_TYPE (GtdMarkupRenderer, gtd_markup_renderer, G_TYPE_OBJECT)

enum
{
  PROP_0,
  PROP_BUF,
  LAST_PROP
};

static void
render_markup_links (GtdMarkupRenderer *self,
                     GtkTextIter        start,
                     GtkTextIter        end,
                     GtkTextTag        *link_tag,
                     GtkTextTag        *link_text_tag)
{
  GtkTextIter iter, end_iter;
  GtkTextIter tmp_iter, tmp_end_iter;

  end_iter = start;

  while (gtk_text_iter_forward_search (&end_iter, "(",
                                       GTK_TEXT_SEARCH_TEXT_ONLY,
                                       &iter, NULL,
                                       &end))
    {
      if (gtk_text_iter_forward_search (&iter, ")",
                                        GTK_TEXT_SEARCH_TEXT_ONLY,
                                        NULL, &end_iter,
                                        &end))
        {
          gtk_text_buffer_apply_tag (self->buffer, link_tag,
                                     &iter, &end_iter);

          if (gtk_text_iter_backward_search (&iter, "]",
                                             GTK_TEXT_SEARCH_TEXT_ONLY,
                                             &tmp_iter, NULL,
                                             &start))
            {
              if (gtk_text_iter_backward_search (&tmp_iter, "[",
                                                 GTK_TEXT_SEARCH_TEXT_ONLY,
                                                 NULL, &tmp_end_iter,
                                                 &start))
                {
                  gtk_text_buffer_apply_tag (self->buffer, link_text_tag,
                                             &tmp_end_iter, &tmp_iter);
                }
            }
        }
    }
}

static void
apply_markup_tag (GtdMarkupRenderer *self,
                  GtkTextTag        *tag,
                  const gchar       *symbol,
                  GtkTextIter        start,
                  GtkTextIter        end,
                  gboolean           pre,
                  gboolean           suf)
{
  GtkTextIter iter, end_iter, match, end_match, match1;
  int line_count;
  int i;

  iter = start;

  if (pre && suf)
    {
      while (gtk_text_iter_forward_search (&iter, symbol,
                                           GTK_TEXT_SEARCH_TEXT_ONLY,
                                           &match, &end_match,
                                           &end))
        {
          if (gtk_text_iter_forward_search (&end_match, symbol,
                                            GTK_TEXT_SEARCH_TEXT_ONLY,
                                            &match1, &iter,
                                            &end))
            {
              gtk_text_buffer_apply_tag (self->buffer, tag, &match, &iter);
            }
            else
              break;
        }
    }
  else if (pre && !suf)
    {
      line_count = gtk_text_buffer_get_line_count (self->buffer);
      for (i = 0; i < line_count; i++)
        {
          gtk_text_buffer_get_iter_at_line (self->buffer, &iter, i);
          end_iter = iter;
          gtk_text_iter_forward_to_line_end (&end_iter);

          if (gtk_text_iter_forward_search (&iter, symbol,
                                            GTK_TEXT_SEARCH_TEXT_ONLY,
                                            &match, NULL,
                                            &end_iter))
            {
              gtk_text_buffer_apply_tag (self->buffer, tag, &match, &end_iter);
            }
        }
    }
}

static void
gtd_markup_renderer_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  GtdMarkupRenderer *self = GTD_MARKUP_RENDERER (object);

  switch (prop_id)
    {
    case PROP_BUF:
      g_set_object (&self->buffer, GTK_TEXT_BUFFER (g_value_get_object (value)));
      g_object_ref (self->buffer);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gtd_markup_renderer_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  GtdMarkupRenderer *self = GTD_MARKUP_RENDERER (object);

  switch (prop_id)
    {
    case PROP_BUF:
      g_value_set_object (value, self->buffer);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gtd_markup_renderer_dispose (GObject *object)
{
  GtdMarkupRenderer *self = GTD_MARKUP_RENDERER (object);

  GtkTextTagTable *table = gtk_text_buffer_get_tag_table (self->buffer);

  gtk_text_tag_table_remove (table, self->italic);
  gtk_text_tag_table_remove (table, self->bold);
  gtk_text_tag_table_remove (table, self->head_1);
  gtk_text_tag_table_remove (table, self->head_2);
  gtk_text_tag_table_remove (table, self->head_3);
  gtk_text_tag_table_remove (table, self->list_indent);
  gtk_text_tag_table_remove (table, self->strikethrough);
  gtk_text_tag_table_remove (table, self->link);
  gtk_text_tag_table_remove (table, self->link_text);

  g_clear_object (&self->buffer);

  G_OBJECT_CLASS (gtd_markup_renderer_parent_class)->dispose(object);
}

static void
gtd_markup_renderer_class_init (GtdMarkupRendererClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = gtd_markup_renderer_set_property;
  object_class->get_property = gtd_markup_renderer_get_property;
  object_class->dispose = gtd_markup_renderer_dispose;

  g_object_class_install_property (
        object_class,
        PROP_BUF,
        g_param_spec_object ("textbuffer",
                             "TextBuffer",
                             "The TextBuffer to render markdown on",
                             GTK_TYPE_TEXT_BUFFER,
                             G_PARAM_WRITABLE | G_PARAM_CONSTRUCT));

}

GtdMarkupRenderer*
gtd_markup_renderer_new (void)
{
  return g_object_new (GTD_TYPE_MARKUP_RENDERER, NULL);
}

void
gtd_markup_renderer_init (GtdMarkupRenderer *self)
{
}

void
gtd_markup_renderer_populate_tag_table (GtdMarkupRenderer *self)
{
  self->italic = gtk_text_buffer_create_tag (self->buffer,
                                             "italic",
                                             "style",
                                             PANGO_STYLE_ITALIC,
                                             NULL);

  self->bold = gtk_text_buffer_create_tag (self->buffer,
                                           "bold",
                                           "weight",
                                           PANGO_WEIGHT_BOLD,
                                           NULL);

  self->head_1 = gtk_text_buffer_create_tag (self->buffer,
                                             "head_1",
                                             "weight",
                                             PANGO_WEIGHT_BOLD,
                                             "scale",
                                             PANGO_SCALE_XX_LARGE,
                                             NULL);

  self->head_2 = gtk_text_buffer_create_tag (self->buffer,
                                             "head_2",
                                             "weight",
                                             PANGO_WEIGHT_BOLD,
                                             "scale",
                                             PANGO_SCALE_SMALL,
                                             NULL);

  self->head_3 = gtk_text_buffer_create_tag (self->buffer,
                                             "head_3",
                                             "weight",
                                             PANGO_WEIGHT_BOLD,
                                             "scale",
                                             PANGO_SCALE_X_SMALL,
                                             NULL);

  self->strikethrough = gtk_text_buffer_create_tag (self->buffer,
                                                    "strikethrough",
                                                    "strikethrough",
                                                    TRUE,
                                                    NULL);

  self->list_indent = gtk_text_buffer_create_tag (self->buffer,
                                                  "list-indent",
                                                  "indent",
                                                  50,
                                                  NULL);

  self->link = gtk_text_buffer_create_tag (self->buffer,
                                           "link",
                                           "foreground",
                                           "blue",
                                           "underline",
                                           PANGO_UNDERLINE_SINGLE,
                                           NULL);

  self->link_text = gtk_text_buffer_create_tag (self->buffer,
                                                "link-text",
                                                "weight",
                                                PANGO_WEIGHT_BOLD,
                                                "foreground",
                                                "#555F61",
                                                NULL);
}

void
gtd_markup_renderer_render_markup (GtdMarkupRenderer *self)
{
  GtkTextIter start, end;

  gtk_text_buffer_get_start_iter (self->buffer, &start);
  gtk_text_buffer_get_end_iter (self->buffer, &end);

  apply_markup_tag (self, self->bold, BOLD_1, start, end, TRUE, TRUE);
  apply_markup_tag (self, self->bold, BOLD_2, start, end, TRUE, TRUE);

  apply_markup_tag (self, self->italic, ITALICS_1, start, end, TRUE, TRUE);
  apply_markup_tag (self, self->italic, ITALICS_2, start, end, TRUE, TRUE);

  apply_markup_tag (self, self->head_1, HEAD_1, start, end, TRUE, FALSE);
  apply_markup_tag (self, self->head_2, HEAD_2, start, end, TRUE, FALSE);
  apply_markup_tag (self, self->head_3, HEAD_3, start, end, TRUE, FALSE);

  apply_markup_tag (self, self->strikethrough, STRIKE, start, end, TRUE, TRUE);

  apply_markup_tag (self, self->list_indent, LIST, start, end, TRUE, FALSE);

  render_markup_links (self, start, end, self->link, self->link_text);
}

void
gtd_markup_renderer_clear_markup (GtdMarkupRenderer *self)
{
  GtkTextTagTable *table;

  table = gtk_text_buffer_get_tag_table (self->buffer);

  gtk_text_tag_table_remove (table, self->italic);
  gtk_text_tag_table_remove (table, self->bold);
  gtk_text_tag_table_remove (table, self->head_1);
  gtk_text_tag_table_remove (table, self->head_2);
  gtk_text_tag_table_remove (table, self->head_3);
  gtk_text_tag_table_remove (table, self->list_indent);
  gtk_text_tag_table_remove (table, self->strikethrough);
  gtk_text_tag_table_remove (table, self->link);
  gtk_text_tag_table_remove (table, self->link_text);
}
