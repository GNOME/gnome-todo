/* gtd-task-list-view.c
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

#define G_LOG_DOMAIN "GtdTaskListView"

#include "gtd-debug.h"
#include "gtd-dnd-row.h"
#include "gtd-done-button.h"
#include "gtd-edit-pane.h"
#include "gtd-empty-list-widget.h"
#include "gtd-task-list-view.h"
#include "gtd-manager.h"
#include "gtd-markdown-renderer.h"
#include "gtd-new-task-row.h"
#include "gtd-notification.h"
#include "gtd-provider.h"
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
 * gtd_task_list_view_set_list (view, list);
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
  GtkWidget             *dnd_row;
  GtkListBox            *listbox;
  GtkListBoxRow         *new_task_row;
  GtkWidget             *scrolled_window;
  GtkStack              *stack;

  /* internal */
  gboolean               can_toggle;
  gboolean               show_due_date;
  gboolean               show_list_name;
  gboolean               handle_subtasks;
  GHashTable            *tasks;
  GtdTaskList           *task_list;
  GDateTime             *default_date;

  guint                  idle_handler_id;
  guint                  scroll_to_bottom_handler_id;

  /* Markup renderer*/
  GtdMarkdownRenderer   *renderer;

  /* DnD autoscroll */
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

  /* Custom filter function data */
  GtdTaskListViewFilterFunc filter_func;
  gpointer                  filter_user_data;

  /* Custom sorting function data */
  GtdTaskListViewSortFunc sort_func;
  gpointer                sort_user_data;

  GtkWidget              *active_row;
  GtkSizeGroup           *due_date_sizegroup;
  GtkSizeGroup           *tasklist_name_sizegroup;
} GtdTaskListViewPrivate;

struct _GtdTaskListView
{
  GtkOverlay          parent;

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
#define N_TASKS_BEFORE_LOAD          50


static gboolean      idle_process_items_cb                       (gpointer            data);

static void          on_clear_completed_tasks_activated_cb       (GSimpleAction      *simple,
                                                                  GVariant           *parameter,
                                                                  gpointer            user_data);

static void          on_remove_task_row_cb                       (GtdTaskRow         *row,
                                                                  GtdTaskListView    *self);

static void          on_task_list_task_added_cb                  (GtdTaskList         *list,
                                                                  GtdTask             *task,
                                                                  GtdTaskListView     *self);

static void          on_task_list_task_removed_cb                (GtdTaskListView     *view,
                                                                  GtdTask             *task);

static void          on_task_row_entered_cb                      (GtdTaskListView    *self,
                                                                  GtdTaskRow         *row);

static void          on_task_row_exited_cb                       (GtdTaskListView    *self,
                                                                  GtdTaskRow         *row);

static gboolean      scroll_to_bottom_cb                         (gpointer            data);


G_DEFINE_TYPE_WITH_PRIVATE (GtdTaskListView, gtd_task_list_view, GTK_TYPE_OVERLAY)

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

static GList*
get_tasks_from_list (GtdTaskList *list)
{
  GList *values = NULL;
  guint i;

  for (i = 0; i < g_list_model_get_n_items (G_LIST_MODEL (list)); i++)
    values = g_list_prepend (values, g_list_model_get_item (G_LIST_MODEL (list), i));

  return values;
}

static void
idle_data_free (gpointer data)
{
  GtdIdleData *idle_data = data;

  g_clear_pointer (&idle_data->added, g_ptr_array_unref);
  g_clear_pointer (&idle_data->removed, g_ptr_array_unref);
  g_clear_pointer (&idle_data, g_free);
}

static void
add_and_remove_tasks_in_idle (GtdTaskListView *self,
                              GPtrArray       *added,
                              GPtrArray       *removed)
{
  GtdTaskListViewPrivate *priv = gtd_task_list_view_get_instance_private (self);
  GtdIdleData *idle_data;

  /* If there's nothing to add or remove, don't do anything */
  if (added->len == 0 && removed->len == 0)
    return;

  idle_data = g_new0 (GtdIdleData, 1);
  idle_data->self = self;
  idle_data->added = g_ptr_array_ref (added);
  idle_data->removed = g_ptr_array_ref (removed);
  idle_data->state = GTD_IDLE_STATE_STARTED;

  /* Start loading stuff in idle */
  priv->idle_handler_id = g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
                                           idle_process_items_cb,
                                           idle_data,
                                           idle_data_free);
}

static void
set_active_row (GtdTaskListView *self,
                GtkWidget       *row)
{
  GtdTaskListViewPrivate *priv = gtd_task_list_view_get_instance_private (self);

  if (priv->active_row == row)
    return;

  if (priv->active_row)
    {
      if (GTD_IS_TASK_ROW (priv->active_row))
        gtd_task_row_set_active (GTD_TASK_ROW (priv->active_row), FALSE);
      else
        gtd_new_task_row_set_active (GTD_NEW_TASK_ROW (priv->active_row), FALSE);
    }

  priv->active_row = row;

  if (row)
    {
      if (GTD_IS_TASK_ROW (row))
        gtd_task_row_set_active (GTD_TASK_ROW (row), TRUE);
      else
        gtd_new_task_row_set_active (GTD_NEW_TASK_ROW (row), TRUE);

      gtk_widget_grab_focus (row);
    }
}

static void
iterate_subtasks (GtdTaskListView    *self,
                  GtdTask            *task,
                  IterateSubtaskFunc  func)
{
  GtdTask *aux;
  GQueue queue;

  aux = task;

  g_queue_init (&queue);

  do
    {
      GList *subtasks, *l;
      gboolean should_continue;

      subtasks = gtd_task_get_subtasks (aux);

      /* Call the passed function */
      should_continue = func (self, aux);

      if (!should_continue)
        break;

      /* Add the subtasks to the queue so we can keep iterating */
      for (l = subtasks; l != NULL; l = l->next)
        g_queue_push_tail (&queue, l->data);

      g_clear_pointer (&subtasks, g_list_free);

      aux = g_queue_pop_head (&queue);
    }
  while (aux);
}

static void
update_font_color (GtdTaskListView *view)
{
  GtdTaskListViewPrivate *priv;

  g_return_if_fail (GTD_IS_TASK_LIST_VIEW (view));

  priv = view->priv;

  if (priv->task_list)
    {
      GtkStyleContext *context;
      GdkRGBA *color;

      context = gtk_widget_get_style_context (GTK_WIDGET (view));
      color = gtd_task_list_get_color (priv->task_list);

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
}

static gboolean
ask_subtask_removal_warning (GtdTaskListView *self)
{
  GtkWidget *dialog, *button;
  gint response;

  dialog = gtk_message_dialog_new (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (self))),
                                   GTK_DIALOG_USE_HEADER_BAR | GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                   GTK_MESSAGE_QUESTION,
                                   GTK_BUTTONS_NONE,
                                   _("Removing this task will also remove its subtasks. Remove anyway?"));

  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                            _("Once removed, the tasks cannot be recovered."));

  gtk_dialog_add_buttons (GTK_DIALOG (dialog),
                          _("Cancel"),
                          GTK_RESPONSE_CANCEL,
                          _("Remove"),
                          GTK_RESPONSE_ACCEPT,
                          NULL);

  /* Make the Remove button visually destructive */
  button = gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);
  gtk_style_context_add_class (gtk_widget_get_style_context (button), "destructive-action");

  /* Run the dialog */
  response = gtk_dialog_run (GTK_DIALOG (dialog));

  gtk_widget_destroy (dialog);

  return response == GTK_RESPONSE_ACCEPT;
}

static void
add_task_row (GtdTaskListView *self,
              GtdTask         *task,
              gboolean         animated)
{
  GtdTaskListViewPrivate *priv = self->priv;
  GtkWidget *new_row;

  new_row = gtd_task_row_new (task, priv->renderer);

  g_object_bind_property (self,
                          "handle-subtasks",
                          new_row,
                          "handle-subtasks",
                          G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

  gtd_task_row_set_list_name_visible (GTD_TASK_ROW (new_row), priv->show_list_name);
  gtd_task_row_set_due_date_visible (GTD_TASK_ROW (new_row), priv->show_due_date);

  g_signal_connect_swapped (new_row,
                            "enter",
                            G_CALLBACK (on_task_row_entered_cb),
                            self);

  g_signal_connect_swapped (new_row,
                            "exit",
                            G_CALLBACK (on_task_row_exited_cb),
                            self);

  g_signal_connect (new_row,
                    "remove-task",
                    G_CALLBACK (on_remove_task_row_cb),
                    self);

  gtk_list_box_insert (priv->listbox, new_row, 0);

  /*
   * Setup a sizegroup to let all the tasklist labels have
   * the same width.
   */
  gtd_task_row_set_sizegroups (GTD_TASK_ROW (new_row),
                               priv->tasklist_name_sizegroup,
                               priv->due_date_sizegroup);

  gtd_task_row_reveal (GTD_TASK_ROW (new_row), animated);
}

static GtdTaskRow*
get_row_for_task (GtdTaskListView *self,
                  GtdTask         *task)
{
  g_autoptr (GList) children = NULL;
  GtdTaskListViewPrivate *priv;
  GList *l;

  priv = gtd_task_list_view_get_instance_private (self);
  children = gtk_container_get_children (GTK_CONTAINER (priv->listbox));

  for (l = children; l != NULL; l = l->next)
    {
      if (!GTD_IS_TASK_ROW (l->data))
        continue;

      if (gtd_task_row_get_task (l->data) == task)
        return l->data;
    }

  return NULL;
}

static void
destroy_task_row (GtdTaskListView *self,
                  GtdTaskRow      *row)
{
  g_signal_handlers_disconnect_by_func (row, on_task_row_entered_cb, self);
  g_signal_handlers_disconnect_by_func (row, on_task_row_exited_cb, self);

  if (GTK_WIDGET (row) == self->priv->active_row)
    set_active_row (self, NULL);

  gtd_task_row_destroy (row);
}

static void
remove_task (GtdTaskListView *self,
             GtdTask         *task)
{
  GtdTaskListViewPrivate *priv = gtd_task_list_view_get_instance_private (self);
  GtdTaskRow *row;

  row = get_row_for_task (self, task);

  /* We must not try to remove a task that is not added */
  g_assert (g_hash_table_contains (priv->tasks, task));
  g_hash_table_remove (priv->tasks, task);

  if (row)
    destroy_task_row (self, row);
}

static void
remove_task_row (GtdTaskListView *view,
                 GtdTask         *task)
{
  GtdTaskRow *row;

  g_assert (GTD_IS_TASK_LIST_VIEW (view));
  g_assert (GTD_IS_TASK (task));

  row = get_row_for_task (view, task);

  if (row)
    destroy_task_row (view, row);
}

static void
add_task (GtdTaskListView *self,
          GtdTask         *task,
          gboolean         animated)
{
  GtdTaskListViewPrivate *priv = gtd_task_list_view_get_instance_private (self);

  GTD_ENTRY;

  g_return_if_fail (GTD_IS_TASK_LIST_VIEW (self));
  g_return_if_fail (GTD_IS_TASK (task));

  /* We must not try to add a task that is already added */
  g_assert (!g_hash_table_contains (priv->tasks, task));
  g_hash_table_add (priv->tasks, task);

  add_task_row (self, task, animated);

  GTD_EXIT;
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

static gboolean
scroll_to_bottom_cb (gpointer data)
{
  GtdTaskListViewPrivate *priv;
  GtkWindow *window;
  GtkWidget *widget;

  priv = gtd_task_list_view_get_instance_private (data);
  widget = GTK_WIDGET (data);
  window = GTK_WINDOW (gtk_widget_get_toplevel (widget));

  g_assert (window != NULL);

  priv->scroll_to_bottom_handler_id = 0;

  /*
   * Only focus the new task row if the current list is visible,
   * and the focused widget isn't inside this list view.
   */
  if (gtk_widget_get_visible (widget) &&
      gtk_widget_get_child_visible (widget) &&
      gtk_widget_get_mapped (widget) &&
      !gtk_widget_is_ancestor (gtk_window_get_focus (window), widget))
    {
      gboolean ignored;

      gtk_widget_grab_focus (GTK_WIDGET (priv->new_task_row));
      g_signal_emit_by_name (priv->scrolled_window, "scroll-child", GTK_SCROLL_END, FALSE, &ignored);
    }

  return G_SOURCE_REMOVE;
}

static gboolean
idle_process_items_cb (gpointer data)
{
  GtdTaskListViewPrivate *priv;
  GtdIdleData *idle_data;
  guint32 n_removed_items;
  guint32 n_added_items;
  guint i;

  idle_data = data;
  priv = gtd_task_list_view_get_instance_private (idle_data->self);

  g_assert (idle_data->state == GTD_IDLE_STATE_STARTED || idle_data->state == GTD_IDLE_STATE_LOADING);

  n_removed_items = idle_data->removed->len;
  n_added_items = idle_data->added->len;

  /* Stop if we finished loading */
  if (idle_data->current_item == n_removed_items + n_added_items)
    {
      GTD_TRACE_MSG ("Finished adding %u and removing %u items", n_added_items, n_removed_items);

      priv->idle_handler_id = 0;
      idle_data->state = GTD_IDLE_STATE_FINISHED;

      /* Show the list again */
      gtk_widget_show (GTK_WIDGET (priv->listbox));
      gtk_stack_set_visible_child_name (priv->stack, "listbox");

      /* Scroll to the bottom */
      schedule_scroll_to_bottom (idle_data->self);

      return G_SOURCE_REMOVE;
    }

  GTD_TRACE_MSG ("Processing items (removed: %u, added: %u, current: %u)",
                 n_removed_items,
                 n_added_items,
                 idle_data->current_item);

  idle_data->state = GTD_IDLE_STATE_LOADING;

  /*
   * A performance trick we do is to hide the listbox until it's completely
   * finished. Only do that if we're going to idle more than 30 tasks. This
   * number is arbitrary
   */
  if (n_added_items >= N_TASKS_BEFORE_LOAD)
    {
      gtk_widget_hide (GTK_WIDGET (priv->listbox));
      gtk_stack_set_visible_child_name (priv->stack, "loading");
    }

  /*
   * Here we take the following approach:
   *  1. Remove all the rows marked for removal at once.
   *  2. Add the remaining rows in chunks of BATCH_SIZE
   */
  if (idle_data->current_item < n_removed_items)
    {
      for (i = 0; i < n_removed_items; i++)
        remove_task (idle_data->self, g_ptr_array_index (idle_data->removed, i));

      idle_data->current_item = n_removed_items;
    }

  /* No tasks to add, run the mainloop one more time to finish loading */
  if (n_added_items == 0)
    return G_SOURCE_CONTINUE;

  /*
   * For performance reasons, only animate task reveal when adding less
   * than a batch of items.
   */
  add_task (idle_data->self,
            g_ptr_array_index (idle_data->added, idle_data->current_item - n_removed_items),
            n_added_items < N_TASKS_BEFORE_LOAD);

  /* Next item */
  idle_data->current_item += 1;

  return G_SOURCE_CONTINUE;
}

static gboolean
undo_remove_task_cb (GtdTaskListView *self,
                     GtdTask         *task)
{
  /* Tasks are not loading anymore */
  gtd_object_pop_loading (GTD_OBJECT (task));

  on_task_list_task_added_cb (NULL, task, self);

  return TRUE;
}

static inline gboolean
real_remove_task_cb (GtdTaskListView *self,
                     GtdTask         *task)
{
  GtdTaskListViewPrivate *priv = gtd_task_list_view_get_instance_private (self);

  gtd_provider_remove_task (gtd_task_get_provider (task), task);

  /* Remove from the internal list */
  g_hash_table_remove (priv->tasks, task);

  GTD_TRACE_MSG ("Removing task %p from list", task);

  return TRUE;
}

static void
on_clear_completed_tasks_activated_cb (GSimpleAction *simple,
                                       GVariant      *parameter,
                                       gpointer       user_data)
{
  GtdTaskListView *view;
  g_autoptr (GList) tasks = NULL;
  g_autoptr (GList) l = NULL;

  view = GTD_TASK_LIST_VIEW (user_data);
  tasks = gtd_task_list_view_get_list (view);

  for (l = tasks; l; l = l->next)
    {
      GtdTask *task = l->data;

      if (!gtd_task_get_complete (task))
        continue;

      if (gtd_task_get_parent (task))
        gtd_task_remove_subtask (gtd_task_get_parent (task), task);

      /* Remove the subtasks recursively */
      iterate_subtasks (view, task, real_remove_task_cb);
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
  iterate_subtasks (data->view, data->task, real_remove_task_cb);

  g_clear_pointer (&data, g_free);
}

static void
on_undo_remove_task_action_cb (GtdNotification *notification,
                               gpointer         user_data)
{
  RemoveTaskData *data = user_data;

  /* Save the subtasks recursively */
  iterate_subtasks (data->view, data->task, undo_remove_task_cb);

  g_free (data);
}

static inline gboolean
remove_task_rows_from_list_view_cb (GtdTaskListView *self,
                                    GtdTask         *task)
{
  GtdTaskListViewPrivate *priv = gtd_task_list_view_get_instance_private (self);

  /* Task is in loading state until it's either readded, or effectively removed */
  gtd_object_push_loading (GTD_OBJECT (task));

  /* Remove from the view, but not from the list */
  on_task_list_task_removed_cb (self, task);

  /* Remove from the internal list */
  g_hash_table_remove (priv->tasks, task);

  GTD_TRACE_MSG ("Removing task %p from list", task);

  return TRUE;
}

static void
on_remove_task_row_cb (GtdTaskRow      *row,
                       GtdTaskListView *self)
{
  GtdTaskListViewPrivate *priv;
  GtdNotification *notification;
  RemoveTaskData *data;
  GtdWindow *window;
  GtdTask *task;
  GList *subtasks;
  gchar *text;

  task = gtd_task_row_get_task (row);
  subtasks = gtd_task_get_subtasks (task);

  /*
   * If the task has subtasks, ask the user if he/she really wants to
   * remove the subtasks.
   */
  if (subtasks)
    {
      gboolean should_remove_task;

      should_remove_task = ask_subtask_removal_warning (self);

      /* The user canceled the operation, do nothing */
      if (!should_remove_task)
        goto out;
    }

  priv = self->priv;
  text = g_strdup_printf (_("Task <b>%s</b> removed"), gtd_task_get_title (task));
  window = GTD_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (self)));

  data = g_new0 (RemoveTaskData, 1);
  data->view = self;
  data->task = task;

  /* Always remove tasks and subtasks */
  iterate_subtasks (self, task, remove_task_rows_from_list_view_cb);

  /*
   * Reset the DnD row, to avoid getting into an inconsistent state where
   * the DnD row points to a row that is not present anymore.
   */
  gtd_dnd_row_set_row_above (GTD_DND_ROW (priv->dnd_row), NULL);

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

  g_clear_pointer (&text, g_free);

out:
  g_clear_pointer (&subtasks, g_list_free);
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

      color = gtd_task_list_get_color (GTD_TASK_LIST (priv->task_list));
      color_str = gdk_rgba_to_string (color);

      gdk_rgba_free (color);
    }

  parsed_css = g_strdup_printf (COLOR_TEMPLATE, color_str);

  gtk_css_provider_load_from_data (priv->color_provider, parsed_css, -1);

  update_font_color (self);

  g_free (color_str);
}

static gboolean
on_can_toggle_show_completed_cb (GtdTaskListView *view)
{
  view->priv->can_toggle = TRUE;
  return G_SOURCE_REMOVE;
}

static void
on_done_button_clicked_cb (GtkButton *button,
                           gpointer   user_data)
{
  GtdTaskListView *view = GTD_TASK_LIST_VIEW (user_data);

  g_return_if_fail (GTD_IS_TASK_LIST_VIEW (view));

  if (!view->priv->can_toggle)
    return;

  /*
   * The can_toggle field is needed because the user
   * can click mindlessly the Done button, while the row
   * animations are not finished. While the animation is
   * running, we ignore other clicks.
   */
  view->priv->can_toggle = FALSE;

  g_timeout_add (205, (GSourceFunc) on_can_toggle_show_completed_cb, user_data);
}

static void
on_task_row_entered_cb (GtdTaskListView *self,
                        GtdTaskRow      *row)
{
  set_active_row (self, GTK_WIDGET (row));
}

static void
on_task_row_exited_cb (GtdTaskListView *self,
                       GtdTaskRow      *row)
{
  GtdTaskListViewPrivate *priv = self->priv;

  if (GTK_WIDGET (row) == priv->active_row &&
      priv->active_row != GTK_WIDGET (priv->new_task_row))
    {
      set_active_row (self, NULL);
    }
}

static void
on_task_list_task_added_cb (GtdTaskList     *list,
                            GtdTask         *task,
                            GtdTaskListView *self)
{
  GtdTaskListViewPrivate *priv = gtd_task_list_view_get_instance_private (self);

  GTD_ENTRY;

  add_task (self, task, TRUE);

  /* Also add to the list of current tasks */
  g_hash_table_add (priv->tasks, task);

  GTD_TRACE_MSG ("Adding task %p to list", task);

  GTD_EXIT;
}

static void
on_task_list_task_removed_cb (GtdTaskListView *view,
                              GtdTask         *task)
{
  remove_task_row (view, task);
}

static void
on_listbox_row_activated_cb (GtkListBox      *listbox,
                             GtkListBox      *row,
                             GtdTaskListView *self)
{
  GTD_ENTRY;

  if (!GTD_IS_TASK_ROW (row))
    GTD_RETURN ();

  /* Toggle the row */
  if (gtd_task_row_get_active (GTD_TASK_ROW (row)))
    set_active_row (self, NULL);
  else
    set_active_row (self, GTK_WIDGET (row));

  GTD_EXIT;
}

static gint
sort_tasks_gptrarray_cb (gconstpointer a,
                         gconstpointer b)
{
  GtdTask *task_a = (*((GtdTask**) a));
  GtdTask *task_b = (*((GtdTask**) b));

  return gtd_task_compare (task_a, task_b);
}

/*
 * Default sorting functions
 */

static gint
compare_task_rows (GtkListBoxRow *row1,
                   GtkListBoxRow *row2)
{
  if (GTD_IS_NEW_TASK_ROW (row1))
    return 1;

  if (GTD_IS_NEW_TASK_ROW (row2))
    return -1;

  return gtd_task_compare (gtd_task_row_get_task (GTD_TASK_ROW (row1)),
                           gtd_task_row_get_task (GTD_TASK_ROW (row2)));
}

static gint
compare_dnd_rows (GtkListBoxRow *row1,
                  GtkListBoxRow *row2)
{
  GtkListBoxRow *row_above, *current_row;
  gboolean reverse;

  if (GTD_IS_DND_ROW (row1))
    {
      row_above = gtd_dnd_row_get_row_above (GTD_DND_ROW (row1));
      current_row = row2;
      reverse = FALSE;
    }
  else
    {
      row_above = gtd_dnd_row_get_row_above (GTD_DND_ROW (row2));
      current_row = row1;
      reverse = TRUE;
    }

  if (!row_above)
    return reverse ? 1 : -1;

  if (current_row == row_above)
    return reverse ? -1 : 1;

  return compare_task_rows (current_row, row_above) * (reverse ? 1 : -1);
}

static gint
default_sort_func (GtkListBoxRow *row1,
                   GtkListBoxRow *row2,
                   gpointer       user_data)
{
  /* Automagically manage the DnD row */
  if (GTD_IS_DND_ROW (row1) || GTD_IS_DND_ROW (row2))
    return compare_dnd_rows (row1, row2);

  return compare_task_rows (row1, row2);
}


/*
 * Custom filter function
 */

static gboolean
custom_filter_func (GtkListBoxRow   *row,
                    GtdTaskListView *self)
{
  GtdTaskListViewPrivate *priv;
  GtdTask *task;

  if (GTD_IS_NEW_TASK_ROW (row) || GTD_IS_DND_ROW (row))
    return gtk_widget_get_visible (GTK_WIDGET (row));

  priv = gtd_task_list_view_get_instance_private (self);
  task = gtd_task_row_get_task (GTD_TASK_ROW (row));

  return priv->filter_func (task, priv->filter_user_data);;
}


/*
 * Custom sorting functions
 */

static void
internal_header_func (GtkListBoxRow   *row,
                      GtkListBoxRow   *before,
                      GtdTaskListView *view)
{
  GtdTask *row_task;
  GtdTask *before_task;

  if (!view->priv->header_func || row == view->priv->new_task_row)
    return;

  row_task = before_task = NULL;

  if (row && GTD_IS_TASK_ROW (row))
    row_task = gtd_task_row_get_task (GTD_TASK_ROW (row));

  if (before && GTD_IS_TASK_ROW (row))
    before_task = gtd_task_row_get_task (GTD_TASK_ROW (before));

  view->priv->header_func (GTK_LIST_BOX_ROW (row),
                           row_task,
                           GTK_LIST_BOX_ROW (before),
                           before_task,
                           view->priv->header_user_data);
}

static gint
internal_compare_task_rows (GtdTaskListView *self,
                            GtkListBoxRow   *row1,
                            GtkListBoxRow   *row2)
{
  GtdTask *row1_task;
  GtdTask *row2_task;

  if (row1 == self->priv->new_task_row)
    return 1;
  else if (row2 == self->priv->new_task_row)
    return -1;

  row1_task = row2_task = NULL;

  if (row1)
    row1_task = gtd_task_row_get_task (GTD_TASK_ROW (row1));

  if (row2)
    row2_task = gtd_task_row_get_task (GTD_TASK_ROW (row2));

  return self->priv->sort_func (GTK_LIST_BOX_ROW (row1),
                                row1_task,
                                GTK_LIST_BOX_ROW (row2),
                                row2_task,
                                self->priv->header_user_data);
}

static gint
internal_compare_dnd_rows (GtdTaskListView *self,
                           GtkListBoxRow   *row1,
                           GtkListBoxRow   *row2)
{
  GtkListBoxRow *row_above, *current_row;
  gboolean reverse;

  if (GTD_IS_DND_ROW (row1))
    {
      row_above = gtd_dnd_row_get_row_above (GTD_DND_ROW (row1));
      current_row = row2;
      reverse = FALSE;
    }
  else
    {
      row_above = gtd_dnd_row_get_row_above (GTD_DND_ROW (row2));
      current_row = row1;
      reverse = TRUE;
    }

  if (!row_above)
    return reverse ? 1 : -1;

  if (current_row == row_above)
    return reverse ? -1 : 1;

  return internal_compare_task_rows (self, current_row, row_above) * (reverse ? 1 : -1);
}

static gint
custom_sort_func (GtkListBoxRow   *a,
                  GtkListBoxRow   *b,
                  GtdTaskListView *view)
{
  if (!view->priv->sort_func)
    return 0;

  if (GTD_IS_DND_ROW (a) || GTD_IS_DND_ROW (b))
    return internal_compare_dnd_rows (view, a, b);

  return internal_compare_task_rows (view, a, b);
}


/*
 * Drag n' Drop functions
 */

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
listbox_drag_leave (GtkListBox      *listbox,
                    GdkDrop         *drop,
                    GtdTaskListView *self)
{
  GtdTaskListViewPrivate *priv;

  priv = gtd_task_list_view_get_instance_private (self);

  gtk_widget_set_visible (priv->dnd_row, FALSE);

  check_dnd_scroll (self, TRUE, -1);

  gtk_list_box_invalidate_sort (listbox);
}

static gboolean
listbox_drag_motion (GtkListBox      *listbox,
                     GdkDrop         *drop,
                     gint             x,
                     gint             y,
                     GtdTaskListView *self)
{
  GtdTaskListViewPrivate *priv;
  GtkListBoxRow *hovered_row;
  GtkListBoxRow *source_row;
  GtkListBoxRow *task_row;
  GtkListBoxRow *row_above_dnd;
  GtkWidget *source_widget;
  GdkDrag *drag;
  gboolean success;
  gint row_x, row_y, row_height;

  priv = gtd_task_list_view_get_instance_private (self);
  drag = gdk_drop_get_drag (drop);

  if (!drag)
    {
      g_debug ("Only dragging task rows is supported");
      goto fail;
    }

  source_widget = gtk_drag_get_source_widget (drag);
  source_row = GTK_LIST_BOX_ROW (gtk_widget_get_ancestor (source_widget, GTK_TYPE_LIST_BOX_ROW));
  hovered_row = gtk_list_box_get_row_at_y (listbox, y);

  /* Update the x value according to the current offset */
  if (gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL)
    x += gtd_task_row_get_x_offset (GTD_TASK_ROW (source_row));
  else
    x -= gtd_task_row_get_x_offset (GTD_TASK_ROW (source_row));

  /* Make sure the DnD row always have the same height of the dragged row */
  gtk_widget_set_size_request (priv->dnd_row,
                               -1,
                               gtk_widget_get_allocated_height (GTK_WIDGET (source_row)));

  gtk_widget_queue_resize (priv->dnd_row);

  gdk_drop_status (drop, GDK_ACTION_MOVE);

  /*
   * When not hovering any row, we still have to make sure that the listbox is a valid
   * drop target. Otherwise, the user can drop at the space after the rows, and the row
   * that started the DnD operation is hidden forever.
   */
  if (!hovered_row)
    {
      gtk_widget_hide (priv->dnd_row);
      gtd_dnd_row_set_row_above (GTD_DND_ROW (priv->dnd_row), NULL);

      goto success;
    }

  /*
   * Hovering the DnD row is perfectly valid, but we don't gather the
   * related row - simply succeed.
   */
  if (GTD_IS_DND_ROW (hovered_row))
    goto success;

  row_above_dnd = NULL;
  task_row = hovered_row;
  row_height = gtk_widget_get_allocated_height (GTK_WIDGET (hovered_row));
  gtk_widget_translate_coordinates (GTK_WIDGET (listbox),
                                    GTK_WIDGET (hovered_row),
                                    x, y,
                                    &row_x, &row_y);

  gtk_widget_show (priv->dnd_row);

  /*
   * If the pointer if in the top part of the row, move the DnD row to
   * the previous row. Also, when hovering the new task row, only show
   * the dnd row over it (never below).
   */
  if (row_y < row_height / 2 || GTD_IS_NEW_TASK_ROW (task_row))
    {
      gint row_index, i;

      row_index = gtk_list_box_row_get_index (hovered_row);

      /* Search for a valid task row */
      for (i = row_index - 1; i >= 0; i--)
        {
          GtkListBoxRow *aux;

          aux = gtk_list_box_get_row_at_index (GTK_LIST_BOX (priv->listbox), i);

          /* Skip DnD, New task and hidden rows */
          if (!GTD_IS_TASK_ROW (aux) || (aux && !gtk_widget_get_visible (GTK_WIDGET (aux))))
            {
              continue;
            }

          row_above_dnd = aux;

          break;
        }
    }
  else
    {
      row_above_dnd = task_row;
    }

  /* Check if we're not trying to add a subtask */
  if (row_above_dnd)
    {
      GtdTask *row_above_task, *dnd_task;

      dnd_task = gtd_task_row_get_task (GTD_TASK_ROW (source_row));
      row_above_task = gtd_task_row_get_task (GTD_TASK_ROW (row_above_dnd));

      /* Forbid DnD'ing a row into a subtask */
      if (row_above_task && gtd_task_is_subtask (dnd_task, row_above_task))
        {
          gtk_widget_hide (priv->dnd_row);
          gtd_dnd_row_set_row_above (GTD_DND_ROW (priv->dnd_row), NULL);

          goto fail;
        }

    }

  gtd_dnd_row_set_row_above (GTD_DND_ROW (priv->dnd_row), row_above_dnd);

success:
  /*
   * Also pass the current motion to the DnD row, so it correctly
   * adjusts itself - even when the DnD is hovering another row.
   */
  success = gtd_dnd_row_drag_motion (GTK_WIDGET (priv->dnd_row), x, y);

  check_dnd_scroll (self, FALSE, y);

  return success;

fail:
  return FALSE;
}

static gboolean
listbox_drag_drop (GtkWidget       *widget,
                   GdkDrop         *drop,
                   gint             x,
                   gint             y,
                   guint            time,
                   GtdTaskListView *self)
{
  GtdTaskListViewPrivate *priv;
  gboolean success;

  GTD_ENTRY;

  priv = gtd_task_list_view_get_instance_private (self);
  success = gtd_dnd_row_drag_drop (GTK_WIDGET (priv->dnd_row), drop, x, y);

  check_dnd_scroll (self, TRUE, -1);

  gdk_drop_finish (drop, GDK_ACTION_MOVE);

  GTD_RETURN (success);
}


/*
 * GObject overrides
 */

static void
gtd_task_list_view_finalize (GObject *object)
{
  GtdTaskListViewPrivate *priv = GTD_TASK_LIST_VIEW (object)->priv;

  if (priv->idle_handler_id > 0)
    {
      g_source_remove (priv->idle_handler_id);
      priv->idle_handler_id = 0;
    }

  if (priv->scroll_to_bottom_handler_id > 0)
    {
      g_source_remove (priv->scroll_to_bottom_handler_id);
      priv->scroll_to_bottom_handler_id = 0;
    }

  g_clear_pointer (&priv->default_date, g_date_time_unref);
  g_clear_pointer (&priv->tasks, g_hash_table_destroy);
  g_clear_object (&priv->renderer);

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

  gtk_list_box_set_sort_func (self->priv->listbox,
                              (GtkListBoxSortFunc) default_sort_func,
                              NULL,
                              NULL);
}


/*
 * GtkWidget overrides
 */

static void
gtd_task_list_view_map (GtkWidget *widget)
{
  GtdTaskListViewPrivate *priv;
  GtkWidget *window;

  GTK_WIDGET_CLASS (gtd_task_list_view_parent_class)->map (widget);

  priv = GTD_TASK_LIST_VIEW (widget)->priv;
  window = gtk_widget_get_toplevel (widget);

  /* Clear previously added "list" actions */
  gtk_widget_insert_action_group (window,
                                  "list",
                                  NULL);

  /* Add this instance's action group */
  gtk_widget_insert_action_group (window,
                                  "list",
                                  priv->action_group);
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
  g_type_ensure (GTD_TYPE_DONE_BUTTON);

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

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/todo/ui/list-view.ui");

  gtk_widget_class_bind_template_child_private (widget_class, GtdTaskListView, dnd_row);
  gtk_widget_class_bind_template_child_private (widget_class, GtdTaskListView, due_date_sizegroup);
  gtk_widget_class_bind_template_child_private (widget_class, GtdTaskListView, listbox);
  gtk_widget_class_bind_template_child_private (widget_class, GtdTaskListView, new_task_row);
  gtk_widget_class_bind_template_child_private (widget_class, GtdTaskListView, tasklist_name_sizegroup);
  gtk_widget_class_bind_template_child_private (widget_class, GtdTaskListView, scrolled_window);
  gtk_widget_class_bind_template_child_private (widget_class, GtdTaskListView, stack);

  gtk_widget_class_bind_template_callback (widget_class, listbox_drag_drop);
  gtk_widget_class_bind_template_callback (widget_class, listbox_drag_leave);
  gtk_widget_class_bind_template_callback (widget_class, listbox_drag_motion);
  gtk_widget_class_bind_template_callback (widget_class, on_listbox_row_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_done_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_task_row_entered_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_task_row_exited_cb);

  gtk_widget_class_set_css_name (widget_class, "tasklistview");
}

static void
gtd_task_list_view_init (GtdTaskListView *self)
{
  GtdTaskListViewPrivate *priv = gtd_task_list_view_get_instance_private (self);

  self->priv = priv;

  priv->can_toggle = TRUE;
  priv->handle_subtasks = TRUE;
  priv->show_due_date = TRUE;
  priv->show_due_date = TRUE;

  priv->tasks = g_hash_table_new (g_direct_hash, g_direct_equal);

  gtk_widget_init_template (GTK_WIDGET (self));

  set_active_row (self, GTK_WIDGET (priv->new_task_row));

  gtk_drag_dest_set (GTK_WIDGET (priv->listbox),
                     0,
                     _gtd_get_content_formats (),
                     GDK_ACTION_MOVE);

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
 * gtd_task_list_view_get_list:
 * @view: a #GtdTaskListView
 *
 * Retrieves the list of tasks from @view. Note that,
 * if a #GtdTaskList is set, the #GtdTaskList's list
 * of task will be returned.
 *
 * Returns: (element-type Gtd.TaskList) (transfer full): the internal list of
 * tasks. Free with @g_list_free after use.
 */
GList*
gtd_task_list_view_get_list (GtdTaskListView *view)
{
  g_return_val_if_fail (GTD_IS_TASK_LIST_VIEW (view), NULL);

  if (view->priv->task_list)
    return get_tasks_from_list (view->priv->task_list);
  else
    return g_hash_table_get_keys (view->priv->tasks);
}

/**
 * gtd_task_list_view_set_list:
 * @view: a #GtdTaskListView
 * @list: (element-type Gtd.Task) (nullable): a list of tasks
 *
 * Copies the tasks from @list to @view.
 */
void
gtd_task_list_view_set_list (GtdTaskListView *view,
                             GList           *list)
{
  g_autoptr (GHashTable) new_tasks = NULL;
  g_autoptr (GPtrArray) removed = NULL;
  g_autoptr (GPtrArray) added = NULL;
  g_autoptr (GList) old_list = NULL;
  GtdTaskListViewPrivate *priv;
  GList *l;

  g_return_if_fail (GTD_IS_TASK_LIST_VIEW (view));

  priv = gtd_task_list_view_get_instance_private (view);

  /*
   * If we are currently adding tasks in idle, cancel the idle before
   * anything else. Just for safety.
   */
  if (priv->idle_handler_id > 0)
    {
      GTD_TRACE_MSG ("Cancelling current idle add");

      g_source_remove (priv->idle_handler_id);
      priv->idle_handler_id = 0;
    }

  added = g_ptr_array_new_full (100, g_object_unref);
  removed = g_ptr_array_new_full (100, g_object_unref);
  new_tasks = g_hash_table_new (g_direct_hash, g_direct_equal);
  old_list = g_hash_table_get_keys (priv->tasks);

  /* Reset the DnD parent row */
  gtd_dnd_row_set_row_above (GTD_DND_ROW (priv->dnd_row), NULL);

  /* Map the current tasks */
  for (l = list; l; l = l->next)
    g_hash_table_add (new_tasks, l->data);

  /* Remove the tasks that are in the current list, but not in the new list */
  for (l = old_list; l; l = l->next)
    {
      if (!g_hash_table_contains (new_tasks, l->data))
        g_ptr_array_add (removed, g_object_ref (l->data));
    }

  /* Add the tasks that are in the new list, but not in the current list */
  for (l = list; l; l = l->next)
    {
      if (g_hash_table_contains (priv->tasks, l->data))
        continue;

      g_ptr_array_add (added, g_object_ref (l->data));
    }

  /* Sort the array so we can add the tasks in the order they'll be displayed */
  g_ptr_array_sort (added, sort_tasks_gptrarray_cb);

  /*
   * Add the new tasks and remove the old ones in idle, to keep
   * a slick UI responsiveness.
   */
  add_and_remove_tasks_in_idle (view, added, removed);
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
 * gtd_task_list_view_get_task_list:
 * @view: a #GtdTaskListView
 *
 * Retrieves the #GtdTaskList from @view, or %NULL if none was set.
 *
 * Returns: (transfer none): the @GtdTaskList of @view, or %NULL is
 * none was set.
 */
GtdTaskList*
gtd_task_list_view_get_task_list (GtdTaskListView *view)
{
  g_return_val_if_fail (GTD_IS_TASK_LIST_VIEW (view), NULL);

  return view->priv->task_list;
}

/**
 * gtd_task_list_view_set_task_list:
 * @view: a #GtdTaskListView
 * @list: a #GtdTaskList
 *
 * Sets the internal #GtdTaskList of @view.
 */
void
gtd_task_list_view_set_task_list (GtdTaskListView *view,
                                  GtdTaskList     *list)
{
  GtdTaskListViewPrivate *priv;
  g_autoptr (GdkRGBA) color = NULL;
  g_autoptr (GList) task_list = NULL;
  g_autofree gchar *parsed_css = NULL;
  g_autofree gchar *color_str = NULL;

  g_return_if_fail (GTD_IS_TASK_LIST_VIEW (view));

  priv = gtd_task_list_view_get_instance_private (view);

  if (priv->task_list == list)
    return;

  g_debug ("%p: Setting task list to '%s'", view, gtd_task_list_get_name (list));

  gtd_new_task_row_set_show_list_selector (GTD_NEW_TASK_ROW (priv->new_task_row), list == NULL);

  /*
   * Disconnect the old GtdTaskList signals.
   */
  if (priv->task_list)
    {
      g_signal_handlers_disconnect_by_func (priv->task_list, on_task_list_task_added_cb, view);
      g_signal_handlers_disconnect_by_func (priv->task_list, on_task_list_color_changed_cb, view);
    }

  priv->task_list = list;

  if (!list)
    {
      gtd_task_list_view_set_list (view, NULL);
      return;
    }

  /* Add the color to provider */
  color = gtd_task_list_get_color (list);
  color_str = gdk_rgba_to_string (color);
  parsed_css = g_strdup_printf (COLOR_TEMPLATE, color_str);

  //gtk_css_provider_load_from_data (priv->color_provider, parsed_css, -1);

  update_font_color (view);

  /* Add the tasks from the list */
  task_list = get_tasks_from_list (list);
  gtd_task_list_view_set_list (view, task_list);

  g_signal_connect (list,
                    "task-added",
                    G_CALLBACK (on_task_list_task_added_cb),
                    view);
  g_signal_connect_swapped (list,
                            "task-removed",
                            G_CALLBACK (on_task_list_task_removed_cb),
                            view);
  g_signal_connect_swapped (list,
                            "notify::color",
                            G_CALLBACK (on_task_list_color_changed_cb),
                            view);
  g_signal_connect_swapped (list,
                            "task-updated",
                            G_CALLBACK (gtk_list_box_invalidate_sort),
                            priv->listbox);

  set_active_row (view, GTK_WIDGET (priv->new_task_row));
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
 * gtd_task_list_view_set_filter_func:
 * @view: a #GtdTaskListView
 * @func: (closure user_data) (scope call) (nullable): the filter function
 * @user_data: data passed to @func
 *
 * Sets @func as the filter function of @view.
 *
 * Do not unref nor free any of the passed data.
 */
void
gtd_task_list_view_set_filter_func (GtdTaskListView           *view,
                                    GtdTaskListViewFilterFunc  func,
                                    gpointer                   user_data)
{
  GtdTaskListViewPrivate *priv;

  g_return_if_fail (GTD_IS_TASK_LIST_VIEW (view));

  priv = view->priv;

  if (func)
    {
      priv->filter_func = func;
      priv->filter_user_data = user_data;

      gtk_list_box_set_filter_func (priv->listbox,
                                    (GtkListBoxFilterFunc) custom_filter_func,
                                    view,
                                    NULL);
    }
  else
    {
      priv->filter_func = NULL;
      priv->filter_user_data = NULL;

      gtk_list_box_set_filter_func (priv->listbox, NULL, NULL, NULL);
    }
}

/**
 * gtd_task_list_view_set_sort_func:
 * @view: a #GtdTaskListView
 * @func: (closure user_data) (scope call) (nullable): the sort function
 * @user_data: data passed to @func
 *
 * Sets @func as the sorting function of @view.
 *
 * Do not unref nor free any of the passed data.
 */
void
gtd_task_list_view_set_sort_func (GtdTaskListView         *view,
                                  GtdTaskListViewSortFunc  func,
                                  gpointer                 user_data)
{
  GtdTaskListViewPrivate *priv;

  g_return_if_fail (GTD_IS_TASK_LIST_VIEW (view));

  priv = gtd_task_list_view_get_instance_private (view);

  if (func)
    {
      priv->sort_func = func;
      priv->header_user_data = user_data;

      gtk_list_box_set_sort_func (priv->listbox,
                                  (GtkListBoxSortFunc) custom_sort_func,
                                  view,
                                  NULL);
    }
  else
    {
      priv->sort_func = NULL;
      priv->sort_user_data = NULL;

      gtk_list_box_set_sort_func (priv->listbox,
                                  (GtkListBoxSortFunc) default_sort_func,
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

/**
 * gtd_task_list_view_invalidate:
 * @self: a #GtdTaskListView
 *
 * Invalidates the sorting and headers of @self.
 */
void
gtd_task_list_view_invalidate (GtdTaskListView *self)
{
  GtdTaskListViewPrivate *priv;

  g_return_if_fail (GTD_IS_TASK_LIST_VIEW (self));

  priv = gtd_task_list_view_get_instance_private (self);

  gtk_list_box_invalidate_sort (priv->listbox);
  gtk_list_box_invalidate_headers (priv->listbox);
  gtk_list_box_invalidate_filter (priv->listbox);
}
