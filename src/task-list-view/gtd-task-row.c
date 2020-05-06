/* gtd-task-row.c
 *
 * Copyright (C) 2015-2020 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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
#include "gtd-manager.h"
#include "gtd-markdown-renderer.h"
#include "gtd-provider.h"
#include "gtd-rows-common-private.h"
#include "gtd-star-widget.h"
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
  GtkWidget           parent;

  /*<private>*/
  GtkWidget          *content_box;
  GtkWidget          *main_box;

  GtkWidget          *done_check;
  GtkWidget          *edit_panel_revealer;
  GtkWidget          *header_event_box;
  GtdStarWidget      *star_widget;
  GtkWidget          *title_entry;

  /* task widgets */
  GtkLabel           *task_date_label;
  GtkLabel           *task_list_label;

  /* dnd widgets */
  GtkWidget          *dnd_box;
  GtkWidget          *dnd_frame;
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

static void          on_star_widget_activated_cb                 (GtdStarWidget      *star_widget,
                                                                  GParamSpec         *pspec,
                                                                  GtdTaskRow         *self);

G_DEFINE_TYPE (GtdTaskRow, gtd_task_row, GTK_TYPE_WIDGET)

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

#if 0
static GtkWidget*
create_transient_row (GtdTaskRow *self)
{
  GtdTaskRow *new_row;

  new_row = GTD_TASK_ROW (gtd_task_row_new (self->task, self->renderer));

  gtk_widget_set_size_request (GTK_WIDGET (new_row),
                               gtk_widget_get_allocated_width (GTK_WIDGET (self)),
                               -1);

  gtk_revealer_set_reveal_child (GTK_REVEALER (new_row->edit_panel_revealer), self->active);

  gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (new_row)), "background");
  gtk_widget_set_opacity (GTK_WIDGET (new_row), 0.5);

  return GTK_WIDGET (new_row);
}
#endif


/*
 * Callbacks
 */

static void
on_task_updated_cb (GObject      *object,
                    GAsyncResult *result,
                    gpointer      user_data)
{
  g_autoptr (GError) error = NULL;

  gtd_provider_update_task_finish (GTD_PROVIDER (object), result, &error);

  if (error)
    {
      g_warning ("Error updating task: %s", error->message);
      return;
    }
}

static void
on_remove_task_cb (GtdEditPane *edit_panel,
                   GtdTask     *task,
                   GtdTaskRow  *self)
{
  g_signal_emit (self, signals[REMOVE_TASK], 0);
}

static void
on_button_press_event_cb (GtkGestureClick *gesture,
                          gint             n_press,
                          gdouble          x,
                          gdouble          y,
                          GtdTaskRow      *self)
{
  GtkWidget *widget;
  gint real_x;
  gint real_y;

  widget = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));

  gtk_widget_translate_coordinates (widget,
                                    GTK_WIDGET (self),
                                    x,
                                    y,
                                    &real_x,
                                    &real_y);

  self->clicked_x = real_x;
  self->clicked_y = real_y;

  GTD_TRACE_MSG ("GtkGestureClick:pressed received from a %s at %.1f,%.1f (%d,%d)",
                 G_OBJECT_TYPE_NAME (widget),
                 x,
                 y,
                 real_x,
                 real_y);
}

static GdkContentProvider*
on_drag_prepare_cb (GtkDragSource *source,
                    gdouble        x,
                    gdouble        y,
                    GtdTaskRow    *self)
{
  GdkContentProvider *content;
  GTD_ENTRY;

  /* Setup the content provider */
  content = gdk_content_provider_new_typed (GTD_TYPE_TASK, self->task);

  GTD_RETURN (content);
}

static void
on_drag_begin_cb (GtkDragSource *source,
                  GdkDrag       *drag,
                  GtdTaskRow    *self)
{
  GtkWidget *widget;

  GTD_ENTRY;

  widget = GTK_WIDGET (self);

  gtk_widget_set_cursor_from_name (widget, "grabbing");

#if 0
  g_autoptr (GtkWidget) drag_icon = NULL;
  GtkWidget * *new_row;
  gint x_offset;

  /*
   * gtk_drag_set_icon_widget() inserts the row in a different GtkWindow, so
   * we have to create a new, transient row.
   */
  new_row = create_transient_row (self);
  drag_icon = gtk_drag_icon_new_for_drag (drag);
  gtk_container_add (GTK_CONTAINER (drag_icon), new_row);

  x_offset = gtk_widget_get_margin_start (self->content_box);
  gdk_drag_set_hotspot (drag, self->clicked_x + x_offset, self->clicked_y);
#endif

  gtk_widget_hide (widget);

  GTD_EXIT;
}

static void
on_drag_end_cb (GtkDragSource *source,
                GdkDrag       *drag,
                gboolean       delete_data,
                GtdTaskRow    *self)
{
  GTD_ENTRY;

  gtd_task_row_unset_drag_offset (self);

  gtk_widget_set_cursor_from_name (GTK_WIDGET (self), NULL);
  gtk_widget_show (GTK_WIDGET (self));

  GTD_EXIT;
}

static gboolean
on_drag_cancelled_cb (GtkDragSource       *source,
                      GdkDrag             *drag,
                      GdkDragCancelReason  result,
                      GtdTaskRow          *self)
{
  GTD_ENTRY;

  gtd_task_row_unset_drag_offset (self);

  gtk_widget_set_cursor_from_name (GTK_WIDGET (self), NULL);
  gtk_widget_show (GTK_WIDGET (self));

  GTD_RETURN (FALSE);
}

static void
on_complete_check_toggled_cb (GtkToggleButton *button,
                              GtdTaskRow      *self)
{
  GTD_ENTRY;

  g_assert (GTD_IS_TASK (self->task));

  gtd_task_set_complete (self->task, gtk_toggle_button_get_active (button));
  gtd_provider_update_task (gtd_task_get_provider (self->task),
                            self->task,
                            NULL,
                            on_task_updated_cb,
                            self);

  GTD_EXIT;
}

static void
on_complete_changed_cb (GtdTaskRow *self,
                        GParamSpec *pspec,
                        GtdTask    *task)
{
  GtkStyleContext *context;
  gboolean complete;

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
}

static void
on_depth_changed_cb (GtdTaskRow *self,
                     GParamSpec *pspec,
                     GtdTask    *task)
{
  gint margin = 0;

  if (self->handle_subtasks)
    margin = 32 * gtd_task_get_depth (task);

  gtk_widget_set_margin_start (self->content_box, margin);
}

static void
on_task_important_changed_cb (GtdTask    *task,
                              GParamSpec *pspec,
                              GtdTaskRow *self)
{
  g_signal_handlers_block_by_func (self->star_widget, on_star_widget_activated_cb, self);

  gtd_star_widget_set_active (self->star_widget, gtd_task_get_important (task));

  g_signal_handlers_unblock_by_func (self->star_widget, on_star_widget_activated_cb, self);
}

static void
on_star_widget_activated_cb (GtdStarWidget *star_widget,
                             GParamSpec    *pspec,
                             GtdTaskRow    *self)
{
  g_signal_handlers_block_by_func (self->task, on_task_important_changed_cb, self);

  gtd_task_set_important (self->task, gtd_star_widget_get_active (star_widget));
  gtd_provider_update_task (gtd_task_get_provider (self->task),
                            self->task,
                            NULL,
                            on_task_updated_cb,
                            self);

  g_signal_handlers_unblock_by_func (self->task, on_task_important_changed_cb, self);
}

static gboolean
on_key_pressed_cb (GtkEventControllerKey *controller,
                   guint                  keyval,
                   guint                  keycode,
                   GdkModifierType        modifiers,
                   GtdTaskRow            *self)
{
  GTD_ENTRY;

  /* Exit when pressing Esc without modifiers */
  if (keyval == GDK_KEY_Escape && !(modifiers & (GDK_SHIFT_MASK | GDK_CONTROL_MASK)))
    {
      GTD_TRACE_MSG ("Escape pressed, closing task…");
      gtd_task_row_set_active (self, FALSE);
    }

  GTD_RETURN (GDK_EVENT_PROPAGATE);
}

static void
on_task_changed_cb (GtdTaskRow  *self)
{
  g_debug ("Task changed");

  self->changed = TRUE;
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
        {
          gtd_provider_update_task (gtd_task_get_provider (self->task),
                                    self->task,
                                    NULL,
                                    on_task_updated_cb,
                                    self);
        }
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
      g_assert (self->task == NULL);

      self->task = g_value_dup_object (value);
      g_assert (self->task != NULL);

      gtk_label_set_label (self->task_list_label, gtd_task_list_get_name (gtd_task_get_list (self->task)));

      g_signal_handlers_block_by_func (self->title_entry, on_task_changed_cb, self);
      g_signal_handlers_block_by_func (self->done_check, on_complete_check_toggled_cb, self);

      g_object_bind_property (self->task,
                              "loading",
                              self,
                              "sensitive",
                              G_BINDING_DEFAULT | G_BINDING_INVERT_BOOLEAN | G_BINDING_SYNC_CREATE);

      g_object_bind_property (self->task,
                              "title",
                              self->title_entry,
                              "text",
                              G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);

      g_object_bind_property_full (self->task,
                                   "due-date",
                                   self->task_date_label,
                                   "label",
                                   G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE,
                                   date_to_label_binding_cb,
                                   NULL,
                                   self,
                                   NULL);

      on_complete_changed_cb (self, NULL, self->task);
      g_signal_connect_object (self->task,
                               "notify::complete",
                               G_CALLBACK (on_complete_changed_cb),
                               self,
                               G_CONNECT_SWAPPED);

      on_depth_changed_cb (self, NULL, self->task);
      g_signal_connect_object (self->task,
                               "notify::depth",
                               G_CALLBACK (on_depth_changed_cb),
                               self,
                               G_CONNECT_SWAPPED);

      on_task_important_changed_cb (self->task, NULL, self);
      g_signal_connect_object (self->task,
                               "notify::important",
                               G_CALLBACK (on_task_important_changed_cb),
                               self,
                               0);

      g_signal_connect_object (self->star_widget,
                               "notify::active",
                               G_CALLBACK (on_star_widget_activated_cb),
                               self,
                               0);

      g_signal_handlers_unblock_by_func (self->done_check, on_complete_check_toggled_cb, self);
      g_signal_handlers_unblock_by_func (self->title_entry, on_task_changed_cb, self);

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

  widget_class->measure = gtd_row_measure_with_max;

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
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

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

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/todo/ui/task-list-view/gtd-task-row.ui");

  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, content_box);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, dnd_box);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, dnd_frame);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, dnd_icon);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, done_check);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, edit_panel_revealer);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, header_event_box);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, main_box);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, star_widget);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, task_date_label);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, task_list_label);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, title_entry);

  gtk_widget_class_bind_template_callback (widget_class, on_button_press_event_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_complete_check_toggled_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_key_pressed_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_remove_task_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_task_changed_cb);

  gtk_widget_class_set_css_name (widget_class, "taskrow");
}

static void
gtd_task_row_init (GtdTaskRow *self)
{
  GtkDragSource *drag_source;

  self->handle_subtasks = TRUE;
  self->active = FALSE;

  gtk_widget_init_template (GTK_WIDGET (self));

  drag_source = gtk_drag_source_new ();
  gtk_drag_source_set_actions (drag_source, GDK_ACTION_MOVE);

  g_signal_connect (drag_source, "prepare", G_CALLBACK (on_drag_prepare_cb), self);
  g_signal_connect (drag_source, "drag-begin", G_CALLBACK (on_drag_begin_cb), self);
  g_signal_connect (drag_source, "drag-cancel", G_CALLBACK (on_drag_cancelled_cb), self);
  g_signal_connect (drag_source, "drag-end", G_CALLBACK (on_drag_end_cb), self);

  gtk_widget_add_controller (GTK_WIDGET (self), GTK_EVENT_CONTROLLER (drag_source));

  gtk_widget_set_cursor_from_name (self->dnd_icon, "grab");
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

      gtd_provider_update_task (gtd_task_get_provider (self->task),
                                self->task,
                                NULL,
                                on_task_updated_cb,
                                self);
      self->changed = FALSE;
    }

  if (active)
    gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (self)), "active");
  else
    gtk_style_context_remove_class (gtk_widget_get_style_context (GTK_WIDGET (self)), "active");

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
    return gtk_widget_get_width (GTK_WIDGET (self)) - self->clicked_x;
  else
    return self->clicked_x;
}

void
gtd_task_row_set_drag_offset (GtdTaskRow *self,
                              GtdTaskRow *source_row,
                              gint        x_offset)
{
  gint current_task_depth;
  gint min_height;
  gint depth;

  g_return_if_fail (GTD_IS_TASK_ROW (self));

  /* Negative values are discarded */
  x_offset = MAX (x_offset, 0);

  /* Make the DnD frame match the height of the dragged row */
  gtk_widget_measure (GTK_WIDGET (self), GTK_ORIENTATION_VERTICAL, -1, &min_height, NULL, NULL, NULL);
  gtk_widget_set_size_request (self->dnd_frame, -1, min_height);

  current_task_depth = gtd_task_get_depth (self->task);
  depth = CLAMP (x_offset / 32, 0, current_task_depth + 1);
  gtk_widget_set_margin_start (self->dnd_frame, depth * 32 + 12);

  GTD_TRACE_MSG ("DnD frame height: %d, depth: %d", min_height, depth);

  gtk_widget_show (self->dnd_frame);
}

void
gtd_task_row_unset_drag_offset (GtdTaskRow *self)
{
  g_return_if_fail (GTD_IS_TASK_ROW (self));

  gtk_widget_hide (self->dnd_frame);
}

GtdTask*
gtd_task_row_get_dnd_drop_task (GtdTaskRow *self)
{
  GtdTask *task;
  gint task_depth;
  gint depth;
  gint i;

  g_return_val_if_fail (GTD_IS_TASK_ROW (self), NULL);

  task = self->task;
  task_depth = gtd_task_get_depth (task);
  depth = (gtk_widget_get_margin_start (self->dnd_frame) - 12) / 32;

  /* Find the real parent */
  for (i = task_depth - depth; i >= 0; i--)
    task = gtd_task_get_parent (task);

  return task;
}
