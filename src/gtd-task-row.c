/* gtd-task-row.c
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
#include "gtd-expandable-entry.h"
#include "gtd-manager.h"
#include "gtd-markup-renderer.h"
#include "gtd-provider.h"
#include "gtd-rows-common-private.h"
#include "gtd-task-row.h"
#include "gtd-task.h"
#include "gtd-task-list.h"
#include "gtd-task-list-view.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>

struct _GtdTaskRow
{
  GtkListBoxRow       parent;

  /*<private>*/
  GtkRevealer        *revealer;

  GtkWidget          *done_check;
  GtkWidget          *edit_panel;
  GtkWidget          *edit_panel_revealer;
  GtkWidget          *header_event_box;
  GtkWidget          *title_entry;
  GtkWidget          *toggle_button;

  /* task widgets */
  GtkLabel           *task_date_label;
  GtkLabel           *task_list_label;

  /* dnd widgets */
  GtkWidget          *dnd_box;
  GtkWidget          *dnd_event_box;
  GtkWidget          *dnd_icon;
  gint                clicked_x;
  gint                clicked_y;

  gboolean            handle_subtasks : 1;

  /* data */
  GtdTask            *task;

  gint                destroy_row_timeout_id;
  gboolean            active;
};

#define PRIORITY_ICON_SIZE 8

G_DEFINE_TYPE (GtdTaskRow, gtd_task_row, GTK_TYPE_LIST_BOX_ROW)

enum
{
  ENTER,
  EXIT,
  REMOVE_TASK,
  NUM_SIGNALS
};

enum
{
  PROP_0,
  PROP_HANDLE_SUBTASKS,
  PROP_TASK,
  LAST_PROP
};

static guint signals[NUM_SIGNALS] = { 0, };

/*
 * Auxiliary methods
 */

static GtkWidget*
create_transient_row (GtdTaskRow *self)
{
  GtdTaskRow *new_row;

  new_row = GTD_TASK_ROW (gtd_task_row_new (self->task));
  gtk_revealer_set_transition_duration (new_row->revealer, 0);
  gtk_revealer_set_reveal_child (new_row->revealer, TRUE);

  gtk_widget_set_size_request (GTK_WIDGET (new_row),
                               gtk_widget_get_allocated_width (GTK_WIDGET (self)),
                               -1);

  gtk_revealer_set_reveal_child (GTK_REVEALER (new_row->edit_panel_revealer), self->active);

  return GTK_WIDGET (new_row);
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
set_cursor (GtkWidget   *widget,
            const gchar *cursor_name)
{
  GdkDisplay *display;
  GdkCursor *cursor;

  if (!gtk_widget_get_realized (widget))
    return;

  cursor = NULL;
  display = gtk_widget_get_display (widget);

  if (cursor_name)
    cursor = gdk_cursor_new_from_name (display, cursor_name);

  gdk_window_set_cursor (gtk_widget_get_window (widget), cursor);
  gdk_display_flush (display);

  g_clear_object (&cursor);
}


/*
 * Callbacks
 */

static void
toggle_active_cb (GtkWidget  *button,
                  GtdTaskRow *self)
{
  if (self->active)
    gtd_manager_update_task (gtd_manager_get_default (), self->task);

  gtd_task_row_set_active (self, !self->active);
}

static void
remove_task_cb (GtdEditPane *edit_panel,
                GtdTask     *task,
                GtdTaskRow  *self)
{
  g_signal_emit (self, signals[REMOVE_TASK], 0);
}

static gboolean
mouse_out_event_cb (GtkWidget  *widget,
                    GdkEvent   *event,
                    GtdTaskRow *self)
{
  set_cursor (widget, NULL);

  return GDK_EVENT_STOP;
}

static gboolean
mouse_over_event_cb (GtkWidget  *widget,
                     GdkEvent   *event,
                     GtdTaskRow *self)
{
  set_cursor (widget, "pointer");

  return GDK_EVENT_STOP;
}


static gboolean
mouse_out_dnd_event_cb (GtkWidget  *widget,
                        GdkEvent   *event,
                        GtdTaskRow *self)
{
  set_cursor (widget, NULL);

  return GDK_EVENT_STOP;
}

static gboolean
mouse_over_dnd_event_cb (GtkWidget  *widget,
                         GdkEvent   *event,
                         GtdTaskRow *self)
{
  set_cursor (widget, "grab");

  return GDK_EVENT_STOP;
}

static gboolean
button_press_event (GtkWidget      *widget,
                    GdkEventButton *event,
                    GtdTaskRow     *self)
{
  gint real_x;
  gint real_y;

  gtk_widget_translate_coordinates (widget,
                                    GTK_WIDGET (self),
                                    event->x,
                                    event->y,
                                    &real_x,
                                    &real_y);

  self->clicked_x = real_x;
  self->clicked_y = real_y;

  return GDK_EVENT_PROPAGATE;
}

static void
drag_begin_cb (GtkWidget      *event_box,
               GdkDragContext *context,
               GtdTaskRow     *self)
{
  GtkWidget *widget, *new_row;
  gint x_offset;

  widget = GTK_WIDGET (self);

  set_cursor (widget, "grabbing");

  /*
   * gtk_drag_set_icon_widget() inserts the row in a different GtkWindow, so
   * we have to create a new, transient row.
   */
  new_row = create_transient_row (self);

  if (gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL)
    x_offset = gtk_widget_get_margin_end (GTK_WIDGET (self));
  else
    x_offset = gtk_widget_get_margin_start (GTK_WIDGET (self));

  gtk_drag_set_icon_widget (context,
                            new_row,
                            self->clicked_x + x_offset,
                            self->clicked_y);

  gtk_widget_hide (widget);
}

static void
drag_end_cb (GtkWidget      *event_box,
             GdkDragContext *context,
             GtdTaskRow     *self)
{
  set_cursor (GTK_WIDGET (self), NULL);
  gtk_widget_show (GTK_WIDGET (self));
}

static gboolean
drag_failed_cb (GtkWidget      *widget,
                GdkDragContext *context,
                GtkDragResult   result,
                GtdTaskRow     *self)
{
  set_cursor (GTK_WIDGET (self), NULL);
  gtk_widget_show (GTK_WIDGET (self));

  return FALSE;
}

static void
priority_changed_cb (GtdTaskRow *row,
                     GParamSpec *spec,
                     GObject    *object)
{
  GtkStyleContext *context;
  gint priority;

  context = gtk_widget_get_style_context (GTK_WIDGET (row));
  priority = gtd_task_get_priority (GTD_TASK (object));

  /* remove all styles */
  gtk_style_context_remove_class (context, "priority-low");
  gtk_style_context_remove_class (context, "priority-medium");
  gtk_style_context_remove_class (context, "priority-hight");

  switch (priority)
    {
    case 1:
      gtk_style_context_add_class (context, "priority-low");
      break;

    case 2:
      gtk_style_context_add_class (context, "priority-medium");
      break;

    case 3:
      gtk_style_context_add_class (context, "priority-hight");
      break;

    default:
      break;
    }

  /* redraw background according to the new applied style */
  gtk_widget_queue_draw (GTK_WIDGET (row));
}

static void
complete_changed_cb (GtdTaskRow *self,
                     GParamSpec *pspec,
                     GtdTask    *task)
{
  GtkStyleContext *context;

  context = gtk_widget_get_style_context (GTK_WIDGET (self));

  if (gtd_task_get_complete (task))
    gtk_style_context_add_class (context, "complete");
  else
    gtk_style_context_remove_class (context, "complete");
}

static void
toggle_complete_cb (GtkRevealer *revealer,
                    GParamSpec  *pspec,
                    GtdTaskRow  *self)
{
  g_signal_handlers_disconnect_by_func (revealer, toggle_complete_cb, self);

  gtd_task_set_complete (self->task, !gtd_task_get_complete (self->task));
}

static void
complete_check_toggled_cb (GtkToggleButton *button,
                           GtdTaskRow      *self)
{
  GtdTaskListView *listview;

  listview = GTD_TASK_LIST_VIEW (gtk_widget_get_ancestor (GTK_WIDGET (self),
                                                          GTD_TYPE_TASK_LIST_VIEW));

  /*
   * If the parent list view is showing completed tasks, we
   * don't have to hide the row. Simply toggle the 'complete'
   * property of the task.
   */
  if (gtd_task_list_view_get_show_completed (listview))
    {
      gtd_task_set_complete (self->task, !gtd_task_get_complete (self->task));
      return;
    }

  gtk_revealer_set_reveal_child (GTK_REVEALER (self->revealer),
                                 !gtk_toggle_button_get_active (button));

  g_signal_connect (self->revealer,
                    "notify::child-revealed",
                    G_CALLBACK (toggle_complete_cb),
                    self);
}

static void
depth_changed_cb (GtdTaskRow *self,
                  GParamSpec *pspec,
                  GtdTask    *task)
{
  gtk_widget_set_margin_start (GTK_WIDGET (self),
                               self->handle_subtasks ? 32 * gtd_task_get_depth (task) + 3: 3);
}

static gboolean
gtd_task_row__date_changed_binding (GBinding     *binding,
                                    const GValue *from_value,
                                    GValue       *to_value,
                                    gpointer      user_data)
{
  g_autofree gchar *new_label = NULL;
  GDateTime *dt;

  g_return_val_if_fail (GTD_IS_TASK_ROW (user_data), FALSE);

  dt = g_value_get_boxed (from_value);

  if (dt)
    {
      g_autoptr (GDateTime) today = g_date_time_new_now_local ();

      if (g_date_time_get_year (dt) == g_date_time_get_year (today) &&
          g_date_time_get_month (dt) == g_date_time_get_month (today))
        {
          if (g_date_time_get_day_of_month (dt) == g_date_time_get_day_of_month (today))
            {
              new_label = g_strdup (_("Today"));
            }
          else if (g_date_time_get_day_of_month (dt) == g_date_time_get_day_of_month (today) + 1)
            {
              new_label = g_strdup (_("Tomorrow"));
            }
          else if (g_date_time_get_day_of_month (dt) == g_date_time_get_day_of_month (today) - 1)
            {
              new_label = g_strdup (_("Yesterday"));
            }
          else if (g_date_time_get_day_of_year (dt) > g_date_time_get_day_of_month (today) &&
                   g_date_time_get_day_of_year (dt) < g_date_time_get_day_of_month (today) + 7)
            {
              new_label = g_date_time_format (dt, "%A");
            }
          else
            {
              new_label = g_date_time_format (dt, "%x");
            }
        }
      else
        {
          new_label = g_date_time_format (dt, "%x");
        }
    }
  else
    {
      new_label = g_strdup ("");
    }

  g_value_set_string (to_value, new_label);

  return TRUE;
}

static gboolean
gtd_task_row__destroy_cb (GtkWidget *row)
{
  gtk_widget_destroy (row);

  return G_SOURCE_REMOVE;
}


/*
 * GtkWidget overrides
 */

static gboolean
gtd_task_row__key_press_event (GtkWidget   *row,
                               GdkEventKey *event)
{
  GtdTaskRow *self = GTD_TASK_ROW (row);

  if (event->keyval == GDK_KEY_Escape && // Esc is pressed
      !(event->state & (GDK_SHIFT_MASK|GDK_CONTROL_MASK))) // No modifiers together
    {
      self->active = FALSE;
      g_signal_emit (row, signals[EXIT], 0);
    }

  return FALSE;
}


/*
 * GObject overrides
 */

static void
gtd_task_row_finalize (GObject *object)
{
  GtdTaskRow *self = GTD_TASK_ROW (object);

  g_clear_object (&self->task);

  G_OBJECT_CLASS (gtd_task_row_parent_class)->finalize (object);
}

static void
gtd_task_row_dispose (GObject *object)
{
  GtdTaskRow *self;
  GtdTask *task;
//  GtkTextView *view;

  self = GTD_TASK_ROW (object);
  task = self->task;
//  view = gtd_edit_pane_get_text_view (GTD_EDIT_PANE (self->edit_panel));

  if (task)
    {
      g_signal_handlers_disconnect_by_func (task, depth_changed_cb, self);
      g_signal_handlers_disconnect_by_func (task, complete_changed_cb, self);

      g_signal_handlers_disconnect_by_func (task, priority_changed_cb, self);
/*
      g_signal_handlers_disconnect_by_func (view, on_hyperlink_clicked, NULL);
      g_signal_handlers_disconnect_by_func (view, on_hyperlink_hover, NULL);
*/
    }

  G_OBJECT_CLASS (gtd_task_row_parent_class)->dispose (object);
}

static void
gtd_task_row_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  GtdTaskRow *self = GTD_TASK_ROW (object);

  switch (prop_id)
    {
    case PROP_HANDLE_SUBTASKS:
      g_value_set_boolean (value, self->handle_subtasks);
      break;

    case PROP_TASK:
      g_value_set_object (value, self->task);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_task_row_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  GtdTaskRow *self = GTD_TASK_ROW (object);

  switch (prop_id)
    {
    case PROP_HANDLE_SUBTASKS:
      gtd_task_row_set_handle_subtasks (self, g_value_get_boolean (value));
      break;

    case PROP_TASK:
      gtd_task_row_set_task (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_task_row_class_init (GtdTaskRowClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = gtd_task_row_dispose;
  object_class->finalize = gtd_task_row_finalize;
  object_class->get_property = gtd_task_row_get_property;
  object_class->set_property = gtd_task_row_set_property;

  widget_class->key_press_event = gtd_task_row__key_press_event;
  widget_class->get_preferred_width = gtd_row_get_preferred_width_with_max;

  g_type_ensure (GTD_TYPE_EXPANDABLE_ENTRY);

  /**
   * GtdTaskRow::handle-subtasks:
   *
   * If the row consider the task's subtasks to adjust various UI properties.
   */
  g_object_class_install_property (
          object_class,
          PROP_HANDLE_SUBTASKS,
          g_param_spec_boolean ("handle-subtasks",
                                "If the row adapts to subtasks",
                                "Whether the row adapts to the task's subtasks",
                                TRUE,
                                G_PARAM_READWRITE));

  /**
   * GtdTaskRow::task:
   *
   * The task that this row represents, or %NULL.
   */
  g_object_class_install_property (
          object_class,
          PROP_TASK,
          g_param_spec_object ("task",
                               "Task of the row",
                               "The task that this row represents",
                               GTD_TYPE_TASK,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  /**
   * GtdTaskRow::enter:
   *
   * Emitted when the row is focused and in the editing state.
   */
  signals[ENTER] = g_signal_new ("enter",
                                 GTD_TYPE_TASK_ROW,
                                 G_SIGNAL_RUN_LAST,
                                 0,
                                 NULL,
                                 NULL,
                                 NULL,
                                 G_TYPE_NONE,
                                 0);

  /**
   * GtdTaskRow::exit:
   *
   * Emitted when the row is unfocused and leaves the editing state.
   */
  signals[EXIT] = g_signal_new ("exit",
                                GTD_TYPE_TASK_ROW,
                                G_SIGNAL_RUN_LAST,
                                0,
                                NULL,
                                NULL,
                                NULL,
                                G_TYPE_NONE,
                                0);

  /**
   * GtdTaskRow::remove-task:
   *
   * Emitted when the user wants to delete the task represented by this row.
   */
  signals[REMOVE_TASK] = g_signal_new ("remove-task",
                                       GTD_TYPE_TASK_ROW,
                                       G_SIGNAL_RUN_LAST,
                                       0,
                                       NULL,
                                       NULL,
                                       NULL,
                                       G_TYPE_NONE,
                                       0);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/todo/ui/task-row.ui");

  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, dnd_box);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, dnd_event_box);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, dnd_icon);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, done_check);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, edit_panel);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, edit_panel_revealer);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, header_event_box);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, revealer);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, task_date_label);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, task_list_label);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, title_entry);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, toggle_button);

  gtk_widget_class_bind_template_callback (widget_class, button_press_event);
  gtk_widget_class_bind_template_callback (widget_class, complete_check_toggled_cb);
  gtk_widget_class_bind_template_callback (widget_class, drag_begin_cb);
  gtk_widget_class_bind_template_callback (widget_class, drag_end_cb);
  gtk_widget_class_bind_template_callback (widget_class, drag_failed_cb);
  gtk_widget_class_bind_template_callback (widget_class, mouse_out_event_cb);
  gtk_widget_class_bind_template_callback (widget_class, mouse_out_dnd_event_cb);
  gtk_widget_class_bind_template_callback (widget_class, mouse_over_event_cb);
  gtk_widget_class_bind_template_callback (widget_class, mouse_over_dnd_event_cb);
  gtk_widget_class_bind_template_callback (widget_class, remove_task_cb);
  gtk_widget_class_bind_template_callback (widget_class, toggle_active_cb);

  gtk_widget_class_set_css_name (widget_class, "taskrow");
}

static void
gtd_task_row_init (GtdTaskRow *self)
{
  self->handle_subtasks = TRUE;
  self->active = FALSE;

  gtk_widget_init_template (GTK_WIDGET (self));

  /* The source of DnD is the drag icon */
  gtk_drag_source_set (self->dnd_event_box,
                       GDK_BUTTON1_MASK,
                       NULL,
                       0,
                       GDK_ACTION_MOVE);

  /* But the rest of the row header is also draggable */
  gtk_drag_source_set (self->header_event_box,
                       GDK_BUTTON1_MASK,
                       NULL,
                       0,
                       GDK_ACTION_MOVE);
}

GtkWidget*
gtd_task_row_new (GtdTask *task)
{
  return g_object_new (GTD_TYPE_TASK_ROW,
                       "task", task,
                       NULL);
}

/**
 * gtd_task_row_get_task:
 * @row: a #GtdTaskRow
 *
 * Retrieves the #GtdTask that @row manages, or %NULL if none
 * is set.
 *
 * Returns: (transfer none): the internal task of @row
 */
GtdTask*
gtd_task_row_get_task (GtdTaskRow *row)
{
  g_return_val_if_fail (GTD_IS_TASK_ROW (row), NULL);

  return row->task;
}

/**
 * gtd_task_row_set_task:
 * @row: a #GtdTaskRow
 * @task: a #GtdTask
 *
 * Sets the internal #GtdTask of @row. The task must be set to %NULL
 * before setting GtdObject::new-task-mode to %TRUE.
 */
void
gtd_task_row_set_task (GtdTaskRow *row,
                       GtdTask    *task)
{
  g_return_if_fail (GTD_IS_TASK_ROW (row));

  if (!g_set_object (&row->task, task))
    return;

  if (task)
    {
      gtk_label_set_label (row->task_list_label, gtd_task_list_get_name (gtd_task_get_list (task)));

      g_signal_handlers_block_by_func (row->done_check, complete_check_toggled_cb, row);

      g_object_bind_property (task,
                              "title",
                              row->title_entry,
                              "text",
                              G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);

      g_object_bind_property (task,
                              "complete",
                              row->done_check,
                              "active",
                              G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

      g_object_bind_property_full (task,
                                   "due-date",
                                   row->task_date_label,
                                   "label",
                                   G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE,
                                   gtd_task_row__date_changed_binding,
                                   NULL,
                                   row,
                                   NULL);

      /*
       * Here we generate a false callback call just to reuse the method to
       * sync the initial state of the priority icon.
       */
      priority_changed_cb (row, NULL, G_OBJECT (task));
      g_signal_connect_swapped (task,
                                "notify::priority",
                                G_CALLBACK (priority_changed_cb),
                                row);

      complete_changed_cb (row, NULL, task);
      g_signal_connect_swapped (task,
                                "notify::complete",
                                G_CALLBACK (complete_changed_cb),
                                row);

      depth_changed_cb (row, NULL, task);
      g_signal_connect_swapped (task,
                                "notify::depth",
                                G_CALLBACK (depth_changed_cb),
                                row);

      g_signal_handlers_unblock_by_func (row->done_check, complete_check_toggled_cb, row);

      gtd_edit_pane_set_task (GTD_EDIT_PANE (row->edit_panel), task);
    }

  g_object_notify (G_OBJECT (row), "task");
}

/**
 * gtd_task_row_set_list_name_visible:
 * @row: a #GtdTaskRow
 * @show_list_name: %TRUE to show the list name, %FALSE to hide it
 *
 * Sets @row's list name label visibility to @show_list_name.
 */
void
gtd_task_row_set_list_name_visible (GtdTaskRow *row,
                                    gboolean    show_list_name)
{
  g_return_if_fail (GTD_IS_TASK_ROW (row));

  gtk_widget_set_visible (GTK_WIDGET (row->task_list_label), show_list_name);
}

/**
 * gtd_task_row_set_due_date_visible:
 * @row: a #GtdTaskRow
 * @show_due_date: %TRUE to show the due, %FALSE to hide it
 *
 * Sets @row's due date label visibility to @show_due_date.
 */
void
gtd_task_row_set_due_date_visible (GtdTaskRow *row,
                                   gboolean    show_due_date)
{
  g_return_if_fail (GTD_IS_TASK_ROW (row));

  gtk_widget_set_visible (GTK_WIDGET (row->task_date_label), show_due_date);
}

/**
 * gtd_task_row_reveal:
 * @row: a #GtdTaskRow
 *
 * Runs a nifty animation to reveal @row.
 */
void
gtd_task_row_reveal (GtdTaskRow *row)
{
  g_return_if_fail (GTD_IS_TASK_ROW (row));

  gtk_revealer_set_reveal_child (row->revealer, TRUE);
}

/**
 * gtd_task_row_destroy:
 * @self: a #GtdTaskRow
 *
 * Destroy @self after hiding it.
 */
void
gtd_task_row_destroy (GtdTaskRow *self)
{
  g_return_if_fail (GTD_IS_TASK_ROW (self));

  if (!gtk_revealer_get_child_revealed (self->revealer))
    {
      gtk_widget_destroy (GTK_WIDGET (self));
    }
  else if (self->destroy_row_timeout_id == 0)
    {
      guint duration;

      duration = gtk_revealer_get_transition_duration (self->revealer);

      gtk_revealer_set_reveal_child (self->revealer, FALSE);
      self->destroy_row_timeout_id = g_timeout_add (duration,
                                                    (GSourceFunc) gtd_task_row__destroy_cb,
                                                    self);
    }
}

gboolean
gtd_task_row_get_handle_subtasks (GtdTaskRow *self)
{
  g_return_val_if_fail (GTD_IS_TASK_ROW (self), FALSE);

  return self->handle_subtasks;
}

void
gtd_task_row_set_handle_subtasks (GtdTaskRow *self,
                                  gboolean    handle_subtasks)
{
  g_return_if_fail (GTD_IS_TASK_ROW (self));

  if (self->handle_subtasks == handle_subtasks)
    return;

  self->handle_subtasks = handle_subtasks;

  gtk_widget_set_visible (self->dnd_box, handle_subtasks);
  gtk_widget_set_visible (self->dnd_event_box, handle_subtasks);
  depth_changed_cb (self, NULL, self->task);

  if (handle_subtasks)
    {
      gtk_drag_source_set (self->header_event_box,
                           GDK_BUTTON1_MASK,
                           NULL,
                           0,
                           GDK_ACTION_MOVE);
    }
  else
    {
      gtk_drag_source_unset (self->header_event_box);
    }

  g_object_notify (G_OBJECT (self), "handle-subtasks");
}

gboolean
gtd_task_row_get_active (GtdTaskRow *self)
{
  g_return_val_if_fail (GTD_IS_TASK_ROW (self), FALSE);

  return self->active;
}

void
gtd_task_row_set_active (GtdTaskRow *self,
                         gboolean    active)
{
  GtkStyleContext *context;

  g_return_if_fail (GTD_IS_TASK_ROW (self));

  if (self->active == active)
    return;

  self->active = active;

  /* Update the arrow icon */
  context = gtk_widget_get_style_context (self->toggle_button);

  if (active)
    gtk_style_context_add_class (context, "active");
  else
    gtk_style_context_remove_class (context, "active");

  /* And the listbox */
  gtk_revealer_set_reveal_child (GTK_REVEALER (self->edit_panel_revealer), active);

  g_signal_emit (self, active ? signals[ENTER] : signals[EXIT], 0);
}

void
gtd_task_row_set_sizegroups (GtdTaskRow   *self,
                             GtkSizeGroup *name_group,
                             GtkSizeGroup *date_group)
{
  gtk_size_group_add_widget (name_group, GTK_WIDGET (self->task_list_label));
  gtk_size_group_add_widget (name_group, GTK_WIDGET (self->task_date_label));
}

gint
gtd_task_row_get_x_offset (GtdTaskRow *self)
{
  g_return_val_if_fail (GTD_IS_TASK_ROW (self), -1);

  if (gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL)
    return gtk_widget_get_allocated_width (GTK_WIDGET (self)) - self->clicked_x;
  else
    return self->clicked_x;
}

void
gtd_task_row_set_markup_renderer (GtdTaskRow         *row,
                                  GtdMarkupRenderer **renderer)
{
  GtkTextView   *view;
  GtkTextBuffer *buffer;

  view = gtd_edit_pane_get_text_view (GTD_EDIT_PANE (row->edit_panel));
  buffer = gtk_text_view_get_buffer (view);

  *renderer = g_object_new (GTD_TYPE_MARKUP_RENDERER,
                           "textbuffer",
                           buffer,
                           NULL);

  gtd_markup_renderer_populate_tag_table (*renderer);
  gtd_markup_renderer_render_markup (*renderer);

  g_signal_connect (view, "event_after",
                    G_CALLBACK (on_hyperlink_clicked), NULL);
  g_signal_connect (view, "motion-notify-event",
                    G_CALLBACK (on_hyperlink_hover), NULL);
}
