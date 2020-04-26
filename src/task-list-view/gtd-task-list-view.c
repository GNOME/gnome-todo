/* gtd-task-list-view.c
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

#define G_LOG_DOMAIN "GtdTaskListView"

#include "gtd-debug.h"
#include "gtd-dnd-row.h"
#include "gtd-edit-pane.h"
#include "gtd-empty-list-widget.h"
#include "gtd-task-list-view.h"
#include "gtd-manager.h"
#include "gtd-markdown-renderer.h"
#include "gtd-new-task-row.h"
#include "gtd-notification.h"
#include "gtd-provider.h"
#include "gtd-row-header.h"
#include "gtd-task.h"
#include "gtd-task-list.h"
#include "gtd-task-row.h"
#include "gtd-utils-private.h"
#include "gtd-window.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

/**
 * SECTION:gtd-task-list-view
 * @Short_description: A widget to display tasklists
 * @Title:GtdTaskListView
 *
 * The #GtdTaskListView widget shows the tasks of a #GtdTaskList with
 * various options to fine-tune the appearance. Alternatively, one can
 * pass a #GList of #GtdTask objects.
 *
 * It supports custom sorting and header functions, so the tasks can be
 * sorted in various ways. See the "Today" and "Scheduled" panels for reference
 * implementations.
 *
 * Example:
 * |[
 * GtdTaskListView *view = gtd_task_list_view_new ();
 *
 * gtd_task_list_view_set_model (view, model);
 *
 * // Hide the '+ New task' row
 * gtd_task_list_view_set_show_new_task_row (view, FALSE);
 *
 * // Date which tasks will be automatically assigned
 * gtd_task_list_view_set_default_date (view, now);
 * ]|
 *
 */

typedef struct
{
  GtkListBox            *listbox;
  GtkListBoxRow         *new_task_row;
  GtkWidget             *scrolled_window;
  GtkStack              *stack;

  /* internal */
  gboolean               can_toggle;
  gboolean               show_due_date;
  gboolean               show_list_name;
  gboolean               handle_subtasks;
  GListModel            *model;
  GDateTime             *default_date;

  guint                  scroll_to_bottom_handler_id;

  GHashTable            *task_to_row;

  /* Markup renderer*/
  GtdMarkdownRenderer   *renderer;

  /* DnD */
  GtkListBoxRow         *highlighted_row;
  guint                  scroll_timeout_id;
  gboolean               scroll_up;

  /* color provider */
  GtkCssProvider        *color_provider;
  GdkRGBA               *color;

  /* action */
  GActionGroup          *action_group;

  /* Custom header function data */
  GtdTaskListViewHeaderFunc header_func;
  gpointer                  header_user_data;

  GtdTaskRow             *active_row;
  GtkSizeGroup           *due_date_sizegroup;
  GtkSizeGroup           *tasklist_name_sizegroup;
  GtdTaskListSelectorBehavior task_list_selector_behavior;
} GtdTaskListViewPrivate;

struct _GtdTaskListView
{
  GtkBox                  parent;

  /*<private>*/
  GtdTaskListViewPrivate *priv;
};

typedef enum
{
  GTD_IDLE_STATE_STARTED,
  GTD_IDLE_STATE_LOADING,
  GTD_IDLE_STATE_COMPLETE,
  GTD_IDLE_STATE_FINISHED,
} GtdIdleState;

typedef struct
{
  GtdTaskListView      *self;
  GtdIdleState          state;
  GPtrArray            *added;
  GPtrArray            *removed;
  guint32               current_item;
} GtdIdleData;

#define COLOR_TEMPLATE               "tasklistview {background-color: %s;}"
#define DND_SCROLL_OFFSET            24 //px
#define LUMINANCE(c)                 (0.299 * c->red + 0.587 * c->green + 0.114 * c->blue)
#define TASK_REMOVED_NOTIFICATION_ID "task-removed-id"


static void          on_clear_completed_tasks_activated_cb       (GSimpleAction      *simple,
                                                                  GVariant           *parameter,
                                                                  gpointer            user_data);

static void          on_remove_task_row_cb                       (GtdTaskRow         *row,
                                                                  GtdTaskListView    *self);

static void          on_task_row_entered_cb                      (GtdTaskListView    *self,
                                                                  GtdTaskRow         *row);

static void          on_task_row_exited_cb                       (GtdTaskListView    *self,
                                                                  GtdTaskRow         *row);

static gboolean      scroll_to_bottom_cb                         (gpointer            data);


G_DEFINE_TYPE_WITH_PRIVATE (GtdTaskListView, gtd_task_list_view, GTK_TYPE_BOX)

static const GActionEntry gtd_task_list_view_entries[] = {
  { "clear-completed-tasks", on_clear_completed_tasks_activated_cb },
};

typedef struct
{
  GtdTaskListView *view;
  GtdTask         *task;
} RemoveTaskData;

enum {
  PROP_0,
  PROP_COLOR,
  PROP_HANDLE_SUBTASKS,
  PROP_SHOW_LIST_NAME,
  PROP_SHOW_DUE_DATE,
  PROP_SHOW_NEW_TASK_ROW,
  LAST_PROP
};

typedef gboolean     (*IterateSubtaskFunc)                       (GtdTaskListView    *self,
                                                                  GtdTask            *task);


/*
 * Auxiliary methods
 */

static inline GtdTaskRow*
task_row_from_row (GtkListBoxRow *row)
{
  return GTD_TASK_ROW (gtk_bin_get_child (GTK_BIN (row)));
}

static void
set_active_row (GtdTaskListView *self,
                GtdTaskRow      *row)
{
  GtdTaskListViewPrivate *priv = gtd_task_list_view_get_instance_private (self);

  if (priv->active_row == row)
    return;

  if (priv->active_row)
    gtd_task_row_set_active (priv->active_row, FALSE);

  priv->active_row = row;

  if (row)
    {
      gtd_task_row_set_active (row, TRUE);
      gtk_widget_grab_focus (GTK_WIDGET (row));
    }
}

static gboolean
iterate_subtasks (GtdTaskListView    *self,
                  GtdTask            *task,
                  IterateSubtaskFunc  func)
{
  GtdTask *aux;

  if (!func (self, task))
    return FALSE;

  for (aux = gtd_task_get_first_subtask (task);
       aux;
       aux = gtd_task_get_next_sibling (aux))
    {
      if (!iterate_subtasks (self, aux, func))
        return FALSE;
    }

  return TRUE;
}

static void
update_font_color (GtdTaskListView *self)
{
  GtdTaskListViewPrivate *priv;
  GtkStyleContext *context;
  GdkRGBA *color;

  priv = gtd_task_list_view_get_instance_private (self);

  if (!priv->model || !GTD_IS_TASK_LIST (priv->model))
    return;

  context = gtk_widget_get_style_context (GTK_WIDGET (self));
  color = gtd_task_list_get_color (GTD_TASK_LIST (priv->model));

  if (LUMINANCE (color) < 0.5)
    {
      gtk_style_context_add_class (context, "dark");
      gtk_style_context_remove_class (context, "light");
    }
  else
    {
      gtk_style_context_add_class (context, "light");
      gtk_style_context_remove_class (context, "dark");
    }

  gdk_rgba_free (color);
}

static void
schedule_scroll_to_bottom (GtdTaskListView *self)
{
  GtdTaskListViewPrivate *priv = gtd_task_list_view_get_instance_private (self);

  if (priv->scroll_to_bottom_handler_id > 0)
    return;

  priv->scroll_to_bottom_handler_id = g_timeout_add (250, scroll_to_bottom_cb, self);
}


/*
 * Callbacks
 */

static GtkWidget*
create_row_for_task_cb (gpointer item,
                        gpointer user_data)
{
  GtdTaskListViewPrivate *priv;
  GtdTaskListView *self;
  GtkWidget *listbox_row;
  GtkWidget *row;

  self = GTD_TASK_LIST_VIEW (user_data);
  priv = gtd_task_list_view_get_instance_private (self);

  row = gtd_task_row_new (item, priv->renderer);

  g_object_bind_property (self,
                          "handle-subtasks",
                          row,
                          "handle-subtasks",
                          G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

  gtd_task_row_set_list_name_visible (GTD_TASK_ROW (row), priv->show_list_name);
  gtd_task_row_set_due_date_visible (GTD_TASK_ROW (row), priv->show_due_date);

  g_signal_connect_swapped (row, "enter", G_CALLBACK (on_task_row_entered_cb), self);
  g_signal_connect_swapped (row, "exit", G_CALLBACK (on_task_row_exited_cb), self);

  g_signal_connect (row, "remove-task", G_CALLBACK (on_remove_task_row_cb), self);

  listbox_row = gtk_list_box_row_new ();
  gtk_widget_set_halign (listbox_row, GTK_ALIGN_CENTER);
  gtk_container_add (GTK_CONTAINER (listbox_row), row);

  g_object_bind_property (row, "visible", listbox_row, "visible", G_BINDING_BIDIRECTIONAL);

  g_hash_table_insert (priv->task_to_row, item, row);

  return listbox_row;
}

static gboolean
scroll_to_bottom_cb (gpointer data)
{
  GtdTaskListViewPrivate *priv;
  GtkWidget *widget;
  GtkRoot *root;

  priv = gtd_task_list_view_get_instance_private (data);
  widget = GTK_WIDGET (data);
  root = gtk_widget_get_root (widget);

  g_assert (root != NULL);

  priv->scroll_to_bottom_handler_id = 0;

  /*
   * Only focus the new task row if the current list is visible,
   * and the focused widget isn't inside this list view.
   */
  if (gtk_widget_get_visible (widget) &&
      gtk_widget_get_child_visible (widget) &&
      gtk_widget_get_mapped (widget) &&
      !gtk_widget_is_ancestor (gtk_window_get_focus (GTK_WINDOW (root)), widget))
    {
      gboolean ignored;

      gtk_widget_grab_focus (GTK_WIDGET (priv->new_task_row));
      g_signal_emit_by_name (priv->scrolled_window, "scroll-child", GTK_SCROLL_END, FALSE, &ignored);
    }

  return G_SOURCE_REMOVE;
}

static inline gboolean
remove_task_cb (GtdTaskListView *self,
                GtdTask         *task)
{
  gtd_provider_remove_task (gtd_task_get_provider (task), task);
  return TRUE;
}

static void
on_clear_completed_tasks_activated_cb (GSimpleAction *simple,
                                       GVariant      *parameter,
                                       gpointer       user_data)
{
  GtdTaskListView *self;
  GListModel *model;
  guint i;

  self = GTD_TASK_LIST_VIEW (user_data);
  model = self->priv->model;

  for (i = 0; i < g_list_model_get_n_items (model); i++)
    {
      g_autoptr (GtdTask) task = g_list_model_get_item (model, i);

      if (!gtd_task_get_complete (task))
        continue;

      if (gtd_task_get_parent (task))
        gtd_task_remove_subtask (gtd_task_get_parent (task), task);

      /* Remove the subtasks recursively */
      iterate_subtasks (self, task, remove_task_cb);
    }
}

static void
on_remove_task_action_cb (GtdNotification *notification,
                          gpointer         user_data)
{
  RemoveTaskData *data;
  GtdTask *task;

  data = user_data;
  task = data->task;

  if (gtd_task_get_parent (task))
    gtd_task_remove_subtask (gtd_task_get_parent (task), task);

  /* Remove the subtasks recursively */
  iterate_subtasks (data->view, data->task, remove_task_cb);

  g_clear_pointer (&data, g_free);
}

static void
on_undo_remove_task_action_cb (GtdNotification *notification,
                               gpointer         user_data)
{
  RemoveTaskData *data;
  GtdTaskList *list;

  data = user_data;

  /*
   * Readd task to the list. This will emit GListModel:items-changed (since
   * GtdTaskList implements GListModel) and the row will be added back.
   */
  list = gtd_task_get_list (data->task);
  gtd_task_list_add_task (list, data->task);

  g_free (data);
}

static void
on_remove_task_row_cb (GtdTaskRow      *row,
                       GtdTaskListView *self)
{
  g_autofree gchar *text = NULL;
  GtdNotification *notification;
  RemoveTaskData *data;
  GtdTaskList *list;
  GtdWindow *window;
  GtdTask *task;

  task = gtd_task_row_get_task (row);

  text = g_strdup_printf (_("Task <b>%s</b> removed"), gtd_task_get_title (task));
  window = GTD_WINDOW (gtk_widget_get_root (GTK_WIDGET (self)));

  data = g_new0 (RemoveTaskData, 1);
  data->view = self;
  data->task = task;

  /* Remove tasks and subtasks from the list */
  list = gtd_task_get_list (task);
  gtd_task_list_remove_task (list, task);

  /* Notify about the removal */
  notification = gtd_notification_new (text, 5000.0);

  gtd_notification_set_primary_action (notification,
                                       (GtdNotificationActionFunc) on_remove_task_action_cb,
                                       data);

  gtd_notification_set_secondary_action (notification,
                                         _("Undo"),
                                         (GtdNotificationActionFunc) on_undo_remove_task_action_cb,
                                         data);

  gtd_window_notify (window, notification);


  /* Clear the active row */
  set_active_row (self, NULL);
}

static void
on_task_list_color_changed_cb (GtdTaskListView *self)
{
  GtdTaskListViewPrivate *priv = GTD_TASK_LIST_VIEW (self)->priv;
  gchar *color_str;
  gchar *parsed_css;

  /* Add the color to provider */
  if (priv->color)
    {
      color_str = gdk_rgba_to_string (priv->color);
    }
  else
    {
      GdkRGBA *color;

      color = gtd_task_list_get_color (GTD_TASK_LIST (priv->model));
      color_str = gdk_rgba_to_string (color);

      gdk_rgba_free (color);
    }

  parsed_css = g_strdup_printf (COLOR_TEMPLATE, color_str);

  gtk_css_provider_load_from_data (priv->color_provider, parsed_css, -1);

  update_font_color (self);

  g_free (color_str);
}
static void
on_new_task_row_entered_cb (GtdTaskListView *self,
                            GtdNewTaskRow   *row)
{
  set_active_row (self, NULL);
}

static void
on_new_task_row_exited_cb (GtdTaskListView *self,
                           GtdNewTaskRow   *row)
{

}

static void
on_task_row_entered_cb (GtdTaskListView *self,
                        GtdTaskRow      *row)
{
  set_active_row (self, row);
}

static void
on_task_row_exited_cb (GtdTaskListView *self,
                       GtdTaskRow      *row)
{
  GtdTaskListViewPrivate *priv = self->priv;

  if (row == priv->active_row)
    set_active_row (self, NULL);
}

static void
on_listbox_row_activated_cb (GtkListBox      *listbox,
                             GtkListBoxRow   *row,
                             GtdTaskListView *self)
{
  GtdTaskRow *task_row;

  GTD_ENTRY;

  task_row = task_row_from_row (row);

  /* Toggle the row */
  if (gtd_task_row_get_active (task_row))
    set_active_row (self, NULL);
  else
    set_active_row (self, task_row);

  GTD_EXIT;
}


/*
 * Custom sorting functions
 */

static void
internal_header_func (GtkListBoxRow   *row,
                      GtkListBoxRow   *before,
                      GtdTaskListView *view)
{
  GtkWidget *header;
  GtdTask *row_task;
  GtdTask *before_task;

  if (!view->priv->header_func)
    return;

  row_task = before_task = NULL;

  if (row)
    row_task = gtd_task_row_get_task (task_row_from_row (row));

  if (before)
      before_task = gtd_task_row_get_task (task_row_from_row (before));

  header = view->priv->header_func (row_task, before_task, view->priv->header_user_data);

  if (header)
    {
      GtkWidget *real_header = gtd_row_header_new ();
      gtk_container_add (GTK_CONTAINER (real_header), header);

      header = real_header;
    }

  gtk_list_box_row_set_header (row, header);
}


/*
 * Drag n' Drop functions
 */

static gboolean
row_is_subtask_of (GtdTaskRow *row_a,
                   GtdTaskRow *row_b)
{
  GtdTask *task_a;
  GtdTask *task_b;

  task_a = gtd_task_row_get_task (row_a);
  task_b = gtd_task_row_get_task (row_b);

  return gtd_task_is_subtask (task_a, task_b);
}

static GtkListBoxRow*
get_drop_row_at_y (GtdTaskListView *self,
                   gint             y)
{
  GtdTaskListViewPrivate *priv;
  GtkListBoxRow *hovered_row;
  GtkListBoxRow *task_row;
  GtkListBoxRow *drop_row;
  gint row_y, row_height;

  priv = gtd_task_list_view_get_instance_private (self);

  hovered_row = gtk_list_box_get_row_at_y (priv->listbox, y);

  /* Small optimization when hovering the first row */
  if (gtk_list_box_row_get_index (hovered_row) == 0)
    return hovered_row;

  drop_row = NULL;
  task_row = hovered_row;
  row_height = gtk_widget_get_allocated_height (GTK_WIDGET (hovered_row));
  gtk_widget_translate_coordinates (GTK_WIDGET (priv->listbox),
                                    GTK_WIDGET (hovered_row),
                                    0,
                                    y,
                                    NULL,
                                    &row_y);

  /*
   * If the pointer if in the top part of the row, move the DnD row to
   * the previous row.
   */
  if (row_y < row_height / 2)
    {
      gint row_index, i;

      row_index = gtk_list_box_row_get_index (hovered_row);

      /* Search for a valid task row */
      for (i = row_index - 1; i >= 0; i--)
        {
          GtkListBoxRow *aux;

          aux = gtk_list_box_get_row_at_index (GTK_LIST_BOX (priv->listbox), i);

          /* Skip DnD, New task and hidden rows */
          if (aux && !gtk_widget_get_visible (GTK_WIDGET (aux)))
            continue;

          drop_row = aux;
          break;
        }
    }
  else
    {
      drop_row = task_row;
    }

  return drop_row ? drop_row : NULL;
}

static void
unset_previously_highlighted_row (GtdTaskListView *self)
{
  GtdTaskListViewPrivate *priv = gtd_task_list_view_get_instance_private (self);
  if (priv->highlighted_row)
    {
      gtd_task_row_unset_drag_offset (task_row_from_row (priv->highlighted_row));
      priv->highlighted_row = NULL;
    }
}

static inline gboolean
scroll_to_dnd (gpointer user_data)
{
  GtdTaskListViewPrivate *priv;
  GtkAdjustment *vadjustment;
  gint value;

  priv = gtd_task_list_view_get_instance_private (user_data);
  vadjustment = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (priv->scrolled_window));
  value = gtk_adjustment_get_value (vadjustment) + (priv->scroll_up ? -6 : 6);

  gtk_adjustment_set_value (vadjustment,
                            CLAMP (value, 0, gtk_adjustment_get_upper (vadjustment)));

  return G_SOURCE_CONTINUE;
}

static void
check_dnd_scroll (GtdTaskListView *self,
                  gboolean         should_cancel,
                  gint             y)
{
  GtdTaskListViewPrivate *priv = gtd_task_list_view_get_instance_private (self);
  gint current_y, height;

  if (should_cancel)
    {
      if (priv->scroll_timeout_id > 0)
        {
          g_source_remove (priv->scroll_timeout_id);
          priv->scroll_timeout_id = 0;
        }

      return;
    }

  height = gtk_widget_get_allocated_height (priv->scrolled_window);
  gtk_widget_translate_coordinates (GTK_WIDGET (priv->listbox),
                                    priv->scrolled_window,
                                    0, y,
                                    NULL, &current_y);

  if (current_y < DND_SCROLL_OFFSET || current_y > height - DND_SCROLL_OFFSET)
    {
      if (priv->scroll_timeout_id > 0)
        return;

      /* Start the autoscroll */
      priv->scroll_up = current_y < DND_SCROLL_OFFSET;
      priv->scroll_timeout_id = g_timeout_add (25,
                                               scroll_to_dnd,
                                               self);
    }
  else
    {
      if (priv->scroll_timeout_id == 0)
        return;

      /* Cancel the autoscroll */
      g_source_remove (priv->scroll_timeout_id);
      priv->scroll_timeout_id = 0;
    }
}

static void
on_drop_target_drag_leave_cb (GtkDropTarget   *drop_target,
                              GtdTaskListView *self)
{
  unset_previously_highlighted_row (self);
  check_dnd_scroll (self, TRUE, -1);
}

static GdkDragAction
on_drop_target_drag_motion_cb (GtkDropTarget   *drop_target,
                               gdouble          x,
                               gdouble          y,
                               GtdTaskListView *self)
{
  GtdTaskListViewPrivate *priv;
  GtkListBoxRow *highlighted_row;
  GtdTaskRow *highlighted_task_row;
  GtdTaskRow *source_task_row;
  const GValue *value;
  GdkDrop *drop;
  GtdTask *task;
  GdkDrag *drag;
  gint x_offset;

  GTD_ENTRY;

  priv = gtd_task_list_view_get_instance_private (self);
  drop = gtk_drop_target_get_drop (drop_target);
  drag = gdk_drop_get_drag (drop);

  if (!drag)
    {
      g_info ("Only dragging task rows is supported");
      GTD_GOTO (fail);
    }

  value = gtk_drop_target_get_value (drop_target);
  task = g_value_get_object (value);

  source_task_row = g_hash_table_lookup (priv->task_to_row, task);

  /* Update the x value according to the current offset */
  if (gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL)
    x += gtd_task_row_get_x_offset (source_task_row);
  else
    x -= gtd_task_row_get_x_offset (source_task_row);

  unset_previously_highlighted_row (self);

  highlighted_row = get_drop_row_at_y (self, y);
  if (!highlighted_row)
    GTD_GOTO (success);

  highlighted_task_row = task_row_from_row (highlighted_row);

  /* Forbid dropping a row over a subtask row */
  if (row_is_subtask_of (source_task_row, highlighted_task_row))
    GTD_GOTO (fail);

  gtk_widget_translate_coordinates (GTK_WIDGET (priv->listbox),
                                    GTK_WIDGET (highlighted_task_row),
                                    x,
                                    0,
                                    &x_offset,
                                    NULL);

  gtd_task_row_set_drag_offset (highlighted_task_row, source_task_row, x_offset);
  priv->highlighted_row = highlighted_row;

success:
  check_dnd_scroll (self, FALSE, y);
  GTD_RETURN (GDK_ACTION_MOVE);

fail:
  GTD_RETURN (0);
}

static gboolean
on_drop_target_drag_drop_cb (GtkDropTarget   *drop_target,
                             const GValue    *value,
                             gdouble          x,
                             gdouble          y,
                             GtdTaskListView *self)
{
  GtdTaskListViewPrivate *priv;
  GtkListBoxRow *drop_row;
  GtdProvider *provider;
  GtdTaskRow *hovered_row;
  GtkWidget *row;
  GtdTask *new_parent_task;
  GtdTask *hovered_task;
  GtdTask *source_task;
  GdkDrop *drop;
  GdkDrag *drag;
  gint64 current_position;
  gint64 new_position;

  GTD_ENTRY;

  priv = gtd_task_list_view_get_instance_private (self);
  drop = gtk_drop_target_get_drop (drop_target);
  drag = gdk_drop_get_drag (drop);

  if (!drag)
    {
      g_info ("Only dragging task rows is supported");
      GTD_RETURN (FALSE);
    }

  unset_previously_highlighted_row (self);

  source_task = g_value_get_object (value);

  /*
   * When the drag operation began, the source row was hidden. Now is the time
   * to show it again.
   */
  row = g_hash_table_lookup (priv->task_to_row, source_task);
  gtk_widget_show (row);

  drop_row = get_drop_row_at_y (self, y);
  hovered_row = task_row_from_row (drop_row);
  hovered_task = gtd_task_row_get_task (hovered_row);
  new_parent_task = gtd_task_row_get_dnd_drop_task (hovered_row);

  g_assert (source_task != NULL);
  g_assert (source_task != new_parent_task);

  if (new_parent_task)
    {
      /* Forbid adding the parent task as a subtask */
      if (gtd_task_is_subtask (source_task, new_parent_task))
        {
          gdk_drop_finish (drop, 0);
          GTD_RETURN (FALSE);
        }

      GTD_TRACE_MSG ("Making '%s' (%s) subtask of '%s' (%s)",
                     gtd_task_get_title (source_task),
                     gtd_object_get_uid (GTD_OBJECT (source_task)),
                     gtd_task_get_title (new_parent_task),
                     gtd_object_get_uid (GTD_OBJECT (new_parent_task)));

      gtd_task_add_subtask (new_parent_task, source_task);
    }
  else
    {
      GtdTask *current_parent_task = gtd_task_get_parent (source_task);
      if (current_parent_task)
        gtd_task_remove_subtask (current_parent_task, source_task);
    }

  /*
   * FIXME: via DnD, we only support moving the task to below another
   * task, thus the "+ 1"
   */
  new_position = gtd_task_get_position (hovered_task) + 1;
  current_position = gtd_task_get_position (source_task);

  if (new_position != current_position)
    gtd_task_list_move_task_to_position (GTD_TASK_LIST (priv->model), source_task, new_position);

  /* Finally, save the task */
  provider = gtd_task_list_get_provider (gtd_task_get_list (source_task));
  gtd_provider_update_task (provider, source_task);

  check_dnd_scroll (self, TRUE, -1);
  gdk_drop_finish (drop, GDK_ACTION_MOVE);

  GTD_RETURN (TRUE);
}


/*
 * GObject overrides
 */

static void
gtd_task_list_view_finalize (GObject *object)
{
  GtdTaskListViewPrivate *priv = GTD_TASK_LIST_VIEW (object)->priv;

  g_clear_handle_id (&priv->scroll_to_bottom_handler_id, g_source_remove);
  g_clear_pointer (&priv->task_to_row, g_hash_table_destroy);
  g_clear_pointer (&priv->default_date, g_date_time_unref);
  g_clear_object (&priv->renderer);
  g_clear_object (&priv->model);

  G_OBJECT_CLASS (gtd_task_list_view_parent_class)->finalize (object);
}

static void
gtd_task_list_view_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  GtdTaskListView *self = GTD_TASK_LIST_VIEW (object);

  switch (prop_id)
    {
    case PROP_COLOR:
      g_value_set_boxed (value, self->priv->color);
      break;

    case PROP_HANDLE_SUBTASKS:
      g_value_set_boolean (value, self->priv->handle_subtasks);
      break;

    case PROP_SHOW_DUE_DATE:
      g_value_set_boolean (value, self->priv->show_due_date);
      break;

    case PROP_SHOW_LIST_NAME:
      g_value_set_boolean (value, self->priv->show_list_name);
      break;

    case PROP_SHOW_NEW_TASK_ROW:
      g_value_set_boolean (value, gtk_widget_get_visible (GTK_WIDGET (self->priv->new_task_row)));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_task_list_view_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  GtdTaskListView *self = GTD_TASK_LIST_VIEW (object);

  switch (prop_id)
    {
    case PROP_COLOR:
      gtd_task_list_view_set_color (self, g_value_get_boxed (value));
      break;

    case PROP_HANDLE_SUBTASKS:
      gtd_task_list_view_set_handle_subtasks (self, g_value_get_boolean (value));
      break;

    case PROP_SHOW_DUE_DATE:
      gtd_task_list_view_set_show_due_date (self, g_value_get_boolean (value));
      break;

    case PROP_SHOW_LIST_NAME:
      gtd_task_list_view_set_show_list_name (self, g_value_get_boolean (value));
      break;

    case PROP_SHOW_NEW_TASK_ROW:
      gtd_task_list_view_set_show_new_task_row (self, g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_task_list_view_constructed (GObject *object)
{
  GtdTaskListView *self = GTD_TASK_LIST_VIEW (object);

  G_OBJECT_CLASS (gtd_task_list_view_parent_class)->constructed (object);

  /* action_group */
  self->priv->action_group = G_ACTION_GROUP (g_simple_action_group_new ());

  g_action_map_add_action_entries (G_ACTION_MAP (self->priv->action_group),
                                   gtd_task_list_view_entries,
                                   G_N_ELEMENTS (gtd_task_list_view_entries),
                                   object);

  /* css provider */
  self->priv->color_provider = gtk_css_provider_new ();

  gtk_style_context_add_provider (gtk_widget_get_style_context (GTK_WIDGET (self)),
                                  GTK_STYLE_PROVIDER (self->priv->color_provider),
                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 2);
}


/*
 * GtkWidget overrides
 */

static void
gtd_task_list_view_map (GtkWidget *widget)
{
  GtdTaskListViewPrivate *priv;
  GtkRoot *root;

  GTK_WIDGET_CLASS (gtd_task_list_view_parent_class)->map (widget);

  priv = GTD_TASK_LIST_VIEW (widget)->priv;
  root = gtk_widget_get_root (widget);

  /* Clear previously added "list" actions */
  gtk_widget_insert_action_group (GTK_WIDGET (root), "list", NULL);

  /* Add this instance's action group */
  gtk_widget_insert_action_group (GTK_WIDGET (root), "list", priv->action_group);
}

static void
gtd_task_list_view_class_init (GtdTaskListViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gtd_task_list_view_finalize;
  object_class->constructed = gtd_task_list_view_constructed;
  object_class->get_property = gtd_task_list_view_get_property;
  object_class->set_property = gtd_task_list_view_set_property;

  widget_class->map = gtd_task_list_view_map;

  g_type_ensure (GTD_TYPE_EDIT_PANE);
  g_type_ensure (GTD_TYPE_NEW_TASK_ROW);
  g_type_ensure (GTD_TYPE_TASK_ROW);
  g_type_ensure (GTD_TYPE_DND_ROW);
  g_type_ensure (GTD_TYPE_EMPTY_LIST_WIDGET);

  /**
   * GtdTaskListView::color:
   *
   * The custom color of this list. If there is a custom color set,
   * the tasklist's color is ignored.
   */
  g_object_class_install_property (
        object_class,
        PROP_COLOR,
        g_param_spec_boxed ("color",
                            "Color of the task list view",
                            "The custom color of this task list view",
                            GDK_TYPE_RGBA,
                            G_PARAM_READWRITE));

  /**
   * GtdTaskListView::handle-subtasks:
   *
   * Whether the list is able to handle subtasks.
   */
  g_object_class_install_property (
        object_class,
        PROP_HANDLE_SUBTASKS,
        g_param_spec_boolean ("handle-subtasks",
                              "Whether it handles subtasks",
                              "Whether the list handles subtasks, or not",
                              TRUE,
                              G_PARAM_READWRITE));

  /**
   * GtdTaskListView::show-new-task-row:
   *
   * Whether the list shows the "New Task" row or not.
   */
  g_object_class_install_property (
        object_class,
        PROP_SHOW_NEW_TASK_ROW,
        g_param_spec_boolean ("show-new-task-row",
                              "Whether it shows the New Task row",
                              "Whether the list shows the New Task row, or not",
                              TRUE,
                              G_PARAM_READWRITE));

  /**
   * GtdTaskListView::show-list-name:
   *
   * Whether the task rows should show the list name.
   */
  g_object_class_install_property (
        object_class,
        PROP_SHOW_LIST_NAME,
        g_param_spec_boolean ("show-list-name",
                              "Whether task rows show the list name",
                              "Whether task rows show the list name at the end of the row",
                              FALSE,
                              G_PARAM_READWRITE));

  /**
   * GtdTaskListView::show-due-date:
   *
   * Whether due dates of the tasks are shown.
   */
  g_object_class_install_property (
        object_class,
        PROP_SHOW_DUE_DATE,
        g_param_spec_boolean ("show-due-date",
                              "Whether due dates are shown",
                              "Whether due dates of the tasks are visible or not",
                              TRUE,
                              G_PARAM_READWRITE));

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/todo/ui/task-list-view/gtd-task-list-view.ui");

  gtk_widget_class_bind_template_child_private (widget_class, GtdTaskListView, due_date_sizegroup);
  gtk_widget_class_bind_template_child_private (widget_class, GtdTaskListView, listbox);
  gtk_widget_class_bind_template_child_private (widget_class, GtdTaskListView, new_task_row);
  gtk_widget_class_bind_template_child_private (widget_class, GtdTaskListView, tasklist_name_sizegroup);
  gtk_widget_class_bind_template_child_private (widget_class, GtdTaskListView, scrolled_window);
  gtk_widget_class_bind_template_child_private (widget_class, GtdTaskListView, stack);

  gtk_widget_class_bind_template_callback (widget_class, on_listbox_row_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_new_task_row_entered_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_new_task_row_exited_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_task_row_entered_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_task_row_exited_cb);

  gtk_widget_class_set_css_name (widget_class, "tasklistview");
}

static void
gtd_task_list_view_init (GtdTaskListView *self)
{
  GtdTaskListViewPrivate *priv;
  GtkDropTarget *target;

  priv = gtd_task_list_view_get_instance_private (self);

  self->priv = priv;

  priv->task_list_selector_behavior = GTD_TASK_LIST_SELECTOR_BEHAVIOR_AUTOMATIC;
  priv->task_to_row = g_hash_table_new (NULL, NULL);

  priv->can_toggle = TRUE;
  priv->handle_subtasks = TRUE;
  priv->show_due_date = TRUE;
  priv->show_due_date = TRUE;

  gtk_widget_init_template (GTK_WIDGET (self));

  target = gtk_drop_target_new (GTD_TYPE_TASK, GDK_ACTION_MOVE);
  gtk_drop_target_set_preload (target, TRUE);
  g_signal_connect (target, "drop", G_CALLBACK (on_drop_target_drag_drop_cb), self);
  g_signal_connect (target, "leave", G_CALLBACK (on_drop_target_drag_leave_cb), self);
  g_signal_connect (target, "motion", G_CALLBACK (on_drop_target_drag_motion_cb), self);

  gtk_widget_add_controller (GTK_WIDGET (priv->listbox), GTK_EVENT_CONTROLLER (target));

  priv->renderer = gtd_markdown_renderer_new ();
}

/**
 * gtd_task_list_view_new:
 *
 * Creates a new #GtdTaskListView
 *
 * Returns: (transfer full): a newly allocated #GtdTaskListView
 */
GtkWidget*
gtd_task_list_view_new (void)
{
  return g_object_new (GTD_TYPE_TASK_LIST_VIEW, NULL);
}

/**
 * gtd_task_list_view_get_show_new_task_row:
 * @view: a #GtdTaskListView
 *
 * Gets whether @view shows the new task row or not.
 *
 * Returns: %TRUE if @view is shows the new task row, %FALSE otherwise
 */
gboolean
gtd_task_list_view_get_show_new_task_row (GtdTaskListView *self)
{
  g_return_val_if_fail (GTD_IS_TASK_LIST_VIEW (self), FALSE);

  return gtk_widget_get_visible (GTK_WIDGET (self->priv->new_task_row));
}

/**
 * gtd_task_list_view_set_show_new_task_row:
 * @view: a #GtdTaskListView
 *
 * Sets the #GtdTaskListView:show-new-task-mode property of @view.
 */
void
gtd_task_list_view_set_show_new_task_row (GtdTaskListView *view,
                                          gboolean         show_new_task_row)
{
  g_return_if_fail (GTD_IS_TASK_LIST_VIEW (view));

  gtk_widget_set_visible (GTK_WIDGET (view->priv->new_task_row), show_new_task_row);
  g_object_notify (G_OBJECT (view), "show-new-task-row");
}

/**
 * gtd_task_list_view_get_model:
 * @view: a #GtdTaskListView
 *
 * Retrieves the #GtdTaskList from @view, or %NULL if none was set.
 *
 * Returns: (transfer none): the #GListModel of @view, or %NULL is
 * none was set.
 */
GListModel*
gtd_task_list_view_get_model (GtdTaskListView *view)
{
  g_return_val_if_fail (GTD_IS_TASK_LIST_VIEW (view), NULL);

  return view->priv->model;
}

/**
 * gtd_task_list_view_set_model:
 * @view: a #GtdTaskListView
 * @model: a #GListModel
 *
 * Sets the internal #GListModel of @view. The model must have
 * its element GType as @GtdTask.
 */
void
gtd_task_list_view_set_model (GtdTaskListView *view,
                              GListModel      *model)
{
  GtdTaskListViewPrivate *priv;
  g_autoptr (GdkRGBA) color = NULL;
  g_autofree gchar *parsed_css = NULL;
  g_autofree gchar *color_str = NULL;
  GtdTaskList *list;

  g_return_if_fail (GTD_IS_TASK_LIST_VIEW (view));
  g_return_if_fail (G_IS_LIST_MODEL (model));
  g_return_if_fail (g_list_model_get_item_type (model) == GTD_TYPE_TASK);

  priv = gtd_task_list_view_get_instance_private (view);

  if (!g_set_object (&priv->model, model))
    return;

  gtk_list_box_bind_model (priv->listbox,
                           model,
                           create_row_for_task_cb,
                           view,
                           NULL);

  schedule_scroll_to_bottom (view);

  if (priv->task_list_selector_behavior == GTD_TASK_LIST_SELECTOR_BEHAVIOR_AUTOMATIC)
    gtd_new_task_row_set_show_list_selector (GTD_NEW_TASK_ROW (priv->new_task_row), !GTD_IS_TASK_LIST (model));

  if (!GTD_IS_TASK_LIST (model))
    return;

  list = GTD_TASK_LIST (model);

  g_debug ("%p: Setting task list to '%s'", view, gtd_task_list_get_name (list));

  /* Add the color to provider */
  color = gtd_task_list_get_color (list);
  color_str = gdk_rgba_to_string (color);
  parsed_css = g_strdup_printf (COLOR_TEMPLATE, color_str);

  //gtk_css_provider_load_from_data (priv->color_provider, parsed_css, -1);

  update_font_color (view);
}

/**
 * gtd_task_list_view_get_show_list_name:
 * @view: a #GtdTaskListView
 *
 * Whether @view shows the tasks' list names.
 *
 * Returns: %TRUE if @view show the tasks' list names, %FALSE otherwise
 */
gboolean
gtd_task_list_view_get_show_list_name (GtdTaskListView *view)
{
  g_return_val_if_fail (GTD_IS_TASK_LIST_VIEW (view), FALSE);

  return view->priv->show_list_name;
}

/**
 * gtd_task_list_view_set_show_list_name:
 * @view: a #GtdTaskListView
 * @show_list_name: %TRUE to show list names, %FALSE to hide it
 *
 * Whether @view should should it's tasks' list name.
 */
void
gtd_task_list_view_set_show_list_name (GtdTaskListView *view,
                                       gboolean         show_list_name)
{
  g_return_if_fail (GTD_IS_TASK_LIST_VIEW (view));

  if (view->priv->show_list_name != show_list_name)
    {
      GList *children;
      GList *l;

      view->priv->show_list_name = show_list_name;

      /* update current children */
      children = gtk_container_get_children (GTK_CONTAINER (view->priv->listbox));

      for (l = children; l != NULL; l = l->next)
        {
          if (!GTD_IS_TASK_ROW (l->data))
            continue;

          gtd_task_row_set_list_name_visible (l->data, show_list_name);
        }

      g_list_free (children);

      g_object_notify (G_OBJECT (view), "show-list-name");
    }
}

/**
 * gtd_task_list_view_get_show_due_date:
 * @self: a #GtdTaskListView
 *
 * Retrieves whether the @self is showing the due dates of the tasks
 * or not.
 *
 * Returns: %TRUE if due dates are visible, %FALSE otherwise.
 */
gboolean
gtd_task_list_view_get_show_due_date (GtdTaskListView *self)
{
  g_return_val_if_fail (GTD_IS_TASK_LIST_VIEW (self), FALSE);

  return self->priv->show_due_date;
}

/**
 * gtd_task_list_view_set_show_due_date:
 * @self: a #GtdTaskListView
 * @show_due_date: %TRUE to show due dates, %FALSE otherwise
 *
 * Sets whether @self shows the due dates of the tasks or not.
 */
void
gtd_task_list_view_set_show_due_date (GtdTaskListView *self,
                                      gboolean         show_due_date)
{
  GtdTaskListViewPrivate *priv;
  GList *children;
  GList *l;

  g_return_if_fail (GTD_IS_TASK_LIST_VIEW (self));

  priv = gtd_task_list_view_get_instance_private (self);

  if (priv->show_due_date == show_due_date)
    return;

  priv->show_due_date = show_due_date;

  children = gtk_container_get_children (GTK_CONTAINER (priv->listbox));

  for (l = children; l != NULL; l = l->next)
    {
      if (!GTD_IS_TASK_ROW (l->data))
        continue;

      gtd_task_row_set_due_date_visible (l->data, show_due_date);
    }

  g_list_free (children);

  g_object_notify (G_OBJECT (self), "show-due-date");
}

/**
 * gtd_task_list_view_set_header_func:
 * @view: a #GtdTaskListView
 * @func: (closure user_data) (scope call) (nullable): the header function
 * @user_data: data passed to @func
 *
 * Sets @func as the header function of @view. You can safely call
 * %gtk_list_box_row_set_header from within @func.
 *
 * Do not unref nor free any of the passed data.
 */
void
gtd_task_list_view_set_header_func (GtdTaskListView           *view,
                                    GtdTaskListViewHeaderFunc  func,
                                    gpointer                   user_data)
{
  GtdTaskListViewPrivate *priv;

  g_return_if_fail (GTD_IS_TASK_LIST_VIEW (view));

  priv = view->priv;

  if (func)
    {
      priv->header_func = func;
      priv->header_user_data = user_data;

      gtk_list_box_set_header_func (priv->listbox,
                                    (GtkListBoxUpdateHeaderFunc) internal_header_func,
                                    view,
                                    NULL);
    }
  else
    {
      priv->header_func = NULL;
      priv->header_user_data = NULL;

      gtk_list_box_set_header_func (priv->listbox,
                                    NULL,
                                    NULL,
                                    NULL);
    }
}

/**
 * gtd_task_list_view_get_default_date:
 * @self: a #GtdTaskListView
 *
 * Retrieves the current default date which new tasks are set to.
 *
 * Returns: (nullable): a #GDateTime, or %NULL
 */
GDateTime*
gtd_task_list_view_get_default_date (GtdTaskListView *self)
{
  GtdTaskListViewPrivate *priv;

  g_return_val_if_fail (GTD_IS_TASK_LIST_VIEW (self), NULL);

  priv = gtd_task_list_view_get_instance_private (self);

  return priv->default_date;
}

/**
 * gtd_task_list_view_set_default_date:
 * @self: a #GtdTaskListView
 * @default_date: (nullable): the default_date, or %NULL
 *
 * Sets the current default date.
 */
void
gtd_task_list_view_set_default_date   (GtdTaskListView *self,
                                       GDateTime       *default_date)
{
  GtdTaskListViewPrivate *priv;

  g_return_if_fail (GTD_IS_TASK_LIST_VIEW (self));

  priv = gtd_task_list_view_get_instance_private (self);

  if (priv->default_date == default_date)
    return;

  g_clear_pointer (&priv->default_date, g_date_time_unref);
  priv->default_date = default_date ? g_date_time_ref (default_date) : NULL;
}

/**
 * gtd_task_list_view_get_color:
 * @self: a #GtdTaskListView
 *
 * Retrieves the custom color of @self.
 *
 * Returns: (nullable): a #GdkRGBA, or %NULL if none is set.
 */
GdkRGBA*
gtd_task_list_view_get_color (GtdTaskListView *self)
{
  GtdTaskListViewPrivate *priv;

  g_return_val_if_fail (GTD_IS_TASK_LIST_VIEW (self), NULL);

  priv = gtd_task_list_view_get_instance_private (self);

  return priv->color;
}

/**
 * gtd_task_list_view_set_color:
 * @self: a #GtdTaskListView
 * @color: (nullable): a #GdkRGBA
 *
 * Sets the custom color of @self to @color. If a custom color is set,
 * the tasklist's color is ignored. Passing %NULL makes the tasklist's
 * color apply again.
 */
void
gtd_task_list_view_set_color (GtdTaskListView *self,
                              GdkRGBA         *color)
{
  GtdTaskListViewPrivate *priv;

  g_return_if_fail (GTD_IS_TASK_LIST_VIEW (self));

  priv = gtd_task_list_view_get_instance_private (self);

  if (priv->color != color ||
      (color && priv->color && !gdk_rgba_equal (color, priv->color)))
    {
      g_clear_pointer (&priv->color, gdk_rgba_free);
      priv->color = gdk_rgba_copy (color);

      on_task_list_color_changed_cb (self);

      g_object_notify (G_OBJECT (self), "color");
    }
}

/**
 * gtd_task_list_view_get_handle_subtasks:
 * @self: a #GtdTaskListView
 *
 * Retirves whether @self handle subtasks, i.e. make the rows
 * change padding depending on their depth, show an arrow button
 * to toggle subtasks, among others.
 *
 * Returns: %TRUE if @self handles subtasks, %FALSE otherwise
 */
gboolean
gtd_task_list_view_get_handle_subtasks (GtdTaskListView *self)
{
  GtdTaskListViewPrivate *priv;

  g_return_val_if_fail (GTD_IS_TASK_LIST_VIEW (self), FALSE);

  priv = gtd_task_list_view_get_instance_private (self);

  return priv->handle_subtasks;
}

/**
 * gtd_task_list_view_set_handle_subtasks:
 * @self: a #GtdTaskListView
 * @handle_subtasks: %TRUE to make @self handle subtasks, %FALSE to disable subtasks.
 *
 * If %TRUE, makes @self handle subtasks, adjust the task rows according to their
 * hierarchy level at the subtask tree and show the arrow button to toggle subtasks
 * of a given task.
 *
 * Drag and drop tasks will only work if @self handles subtasks as well.
 */
void
gtd_task_list_view_set_handle_subtasks (GtdTaskListView *self,
                                        gboolean         handle_subtasks)
{
  GtdTaskListViewPrivate *priv;

  g_return_if_fail (GTD_IS_TASK_LIST_VIEW (self));

  priv = gtd_task_list_view_get_instance_private (self);

  if (priv->handle_subtasks == handle_subtasks)
    return;

  priv->handle_subtasks = handle_subtasks;

  g_object_notify (G_OBJECT (self), "handle-subtasks");
}


GtdTaskListSelectorBehavior
gtd_task_list_view_get_task_list_selector_behavior (GtdTaskListView *self)
{
  GtdTaskListViewPrivate *priv;

  g_return_val_if_fail (GTD_IS_TASK_LIST_VIEW (self), -1);

  priv = gtd_task_list_view_get_instance_private (self);

  return priv->task_list_selector_behavior;
}

void
gtd_task_list_view_set_task_list_selector_behavior (GtdTaskListView             *self,
                                                    GtdTaskListSelectorBehavior  behavior)
{
  GtdTaskListViewPrivate *priv;

  g_return_if_fail (GTD_IS_TASK_LIST_VIEW (self));

  priv = gtd_task_list_view_get_instance_private (self);

  if (priv->task_list_selector_behavior == behavior)
    return;

  priv->task_list_selector_behavior = behavior;

  switch (behavior)
    {
    case GTD_TASK_LIST_SELECTOR_BEHAVIOR_AUTOMATIC:
      if (priv->model)
        {
          gtd_new_task_row_set_show_list_selector (GTD_NEW_TASK_ROW (priv->new_task_row),
                                                   !GTD_IS_TASK_LIST (priv->model));
        }
      break;

    case GTD_TASK_LIST_SELECTOR_BEHAVIOR_ALWAYS_SHOW:
      gtd_new_task_row_set_show_list_selector (GTD_NEW_TASK_ROW (priv->new_task_row), TRUE);
      break;

    case GTD_TASK_LIST_SELECTOR_BEHAVIOR_ALWAYS_HIDE:
      gtd_new_task_row_set_show_list_selector (GTD_NEW_TASK_ROW (priv->new_task_row), FALSE);
      break;
    }
}
