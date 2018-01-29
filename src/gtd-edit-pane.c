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

#define G_LOG_DOMAIN "GtdEditPane"

#include "gtd-edit-pane.h"
#include "gtd-manager.h"
#include "gtd-markdown-renderer.h"
#include "gtd-task.h"
#include "gtd-task-list.h"

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
};

G_DEFINE_TYPE (GtdEditPane, gtd_edit_pane, GTK_TYPE_GRID)

enum
{
  PROP_0,
  PROP_TASK,
  LAST_PROP
};

enum
{
  CHANGED,
  REMOVE_TASK,
  NUM_SIGNALS
};


static guint signals[NUM_SIGNALS] = { 0, };


static void          on_date_selected_cb                         (GtkCalendar        *calendar,
                                                                  GtdEditPane        *self);

/*
 * Auxiliary methods
 */

static void
update_date_widgets (GtdEditPane *self)
{
  GDateTime *dt;
  gchar *text;

  g_return_if_fail (GTD_IS_EDIT_PANE (self));

  dt = self->task ? gtd_task_get_due_date (self->task) : NULL;
  text = dt ? g_date_time_format (dt, "%x") : NULL;

  g_signal_handlers_block_by_func (self->calendar, on_date_selected_cb, self);

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

  g_signal_handlers_unblock_by_func (self->calendar, on_date_selected_cb, self);

  gtk_label_set_label (self->date_label, text ? text : _("No date set"));

  g_free (text);
}


/*
 * Callbacks
 */

static void
on_date_selected_cb (GtkCalendar *calendar,
                     GtdEditPane *self)
{
  g_autoptr (GDateTime) new_dt = NULL;
  g_autofree gchar *text = NULL;
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
}

static void
on_delete_button_clicked_cb (GtkButton   *button,
                             GtdEditPane *self)
{
  g_signal_emit (self, signals[REMOVE_TASK], 0, self->task);
}

static void
on_no_date_button_clicked_cb (GtkButton   *button,
                              GtdEditPane *self)
{
  gtd_task_set_due_date (self->task, NULL);
  gtk_calendar_clear_marks (GTK_CALENDAR (self->calendar));
  update_date_widgets (self);
}

static void
on_today_button_clicked_cb (GtkButton   *button,
                            GtdEditPane *self)
{
  GDateTime *new_dt;

  new_dt = g_date_time_new_now_local ();

  gtd_task_set_due_date (self->task, new_dt);
  update_date_widgets (self);

  g_signal_emit (self, signals[CHANGED], 0);

  g_clear_pointer (&new_dt, g_date_time_unref);
}

static void
on_priority_changed_cb (GtkComboBox *combobox,
                        GtdEditPane *self)
{
  g_signal_emit (self, signals[CHANGED], 0);
}

static void
on_tomorrow_button_clicked_cb (GtkButton   *button,
                               GtdEditPane *self)
{
  GDateTime *current_date;
  GDateTime *new_dt;

  current_date = g_date_time_new_now_local ();
  new_dt = g_date_time_add_days (current_date, 1);

  gtd_task_set_due_date (self->task, new_dt);
  update_date_widgets (self);

  g_signal_emit (self, signals[CHANGED], 0);

  g_clear_pointer (&current_date, g_date_time_unref);
  g_clear_pointer (&new_dt, g_date_time_unref);
}

static void
on_text_buffer_changed_cb (GtkTextBuffer *buffer,
                           GtdEditPane   *self)
{
  g_signal_emit (self, signals[CHANGED], 0);
}

static gboolean
on_trap_textview_clicks_cb (GtkWidget   *textview,
                            GdkEvent    *event,
                            GtdEditPane *self)
{
  return GDK_EVENT_STOP;
}

static gboolean
on_hyperlink_hover_cb (GtkTextView    *text_view,
                       GdkEventMotion *event)
{
  g_autoptr (GdkCursor) cursor = NULL;
  GdkDisplay *display = NULL;
  GtkTextIter iter;
  GSList *tags = NULL;
  GSList *l = NULL;
  gboolean hovering;
  gdouble ex, ey;
  gint x, y;

  gdk_event_get_coords ((GdkEvent *)event, &ex, &ey);
  gtk_text_view_window_to_buffer_coords (text_view, GTK_TEXT_WINDOW_WIDGET, ex, ey, &x, &y);

  if (!gtk_text_view_get_iter_at_location (text_view, &iter, x, y))
    return GDK_EVENT_PROPAGATE;

  hovering = FALSE;
  tags = gtk_text_iter_get_tags (&iter);

  for (l = tags; l; l = l->next)
    {
      g_autofree gchar *tag_name = NULL;
      GtkTextTag *tag;

      tag = l->data;

      g_object_get (tag, "name", &tag_name, NULL);

      if (g_strcmp0 (tag_name, "url") == 0)
        {
          hovering = TRUE;
          break;
        }
    }

  display = gtk_widget_get_display (GTK_WIDGET (text_view));
  cursor = gdk_cursor_new_from_name (display, hovering ? "pointer" : "text");

  gdk_window_set_cursor (gtk_text_view_get_window (text_view, GTK_TEXT_WINDOW_TEXT), cursor);

  return GDK_EVENT_STOP;
}

static gboolean
on_hyperlink_clicked_cb (GtkTextView *text_view,
                         GdkEvent    *event)
{
  GtkTextBuffer *buffer;
  GtkTextIter start, end, iter, end_iter;
  GSList *tags = NULL;
  GSList *l = NULL;
  gdouble ex;
  gdouble ey;
  gint x;
  gint y;

  if (event->type == GDK_BUTTON_RELEASE)
    {
      GdkEventButton *event_button;

      event_button = (GdkEventButton *)event;
      if (event_button->button != GDK_BUTTON_PRIMARY)
        return GDK_EVENT_PROPAGATE;

      ex = event_button->x;
      ey = event_button->y;
    }
  else if (event->type == GDK_TOUCH_END)
    {
      GdkEventTouch *event_touch;

      event_touch = (GdkEventTouch *)event;

      ex = event_touch->x;
      ey = event_touch->y;
    }
  else
    {
      return GDK_EVENT_PROPAGATE;
    }

  buffer = gtk_text_view_get_buffer (text_view);

  /* We shouldn't follow a link if the user has selected something */
  gtk_text_buffer_get_selection_bounds (buffer, &start, &end);

  if (gtk_text_iter_get_offset (&start) != gtk_text_iter_get_offset (&end))
    return GDK_EVENT_PROPAGATE;

  gtk_text_view_window_to_buffer_coords (text_view, GTK_TEXT_WINDOW_WIDGET, ex, ey, &x, &y);

  if (!gtk_text_view_get_iter_at_location (text_view, &iter, x, y))
    return GDK_EVENT_PROPAGATE;

  tags = gtk_text_iter_get_tags (&iter);

  for (l = tags; l; l = l->next)
    {
      g_autoptr (GError) error = NULL;
      g_autofree gchar *tag_name = NULL;
      g_autofree gchar *url = NULL;
      GtkTextIter url_start;
      GtkTextIter url_end;
      GtkTextTag *tag;
      GtkWindow *window;

      tag = l->data;

      g_object_get (tag, "name", &tag_name, NULL);

      if (g_strcmp0 (tag_name, "url") != 0)
        continue;

      gtk_text_buffer_get_iter_at_line (buffer, &iter, gtk_text_iter_get_line (&iter));
      end_iter = iter;
      gtk_text_iter_forward_to_line_end (&end_iter);

      /* Find the beginning... */
      if (!gtk_text_iter_forward_search (&iter, "(", GTK_TEXT_SEARCH_TEXT_ONLY, NULL, &url_start, NULL))
        continue;

      /* ... and the end of the URL */
      if (!gtk_text_iter_forward_search (&iter, ")", GTK_TEXT_SEARCH_TEXT_ONLY, &url_end, NULL, &end_iter))
        continue;

      url = gtk_text_iter_get_text (&url_start, &url_end);
      window = GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (text_view)));

      gtk_show_uri_on_window (window, url, GDK_CURRENT_TIME, &error);

      if (error)
        {
          g_warning ("%s", error->message);
          return GDK_EVENT_PROPAGATE;
        }
    }

  return GDK_EVENT_STOP;
}


/*
 * GObject overrides
 */

static void
gtd_edit_pane_finalize (GObject *object)
{
  GtdEditPane *self = (GtdEditPane *) object;

  g_clear_object (&self->task);

  G_OBJECT_CLASS (gtd_edit_pane_parent_class)->finalize (object);
}

static void
gtd_edit_pane_dispose (GObject *object)
{
  GtdEditPane *self = (GtdEditPane *) object;

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
   * GtdEditPane::changed:
   *
   * Emitted when the task was changed.
   */
  signals[CHANGED] = g_signal_new ("changed",
                                   GTD_TYPE_EDIT_PANE,
                                   G_SIGNAL_RUN_LAST,
                                   0,
                                   NULL,
                                   NULL,
                                   NULL,
                                   G_TYPE_NONE,
                                   0);

  /**
   * GtdEditPane::task-removed:
   *
   * Emitted when the user wants to remove the task.
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

  gtk_widget_class_bind_template_callback (widget_class, on_date_selected_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_delete_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_hyperlink_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_hyperlink_hover_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_no_date_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_priority_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_text_buffer_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_today_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_tomorrow_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_trap_textview_clicks_cb);

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

  if (!g_set_object (&self->task, task))
    return;

  if (task)
    {
      GtkTextBuffer *buffer;

      /* due date */
      update_date_widgets (self);

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

    }

  g_object_notify (G_OBJECT (self), "task");
}

void
gtd_edit_pane_set_markdown_renderer (GtdEditPane         *self,
                                     GtdMarkdownRenderer *renderer)
{
  GtkTextBuffer *buffer;

  g_assert (GTD_IS_EDIT_PANE (self));
  g_assert (GTD_IS_MARKDOWN_RENDERER (renderer));

  buffer = gtk_text_view_get_buffer (self->notes_textview);

  gtd_markdown_renderer_add_buffer (renderer, buffer);
}
