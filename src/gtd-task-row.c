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

#define G_LOG_DOMAIN "GtdTaskRow"

#include "gtd-debug.h"
#include "gtd-edit-pane.h"
#include "gtd-expandable-entry.h"
#include "gtd-manager.h"
#include "gtd-markdown-renderer.h"
#include "gtd-provider.h"
#include "gtd-rows-common-private.h"
#include "gtd-task-row.h"
#include "gtd-task.h"
#include "gtd-task-list.h"
#include "gtd-task-list-view.h"
#include "gtd-utils-private.h"

#include <glib/gi18n.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>

struct _GtdTaskRow
{
  GtkListBoxRow       parent;

  /*<private>*/
  GtkRevealer        *revealer;

  GtkWidget          *done_check;
  GtkWidget          *edit_panel_revealer;
  GtkWidget          *header_event_box;
  GtkWidget          *title_entry;

  /* task widgets */
  GtkLabel           *task_date_label;
  GtkLabel           *task_list_label;

  /* dnd widgets */
  GtkWidget          *dnd_box;
  GtkWidget          *dnd_icon;
  gint                clicked_x;
  gint                clicked_y;

  gboolean            handle_subtasks : 1;

  /* data */
  GtdTask            *task;

  GtdEditPane        *edit_pane;

  GtdMarkdownRenderer *renderer;

  gboolean            active;
  gboolean            changed;
};

#define PRIORITY_ICON_SIZE 8

static void          on_complete_changed_cb                      (GtdTaskRow         *self,
                                                                  GParamSpec         *pspec,
                                                                  GtdTask            *task);

static void          on_complete_check_toggled_cb                (GtkToggleButton    *button,
                                                                  GtdTaskRow         *self);

static void          on_depth_changed_cb                         (GtdTaskRow         *self,
                                                                  GParamSpec         *pspec,
                                                                  GtdTask            *task);

static void          on_priority_changed_cb                      (GtdTaskRow         *row,
                                                                  GParamSpec         *spec,
                                                                  GObject            *object);

static void          on_task_changed_cb                          (GtdTaskRow         *self);

static void          on_toggle_active_cb                         (GtkWidget          *button,
                                                                  GtdTaskRow         *self);


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
  PROP_RENDERER,
  PROP_TASK,
  LAST_PROP
};

static guint signals[NUM_SIGNALS] = { 0, };


/*
 * Auxiliary methods
 */

static gboolean
date_to_label_binding_cb (GBinding     *binding,
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

static GtkWidget*
create_transient_row (GtdTaskRow *self)
{
  GtdTaskRow *new_row;

  new_row = GTD_TASK_ROW (gtd_task_row_new (self->task, self->renderer));
  gtk_revealer_set_transition_duration (new_row->revealer, 0);
  gtk_revealer_set_reveal_child (new_row->revealer, TRUE);

  gtk_widget_set_size_request (GTK_WIDGET (new_row),
                               gtk_widget_get_allocated_width (GTK_WIDGET (self)),
                               -1);

  gtk_revealer_set_reveal_child (GTK_REVEALER (new_row->edit_panel_revealer), self->active);

  return GTK_WIDGET (new_row);
}

static void
gtd_task_row_set_task (GtdTaskRow *row,
                       GtdTask    *task)
{
  g_return_if_fail (GTD_IS_TASK_ROW (row));

  if (!g_set_object (&row->task, task))
    return;

  if (task)
    {
      gtk_label_set_label (row->task_list_label, gtd_task_list_get_name (gtd_task_get_list (task)));

      g_signal_handlers_block_by_func (row->title_entry, on_task_changed_cb, row);
      g_signal_handlers_block_by_func (row->done_check, on_complete_check_toggled_cb, row);

      g_object_bind_property (task,
                              "loading",
                              row,
                              "sensitive",
                              G_BINDING_DEFAULT | G_BINDING_INVERT_BOOLEAN | G_BINDING_SYNC_CREATE);

      g_object_bind_property (task,
                              "title",
                              row->title_entry,
                              "text",
                              G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);

      g_object_bind_property_full (task,
                                   "due-date",
                                   row->task_date_label,
                                   "label",
                                   G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE,
                                   date_to_label_binding_cb,
                                   NULL,
                                   row,
                                   NULL);

      /*
       * Here we generate a false callback call just to reuse the method to
       * sync the initial state of the priority icon.
       */
      on_priority_changed_cb (row, NULL, G_OBJECT (task));
      g_signal_connect_object (task,
                               "notify::priority",
                               G_CALLBACK (on_priority_changed_cb),
                               row,
                               G_CONNECT_SWAPPED);

      on_complete_changed_cb (row, NULL, task);
      g_signal_connect_object (task,
                               "notify::complete",
                               G_CALLBACK (on_complete_changed_cb),
                               row,
                               G_CONNECT_SWAPPED);

      on_depth_changed_cb (row, NULL, task);
      g_signal_connect_object (task,
                               "notify::depth",
                               G_CALLBACK (on_depth_changed_cb),
                               row,
                               G_CONNECT_SWAPPED);

      g_signal_handlers_unblock_by_func (row->done_check, on_complete_check_toggled_cb, row);
      g_signal_handlers_unblock_by_func (row->title_entry, on_task_changed_cb, row);
    }

  g_object_notify (G_OBJECT (row), "task");
}


/*
 * Callbacks
 */

static void
on_remove_task_cb (GtdEditPane *edit_panel,
                   GtdTask     *task,
                   GtdTaskRow  *self)
{
  g_signal_emit (self, signals[REMOVE_TASK], 0);
}

static gboolean
on_button_press_event_cb (GtkWidget  *widget,
                          GdkEvent   *event,
                          GtdTaskRow *self)
{
  gdouble event_x;
  gdouble event_y;
  gint real_x;
  gint real_y;

  if (gdk_event_get_event_type (event) != GDK_BUTTON_PRESS)
    return GDK_EVENT_PROPAGATE;

  gdk_event_get_coords (event, &event_x, &event_y);

  gtk_widget_translate_coordinates (widget,
                                    GTK_WIDGET (self),
                                    event_x,
                                    event_y,
                                    &real_x,
                                    &real_y);

  self->clicked_x = real_x;
  self->clicked_y = real_y;

  return GDK_EVENT_PROPAGATE;
}

static void
on_drag_begin_cb (GtkWidget  *event_box,
                  GdkDrag    *drag,
                  GtdTaskRow *self)
{
  GtkWidget *widget, *new_row;
  gint x_offset;

  widget = GTK_WIDGET (self);

  gtk_widget_set_cursor_from_name (widget, "grabbing");

  /*
   * gtk_drag_set_icon_widget() inserts the row in a different GtkWindow, so
   * we have to create a new, transient row.
   */
  new_row = create_transient_row (self);

  if (gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL)
    x_offset = gtk_widget_get_margin_end (GTK_WIDGET (self));
  else
    x_offset = gtk_widget_get_margin_start (GTK_WIDGET (self));

  gtk_drag_set_icon_widget (drag,
                            new_row,
                            self->clicked_x + x_offset,
                            self->clicked_y);

  gtk_widget_hide (widget);
}

static void
on_drag_data_get_cb (GtkWidget        *widget,
                     GdkDrag          *drag,
                     GtkSelectionData *data,
                     GtdTaskRow       *self)
{
}

static void
on_drag_end_cb (GtkWidget  *event_box,
                GdkDrag    *drag,
                GtdTaskRow *self)
{
  gtk_widget_set_cursor_from_name (GTK_WIDGET (self), NULL);
  gtk_widget_show (GTK_WIDGET (self));
}

static gboolean
on_drag_failed_cb (GtkWidget     *widget,
                   GdkDrag       *drag,
                   GtkDragResult  result,
                   GtdTaskRow    *self)
{
  gtk_widget_set_cursor_from_name (GTK_WIDGET (self), NULL);
  gtk_widget_show (GTK_WIDGET (self));

  return FALSE;
}

static void
on_priority_changed_cb (GtdTaskRow *row,
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
on_complete_changed_cb (GtdTaskRow *self,
                        GParamSpec *pspec,
                        GtdTask    *task)
{
  GtkStyleContext *context;
  gboolean complete;

  GTD_ENTRY;

  complete = gtd_task_get_complete (task);
  context = gtk_widget_get_style_context (GTK_WIDGET (self));

  if (complete)
    gtk_style_context_add_class (context, "complete");
  else
    gtk_style_context_remove_class (context, "complete");

  /* Update the toggle button as well */
  g_signal_handlers_block_by_func (self->done_check, on_complete_check_toggled_cb, self);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (self->done_check), complete);
  g_signal_handlers_unblock_by_func (self->done_check, on_complete_check_toggled_cb, self);

  GTD_EXIT;
}

static void
on_complete_check_toggled_cb (GtkToggleButton *button,
                              GtdTaskRow      *self)
{
  GTD_ENTRY;

  g_assert (GTD_IS_TASK (self->task));

  gtd_task_set_complete (self->task, gtk_toggle_button_get_active (button));
  gtd_provider_update_task (gtd_task_get_provider (self->task), self->task);

  GTD_EXIT;
}

static void
on_depth_changed_cb (GtdTaskRow *self,
                     GParamSpec *pspec,
                     GtdTask    *task)
{
  gtk_widget_set_margin_start (GTK_WIDGET (self),
                               self->handle_subtasks ? 32 * gtd_task_get_depth (task) + 3: 3);
}

static void
on_task_changed_cb (GtdTaskRow  *self)
{
  g_debug ("Task changed");

  self->changed = TRUE;
}


/*
 * GtkWidget overrides
 */

static gboolean
gtd_task_row_key_press_event (GtkWidget   *row,
                              GdkEventKey *event_key)
{
  GdkModifierType modifiers;
  GtdTaskRow *self;
  GdkEvent *event;
  guint keyval;

  self = GTD_TASK_ROW (row);
  event = (GdkEvent*) event_key;

  gdk_event_get_keyval (event, &keyval);
  gdk_event_get_state (event, &modifiers);

  /* Exit when pressing Esc without modifiers */
  if (keyval == GDK_KEY_Escape && !(modifiers & (GDK_SHIFT_MASK | GDK_CONTROL_MASK)))
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

  if (self->changed)
    {
      if (self->task)
        gtd_provider_update_task (gtd_task_get_provider (self->task), self->task);
      self->changed = FALSE;
    }

  g_clear_object (&self->task);

  G_OBJECT_CLASS (gtd_task_row_parent_class)->finalize (object);
}

static void
gtd_task_row_dispose (GObject *object)
{
  GtdTaskRow *self;
  GtdTask *task;

  self = GTD_TASK_ROW (object);
  task = self->task;

  if (task)
    {
      g_signal_handlers_disconnect_by_func (task, on_depth_changed_cb, self);
      g_signal_handlers_disconnect_by_func (task, on_complete_changed_cb, self);
      g_signal_handlers_disconnect_by_func (task, on_priority_changed_cb, self);
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

    case PROP_RENDERER:
      g_value_set_object (value, self->renderer);
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

    case PROP_RENDERER:
      self->renderer = g_value_get_object (value);
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

  //widget_class->key_press_event = gtd_task_row_key_press_event;
  widget_class->measure = gtd_row_measure_with_max;

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
   * GtdTaskRow::renderer:
   *
   * The internal markdown renderer.
   */
  g_object_class_install_property (
          object_class,
          PROP_RENDERER,
          g_param_spec_object ("renderer",
                               "Renderer",
                               "Renderer",
                               GTD_TYPE_MARKDOWN_RENDERER,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_PRIVATE | G_PARAM_STATIC_STRINGS));

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
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

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
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, dnd_icon);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, done_check);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, edit_panel_revealer);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, header_event_box);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, revealer);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, task_date_label);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, task_list_label);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, title_entry);

  gtk_widget_class_bind_template_callback (widget_class, on_button_press_event_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_complete_check_toggled_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_drag_begin_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_drag_data_get_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_drag_end_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_drag_failed_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_remove_task_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_task_changed_cb);

  gtk_widget_class_set_css_name (widget_class, "taskrow");
}

static void
gtd_task_row_init (GtdTaskRow *self)
{
  self->handle_subtasks = TRUE;
  self->active = FALSE;

  gtk_widget_init_template (GTK_WIDGET (self));

  /* DnD icon */
  gtk_drag_source_set (self->dnd_icon,
                       GDK_BUTTON1_MASK,
                       _gtd_get_content_formats (),
                       GDK_ACTION_MOVE);

  gtk_widget_set_cursor_from_name (self->dnd_icon, "grab");

  /* Header box */
  gtk_drag_source_set (self->header_event_box,
                       GDK_BUTTON1_MASK,
                       _gtd_get_content_formats (),
                       GDK_ACTION_MOVE);

  gtk_widget_set_cursor_from_name (self->header_event_box, "pointer");
}

GtkWidget*
gtd_task_row_new (GtdTask             *task,
                  GtdMarkdownRenderer *renderer)
{
  return g_object_new (GTD_TYPE_TASK_ROW,
                       "task", task,
                       "renderer", renderer,
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
gtd_task_row_reveal (GtdTaskRow *row,
                     gboolean    animated)
{
  g_return_if_fail (GTD_IS_TASK_ROW (row));

  if (!animated)
    gtk_revealer_set_transition_duration (row->revealer, 0);

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

  gtk_widget_destroy (GTK_WIDGET (self));
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
  gtk_widget_set_visible (self->dnd_icon, handle_subtasks);
  on_depth_changed_cb (self, NULL, self->task);

  if (handle_subtasks)
    {
      gtk_drag_source_set (self->header_event_box,
                           GDK_BUTTON1_MASK,
                           _gtd_get_content_formats (),
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
  g_return_if_fail (GTD_IS_TASK_ROW (self));

  if (self->active == active)
    return;

  self->active = active;

  /* Create or destroy the edit panel */
  if (active && !self->edit_pane)
    {
      GTD_TRACE_MSG ("Creating edit pane");

      self->edit_pane = GTD_EDIT_PANE (gtd_edit_pane_new ());
      gtd_edit_pane_set_markdown_renderer (self->edit_pane, self->renderer);
      gtd_edit_pane_set_task (self->edit_pane, self->task);

      gtk_container_add (GTK_CONTAINER (self->edit_panel_revealer), GTK_WIDGET (self->edit_pane));
      gtk_widget_show (GTK_WIDGET (self->edit_pane));

      g_signal_connect_swapped (self->edit_pane, "changed", G_CALLBACK (on_task_changed_cb), self);
      g_signal_connect (self->edit_pane, "remove-task", G_CALLBACK (on_remove_task_cb), self);
    }
  else if (!active && self->edit_pane)
    {
      GTD_TRACE_MSG ("Destroying edit pane");

      gtk_container_remove (GTK_CONTAINER (self->edit_panel_revealer), GTK_WIDGET (self->edit_pane));
      self->edit_pane = NULL;
    }

  /* And reveal or hide it */
  gtk_revealer_set_reveal_child (GTK_REVEALER (self->edit_panel_revealer), active);

  /* Save the task if it is not being loaded */
  if (!active && !gtd_object_get_loading (GTD_OBJECT (self->task)) && self->changed)
    {
      g_debug ("Saving task…");

      gtd_provider_update_task (gtd_task_get_provider (self->task), self->task);
      self->changed = FALSE;
    }

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
