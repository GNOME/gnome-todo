/* gtd-edit-pane.c
 *
 * Copyright (C) 2015 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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

#include "gtd-edit-pane.h"
#include "gtd-manager.h"
#include "gtd-task.h"
#include "gtd-task-list.h"
#include "gtd-markup-renderer.h"

#include <glib/gi18n.h>

struct _GtdEditPane
{
  GtkGrid            parent;

  GtkCalendar       *calendar;
  GtkLabel          *date_label;
  GtkTextView       *notes_textview;
  GtkComboBoxText   *priority_combo;

  /* task bindings */
  GBinding          *notes_binding;
  GBinding          *priority_binding;

  GtdTask           *task;

  GtdMarkupRenderer *renderer;
};

G_DEFINE_TYPE (GtdEditPane, gtd_edit_pane, GTK_TYPE_GRID)

enum {
  PROP_0,
  PROP_TASK,
  LAST_PROP
};

enum
{
  REMOVE_TASK,
  NUM_SIGNALS
};

static guint signals[NUM_SIGNALS] = { 0, };

static void             date_selected_cb                          (GtkCalendar      *calendar,
                                                                   GtdEditPane      *self);

static void             gtd_edit_pane_update_date                 (GtdEditPane      *pane);

static void
gtd_edit_pane__no_date_button_clicked (GtkButton   *button,
                                       GtdEditPane *self)
{
  gtd_task_set_due_date (self->task, NULL);
  gtk_calendar_clear_marks (GTK_CALENDAR (self->calendar));
  gtd_edit_pane_update_date (self);
}

static void
save_task (GtdEditPane *self)
{
  gtd_manager_update_task (gtd_manager_get_default (), self->task);
}

static void
gtd_edit_pane__delete_button_clicked (GtkButton   *button,
                                      GtdEditPane *self)
{
  g_signal_emit (self, signals[REMOVE_TASK], 0, self->task);
}

static void
today_button_clicked (GtkButton   *button,
                      GtdEditPane *self)
{
  GDateTime *new_dt;

  new_dt = g_date_time_new_now_local ();

  gtd_task_set_due_date (self->task, new_dt);
  gtd_edit_pane_update_date (self);

  save_task (self);

  g_clear_pointer (&new_dt, g_date_time_unref);
}

static void
tomorrow_button_clicked (GtkButton   *button,
                         GtdEditPane *self)
{
  GDateTime *current_date;
  GDateTime *new_dt;

  current_date = g_date_time_new_now_local ();
  new_dt = g_date_time_add_days (current_date, 1);

  gtd_task_set_due_date (self->task, new_dt);
  gtd_edit_pane_update_date (self);

  save_task (self);

  g_clear_pointer (&current_date, g_date_time_unref);
  g_clear_pointer (&new_dt, g_date_time_unref);
}

static gboolean
on_hyperlink_hover (GtkWidget      *text_view,
                    GdkEventMotion *event)
{
  GSList *tags = NULL, *tagp = NULL;
  GtkTextIter iter;
  gboolean hovering = FALSE;
  gdouble ex, ey;
  gint x, y;
  gchar* tag_name;

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
        break;
      }
    }
    if (hovering == TRUE)
    {
      GdkCursor *cursor;
      cursor = gdk_cursor_new_from_name (gtk_widget_get_display (text_view),
                                         "pointer");
      gdk_window_set_cursor (gtk_text_view_get_window (GTK_TEXT_VIEW (text_view), GTK_TEXT_WINDOW_TEXT), cursor);
      g_object_unref (cursor);
    }
    else
    {
      GdkCursor *cursor;
      cursor = gdk_cursor_new_from_name (gtk_widget_get_display (text_view),
                                         "text");
      gdk_window_set_cursor (gtk_text_view_get_window (GTK_TEXT_VIEW (text_view), GTK_TEXT_WINDOW_TEXT), cursor);
      g_object_unref (cursor);
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
    return FALSE;

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

    for (tagp = tags;  tagp != NULL;  tagp = tagp->next)
    {
      GtkTextTag *tag = tagp->data;

      g_object_get (tag, "name", &tag_name, NULL);

      if (g_strcmp0 (tag_name, "link") == 0)
      {
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
  }
  return TRUE;
}

static void
gtd_edit_pane_update_date (GtdEditPane *self)
{
  GDateTime *dt;
  gchar *text;

  g_return_if_fail (GTD_IS_EDIT_PANE (self));

  dt = self->task ? gtd_task_get_due_date (self->task) : NULL;
  text = dt ? g_date_time_format (dt, "%x") : NULL;

  g_signal_handlers_block_by_func (self->calendar, date_selected_cb, self);

  if (dt)
    {
      gtk_calendar_select_month (self->calendar,
                                 g_date_time_get_month (dt) - 1,
                                 g_date_time_get_year (dt));

      gtk_calendar_select_day (self->calendar, g_date_time_get_day_of_month (dt));

    }
  else
    {
      GDateTime *today;

      today = g_date_time_new_now_local ();

      gtk_calendar_select_month (self->calendar,
                                 g_date_time_get_month (today) - 1,
                                 g_date_time_get_year (today));
      gtk_calendar_select_day (self->calendar,
                               g_date_time_get_day_of_month (today));

      g_clear_pointer (&today, g_date_time_unref);
    }

  g_signal_handlers_unblock_by_func (self->calendar, date_selected_cb, self);

  gtk_label_set_label (self->date_label, text ? text : _("No date set"));

  g_free (text);
}

static void
date_selected_cb (GtkCalendar *calendar,
                  GtdEditPane *self)
{
  GDateTime *new_dt;
  gchar *text;
  guint year;
  guint month;
  guint day;

  gtk_calendar_get_date (calendar,
                         &year,
                         &month,
                         &day);

  new_dt = g_date_time_new_local (year,
                                  month + 1,
                                  day,
                                  0,
                                  0,
                                  0);

  text = g_date_time_format (new_dt, "%x");

  gtd_task_set_due_date (self->task, new_dt);
  gtk_label_set_label (self->date_label, text);

  g_date_time_unref (new_dt);
  g_free (text);
}

static gboolean
trap_textview_clicks_cb (GtkWidget   *textview,
                         GdkEvent    *event,
                         GtdEditPane *self)
{
  return GDK_EVENT_STOP;
}

static void
gtd_edit_pane_finalize (GObject *object)
{
  G_OBJECT_CLASS (gtd_edit_pane_parent_class)->finalize (object);
}

static void
gtd_edit_pane_dispose (GObject *object)
{
  GtdEditPane *self = (GtdEditPane *) object;

  g_clear_object (&self->renderer);

  if (self->task)
    gtd_edit_pane_set_task (GTD_EDIT_PANE (object), NULL);

  G_OBJECT_CLASS (gtd_edit_pane_parent_class)->dispose (object);
}

static void
gtd_edit_pane_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  GtdEditPane *self = GTD_EDIT_PANE (object);

  switch (prop_id)
    {
    case PROP_TASK:
      g_value_set_object (value, self->task);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_edit_pane_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  GtdEditPane *self = GTD_EDIT_PANE (object);

  switch (prop_id)
    {
    case PROP_TASK:
      self->task = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_edit_pane_class_init (GtdEditPaneClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gtd_edit_pane_finalize;
  object_class->dispose = gtd_edit_pane_dispose;
  object_class->get_property = gtd_edit_pane_get_property;
  object_class->set_property = gtd_edit_pane_set_property;

  /**
   * GtdEditPane::task:
   *
   * The task that is actually being edited.
   */
  g_object_class_install_property (
        object_class,
        PROP_TASK,
        g_param_spec_object ("task",
                             "Task being edited",
                             "The task that is actually being edited",
                             GTD_TYPE_TASK,
                             G_PARAM_READWRITE));

  /**
   * GtdEditPane::task-removed:
   *
   * Emitted when the the user finishes wants to remove the task.
   */
  signals[REMOVE_TASK] = g_signal_new ("remove-task",
                                       GTD_TYPE_EDIT_PANE,
                                       G_SIGNAL_RUN_LAST,
                                       0,
                                       NULL,
                                       NULL,
                                       NULL,
                                       G_TYPE_NONE,
                                       1,
                                       GTD_TYPE_TASK);

  /* template class */
  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/todo/ui/edit-pane.ui");

  gtk_widget_class_bind_template_child (widget_class, GtdEditPane, calendar);
  gtk_widget_class_bind_template_child (widget_class, GtdEditPane, date_label);
  gtk_widget_class_bind_template_child (widget_class, GtdEditPane, notes_textview);
  gtk_widget_class_bind_template_child (widget_class, GtdEditPane, priority_combo);

  gtk_widget_class_bind_template_callback (widget_class, date_selected_cb);
  gtk_widget_class_bind_template_callback (widget_class, gtd_edit_pane__delete_button_clicked);
  gtk_widget_class_bind_template_callback (widget_class, gtd_edit_pane__no_date_button_clicked);
  gtk_widget_class_bind_template_callback (widget_class, today_button_clicked);
  gtk_widget_class_bind_template_callback (widget_class, tomorrow_button_clicked);
  gtk_widget_class_bind_template_callback (widget_class, trap_textview_clicks_cb);

  gtk_widget_class_set_css_name (widget_class, "editpane");
}

static void
gtd_edit_pane_init (GtdEditPane *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget*
gtd_edit_pane_new (void)
{
  return g_object_new (GTD_TYPE_EDIT_PANE, NULL);
}

/**
 * gtd_edit_pane_get_task:
 * @self: a #GtdEditPane
 *
 * Retrieves the currently edited #GtdTask of %pane, or %NULL if none is set.
 *
 * Returns: (transfer none): the current #GtdTask being edited from %pane.
 */
GtdTask*
gtd_edit_pane_get_task (GtdEditPane *self)
{
  g_return_val_if_fail (GTD_IS_EDIT_PANE (self), NULL);

  return self->task;
}

/**
 * gtd_edit_pane_set_task:
 * @pane: a #GtdEditPane
 * @task: a #GtdTask or %NULL
 *
 * Sets %task as the currently editing task of %pane.
 */
void
gtd_edit_pane_set_task (GtdEditPane *self,
                        GtdTask     *task)
{
  g_return_if_fail (GTD_IS_EDIT_PANE (self));

  if (self->task == task)
    return;

  self->task = task;

  if (task)
    {
      GtkTextBuffer *buffer;

      /* due date */
      gtd_edit_pane_update_date (self);

      /* description */
      buffer = gtk_text_view_get_buffer (self->notes_textview);
      gtk_text_buffer_set_text (buffer,
                                gtd_task_get_description (task),
                                -1);
      self->notes_binding = g_object_bind_property (buffer,
                                                    "text",
                                                    task,
                                                    "description",
                                                    G_BINDING_BIDIRECTIONAL);

      /* priority */
      gtk_combo_box_set_active (GTK_COMBO_BOX (self->priority_combo),
                                CLAMP (gtd_task_get_priority (task), 0, 3));
      self->priority_binding = g_object_bind_property (task,
                                                       "priority",
                                                       self->priority_combo,
                                                       "active",
                                                       G_BINDING_BIDIRECTIONAL);

      self->renderer = gtd_markup_renderer_new ();
      gtd_markup_renderer_set_buffer (self->renderer, buffer);

      gtd_markup_renderer_render_markup (self->renderer);

      g_signal_connect (self->notes_textview, "event_after",
                        G_CALLBACK (on_hyperlink_clicked), NULL);

      g_signal_connect (self->notes_textview, "motion-notify-event",
                        G_CALLBACK (on_hyperlink_hover), NULL);
    }

  g_object_notify (G_OBJECT (self), "task");
}
