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
  GObject              parent_instance;

  GtkTextView         *view;

  /* Tags */
  GtkTextTag          *italic;
  GtkTextTag          *bold;
  GtkTextTag          *head_1;
  GtkTextTag          *head_2;
  GtkTextTag          *head_3;
  GtkTextTag          *list_indent;
  GtkTextTag          *strikethrough;
  GtkTextTag          *link;
  GtkTextTag          *link_text;
};

G_DEFINE_TYPE (GtdMarkupRenderer, gtd_markup_renderer, G_TYPE_OBJECT)

enum
{
  PROP_0,
  PROP_TEXT_VIEW,
  LAST_PROP
};

static gboolean
on_hyperlink_hover (GtkWidget      *text_view,
                    GdkEventMotion *event)
{
  GSList *tags = NULL, *tagp = NULL;
  GtkTextIter iter;
  gboolean hovering = FALSE;
  gdouble ex, ey;
  gint x, y;
  gchar *tag_name;

  gdk_event_get_coords ((GdkEvent *)event, &ex, &ey);
  gtk_text_view_window_to_buffer_coords (GTK_TEXT_VIEW (text_view),
                                         GTK_TEXT_WINDOW_WIDGET,
                                         ex, ey, &x, &y);
  if (gtk_text_view_get_iter_at_location (GTK_TEXT_VIEW (text_view), &iter, x, y))
    {
      tags = gtk_text_iter_get_tags (&iter);
      for (tagp = tags;  tagp != NULL;  tagp = tagp->next)
        {
          GtkTextTag *tag = tagp->data;
          g_object_get (tag, "name", &tag_name, NULL);

          if (g_strcmp0 (tag_name, "link") == 0)
            {
              hovering = TRUE;
              g_free (tag_name);
              break;
            }
          g_free (tag_name);
        }
      if (hovering)
        {
          g_autoptr (GdkCursor) cursor;
          cursor = gdk_cursor_new_from_name (gtk_widget_get_display (text_view),
                                             "pointer");
          gdk_window_set_cursor (gtk_text_view_get_window (GTK_TEXT_VIEW (text_view), GTK_TEXT_WINDOW_TEXT), cursor);
        }
      else
        {
          g_autoptr (GdkCursor) cursor;
          cursor = gdk_cursor_new_from_name (gtk_widget_get_display (text_view),
                                             "text");
          gdk_window_set_cursor (gtk_text_view_get_window (GTK_TEXT_VIEW (text_view), GTK_TEXT_WINDOW_TEXT), cursor);
        }
    }
  return TRUE;
}

static gboolean
on_hyperlink_clicked (GtkWidget *text_view,
                      GdkEvent  *ev)
{
  GtkTextIter start, end, iter, end_iter, match_start, match_end;
  GSList *tags = NULL, *tagp = NULL;
  GtkTextBuffer *buffer;
  gdouble ex, ey;
  gint x, y;
  gchar *tag_name, *link;
  GError *error;

  if (ev->type == GDK_BUTTON_RELEASE)
    {
      GdkEventButton *event;

      event = (GdkEventButton *)ev;
      if (event->button != GDK_BUTTON_PRIMARY)
        return FALSE;

      ex = event->x;
      ey = event->y;
    }
  else if (ev->type == GDK_TOUCH_END)
    {
      GdkEventTouch *event;

      event = (GdkEventTouch *)ev;

      ex = event->x;
      ey = event->y;
    }
  else
    {
      return FALSE;
    }

  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (text_view));

  /* we shouldn't follow a link if the user has selected something */
  gtk_text_buffer_get_selection_bounds (buffer, &start, &end);
  if (gtk_text_iter_get_offset (&start) != gtk_text_iter_get_offset (&end))
    return FALSE;

  gtk_text_view_window_to_buffer_coords (GTK_TEXT_VIEW (text_view),
                                         GTK_TEXT_WINDOW_WIDGET,
                                         ex, ey, &x, &y);

  if (gtk_text_view_get_iter_at_location (GTK_TEXT_VIEW (text_view), &iter, x, y))
    {
      tags = gtk_text_iter_get_tags (&iter);

      for (tagp = tags; tagp != NULL; tagp = tagp->next)
        {
          GtkTextTag *tag = tagp->data;

          g_object_get (tag, "name", &tag_name, NULL);

          if (g_strcmp0 (tag_name, "link"))
            {
              g_free (tag_name);
              continue;
            }

          gtk_text_buffer_get_iter_at_line (buffer, &iter, gtk_text_iter_get_line (&iter));
          end_iter = iter;
          gtk_text_iter_forward_to_line_end (&end_iter);

          if (gtk_text_iter_forward_search (&iter,
                                            "(",
                                            GTK_TEXT_SEARCH_TEXT_ONLY,
                                            NULL,
                                            &match_start,
                                            NULL))
            {
              if (gtk_text_iter_forward_search (&iter,
                                                ")",
                                                GTK_TEXT_SEARCH_TEXT_ONLY,
                                                &match_end,
                                                NULL,
                                                &end_iter))
                {
                  link = gtk_text_iter_get_text (&match_start, &match_end);

                  if (gtk_show_uri_on_window (GTK_WINDOW (gtk_widget_get_toplevel (text_view)),
                                              link,
                                              GDK_CURRENT_TIME,
                                              &error) == 0)
                    {
                      g_critical ("%s", error->message);
                      g_error_free (error);

                      return FALSE;
                    }
                }
            }
        }
    }
  return TRUE;
}

static void
on_text_changed (GtkTextBuffer *buffer,
                 gpointer       user_data)
{
  GtdMarkupRenderer *renderer = GTD_MARKUP_RENDERER (user_data);

  gtd_markup_renderer_clear_markup (renderer);
  gtd_markup_renderer_render_markup (renderer);
}

static void
render_markup_links (GtdMarkupRenderer *self,
                     GtkTextIter        start,
                     GtkTextIter        end,
                     GtkTextTag        *link_tag,
                     GtkTextTag        *link_text_tag)
{
  GtkTextBuffer *buffer;
  GtkTextIter iter, end_iter;
  GtkTextIter tmp_iter, tmp_end_iter;

  buffer = gtk_text_view_get_buffer (self->view);
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
          gtk_text_buffer_apply_tag (buffer, link_tag,
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
                  gtk_text_buffer_apply_tag (buffer, link_text_tag,
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
  GtkTextBuffer *buffer;
  GtkTextIter iter, end_iter, match, end_match, match1;
  int line_count;
  int i;

  buffer = gtk_text_view_get_buffer(self->view);
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
              gtk_text_buffer_apply_tag (buffer, tag, &match, &iter);
            }
            else
              break;
        }
    }
  else if (pre && !suf)
    {
      line_count = gtk_text_buffer_get_line_count (buffer);
      for (i = 0; i < line_count; i++)
        {
          gtk_text_buffer_get_iter_at_line (buffer, &iter, i);
          end_iter = iter;
          gtk_text_iter_forward_to_line_end (&end_iter);

          if (gtk_text_iter_forward_search (&iter, symbol,
                                            GTK_TEXT_SEARCH_TEXT_ONLY,
                                            &match, NULL,
                                            &end_iter))
            {
              gtk_text_buffer_apply_tag (buffer, tag, &match, &end_iter);
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
    case PROP_TEXT_VIEW:
      g_set_object (&self->view, GTK_TEXT_VIEW (g_value_get_object (value)));
      g_object_ref (self->view);
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
    case PROP_TEXT_VIEW:
      g_value_set_object (value, self->view);
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
  GtkTextBuffer *buffer;

  buffer = gtk_text_view_get_buffer (self->view);

  gtd_markup_renderer_clear_markup (self);

  g_signal_handlers_disconnect_by_func (self->view, on_hyperlink_clicked, NULL);
  g_signal_handlers_disconnect_by_func (self->view, on_hyperlink_hover, NULL);
  g_signal_handlers_disconnect_by_func (buffer, on_text_changed, self);

  g_clear_object (&self->view);

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
          PROP_TEXT_VIEW,
          g_param_spec_object ("textview",
                               "TextView",
                               "The TextView to render markdown on",
                               GTK_TYPE_TEXT_VIEW,
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
  GtkTextBuffer *buffer;

  buffer = gtk_text_view_get_buffer (self->view);
  g_warning ("Populating Tag Table");

  self->italic = gtk_text_buffer_create_tag (buffer,
                                             "italic",
                                             "style",
                                             PANGO_STYLE_ITALIC,
                                             NULL);

  self->bold = gtk_text_buffer_create_tag (buffer,
                                           "bold",
                                           "weight",
                                           PANGO_WEIGHT_BOLD,
                                           NULL);

  self->head_1 = gtk_text_buffer_create_tag (buffer,
                                             "head_1",
                                             "weight",
                                             PANGO_WEIGHT_BOLD,
                                             "scale",
                                             PANGO_SCALE_XX_LARGE,
                                             NULL);

  self->head_2 = gtk_text_buffer_create_tag (buffer,
                                             "head_2",
                                             "weight",
                                             PANGO_WEIGHT_BOLD,
                                             "scale",
                                             PANGO_SCALE_SMALL,
                                             NULL);

  self->head_3 = gtk_text_buffer_create_tag (buffer,
                                             "head_3",
                                             "weight",
                                             PANGO_WEIGHT_BOLD,
                                             "scale",
                                             PANGO_SCALE_X_SMALL,
                                             NULL);

  self->strikethrough = gtk_text_buffer_create_tag (buffer,
                                                    "strikethrough",
                                                    "strikethrough",
                                                    TRUE,
                                                    NULL);

  self->list_indent = gtk_text_buffer_create_tag (buffer,
                                                  "list-indent",
                                                  "indent",
                                                  50,
                                                  NULL);

  self->link = gtk_text_buffer_create_tag (buffer,
                                           "link",
                                           "foreground",
                                           "blue",
                                           "underline",
                                           PANGO_UNDERLINE_SINGLE,
                                           NULL);

  self->link_text = gtk_text_buffer_create_tag (buffer,
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
  GtkTextBuffer *buffer;
  GtkTextIter start, end;

  buffer = gtk_text_view_get_buffer (self->view);
  gtk_text_buffer_get_start_iter (buffer, &start);
  gtk_text_buffer_get_end_iter (buffer, &end);

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
gtd_markup_renderer_setup_callbacks (GtdMarkupRenderer *self)
{
  GtkTextBuffer *buffer;

  buffer = gtk_text_view_get_buffer (self->view);

  g_signal_connect (self->view, "event_after",
                    G_CALLBACK (on_hyperlink_clicked), NULL);
  g_signal_connect (self->view, "motion-notify-event",
                    G_CALLBACK (on_hyperlink_hover), NULL);
  g_signal_connect (buffer, "changed",
                    G_CALLBACK (on_text_changed), self);
}

void
gtd_markup_renderer_clear_markup (GtdMarkupRenderer *self)
{
  GtkTextBuffer *buffer;
  GtkTextIter start, end;

  buffer = gtk_text_view_get_buffer (self->view);

  gtk_text_buffer_get_start_iter (buffer, &start);
  gtk_text_buffer_get_end_iter (buffer, &end);

  gtk_text_buffer_remove_all_tags (buffer, &start, &end);
}
