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
#include "gtd-edit-pane.h"
#include "gtd-empty-list-widget.h"
#include "gtd-task-list-view.h"
#include "gtd-task-list-view-model.h"
#include "gtd-manager.h"
#include "gtd-markdown-renderer.h"
#include "gtd-new-task-row.h"
#include "gtd-notification.h"
#include "gtd-provider.h"
#include "gtd-task.h"
#include "gtd-task-list.h"
#include "gtd-task-row.h"
#include "gtd-utils-private.h"
#include "gtd-widget.h"
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
 * // Date which tasks will be automatically assigned
 * gtd_task_list_view_set_default_date (view, now);
 * ]|
 *
 */

struct _GtdTaskListView
{
  GtkBox                  parent;

  GtdEmptyListWidget    *empty_list_widget;
  GtkListBox            *listbox;
  GtkStack              *main_stack;
  GtkWidget             *scrolled_window;

  /* internal */
  gboolean               can_toggle;
  gboolean               show_due_date;
  gboolean               show_list_name;
  GtdTaskListViewModel  *view_model;
  GListModel            *model;
  GDateTime             *default_date;

  GListModel            *incomplete_tasks_model;
  guint                  n_incomplete_tasks;

  guint                  scroll_to_bottom_handler_id;

  GHashTable            *task_to_row;

  /* Markup renderer*/
  GtdMarkdownRenderer   *renderer;

  /* DnD */
  guint                  scroll_timeout_id;
  gboolean               scroll_up;

  /* action */
  GActionGroup          *action_group;

  struct {
    GtkWidget *top;
    GtkWidget *bottom;
  } drag_highlight;

  /* Custom header function data */
  GtdTaskListViewHeaderFunc header_func;
  gpointer                  header_user_data;

  GtdTaskRow             *active_row;
  GtkSizeGroup           *due_date_sizegroup;
  GtkSizeGroup           *tasklist_name_sizegroup;
};

#define DND_SCROLL_OFFSET            24 //px
#define TASK_REMOVED_NOTIFICATION_ID "task-removed-id"


static gboolean      filter_complete_func                        (gpointer            item,
                                                                  gpointer            user_data);

static void          on_clear_completed_tasks_activated_cb       (GSimpleAction      *simple,
                                                                  GVariant           *parameter,
                                                                  gpointer            user_data);

static void          on_incomplete_tasks_items_changed_cb        (GListModel         *model,
                                                                  guint               position,
                                                                  guint               n_removed,
                                                                  guint               n_added,
                                                                  GtdTaskListView    *self);

static void          on_remove_task_row_cb                       (GtdTaskRow         *row,
                                                                  GtdTaskListView    *self);

static void          on_task_row_entered_cb                      (GtdTaskListView    *self,
                                                                  GtdTaskRow         *row);

static void          on_task_row_exited_cb                       (GtdTaskListView    *self,
                                                                  GtdTaskRow         *row);

static gboolean      scroll_to_bottom_cb                         (gpointer            data);


G_DEFINE_TYPE (GtdTaskListView, gtd_task_list_view, GTK_TYPE_BOX)

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
  PROP_SHOW_LIST_NAME,
  PROP_SHOW_DUE_DATE,
  LAST_PROP
};


/*
 * Auxiliary methods
 */

static inline GtdTaskRow*
task_row_from_row (GtkListBoxRow *row)
{
  GtkWidget *child = gtk_list_box_row_get_child (row);

  if (!GTD_IS_TASK_ROW (child))
    return NULL;

  return GTD_TASK_ROW (child);
}

static void
set_active_row (GtdTaskListView *self,
                GtdTaskRow      *row)
{
  if (self->active_row == row)
    return;

  if (self->active_row)
    gtd_task_row_set_active (self->active_row, FALSE);

  self->active_row = row;

  if (row)
    {
      gtd_task_row_set_active (row, TRUE);
      gtk_widget_grab_focus (GTK_WIDGET (row));
    }
}

static void
schedule_scroll_to_bottom (GtdTaskListView *self)
{
  if (self->scroll_to_bottom_handler_id > 0)
    return;

  self->scroll_to_bottom_handler_id = g_timeout_add (250, scroll_to_bottom_cb, self);
}

static void
update_empty_state (GtdTaskListView *self)
{
  gboolean show_empty_list_widget;
  gboolean is_empty;

  g_assert (GTD_IS_TASK_LIST_VIEW (self));

  if (!self->model)
    return;

  is_empty = g_list_model_get_n_items (self->model) == 0;
  gtd_empty_list_widget_set_is_empty (self->empty_list_widget, is_empty);

  show_empty_list_widget = !GTD_IS_TASK_LIST (self->model) &&
                           (is_empty || self->n_incomplete_tasks == 0);
  gtk_stack_set_visible_child_name (self->main_stack,
                                    show_empty_list_widget ? "empty-list" : "task-list");
}

static void
update_incomplete_tasks_model (GtdTaskListView *self)
{
  if (!self->incomplete_tasks_model)
    {
      g_autoptr (GtkFilterListModel) filter_model = NULL;
      GtkCustomFilter *filter;

      filter = gtk_custom_filter_new (filter_complete_func, self, NULL);
      filter_model = gtk_filter_list_model_new (NULL, GTK_FILTER (filter));
      gtk_filter_list_model_set_incremental (filter_model, TRUE);

      self->incomplete_tasks_model = G_LIST_MODEL (g_steal_pointer (&filter_model));
    }

  gtk_filter_list_model_set_model (GTK_FILTER_LIST_MODEL (self->incomplete_tasks_model),
                                   self->model);
  self->n_incomplete_tasks = g_list_model_get_n_items (self->incomplete_tasks_model);

  g_signal_connect (self->incomplete_tasks_model,
                    "items-changed",
                    G_CALLBACK (on_incomplete_tasks_items_changed_cb),
                    self);
}


/*
 * Callbacks
 */

static gboolean
filter_complete_func (gpointer item,
                      gpointer user_data)
{
  GtdTask *task = (GtdTask*) item;
  return !gtd_task_get_complete (task);
}

static void
on_incomplete_tasks_items_changed_cb (GListModel      *model,
                                      guint            position,
                                      guint            n_removed,
                                      guint            n_added,
                                      GtdTaskListView *self)
{
  self->n_incomplete_tasks -= n_removed;
  self->n_incomplete_tasks += n_added;

  update_empty_state (self);
}

static void
on_empty_list_widget_add_tasks_cb (GtdEmptyListWidget *empty_list_widget,
                                   GtdTaskListView    *self)
{
  gtk_stack_set_visible_child_name (self->main_stack, "task-list");
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

static GtkWidget*
create_row_for_task_cb (gpointer item,
                        gpointer user_data)
{
  GtdTaskListView *self;
  GtkWidget *listbox_row;
  GtkWidget *row;

  self = GTD_TASK_LIST_VIEW (user_data);

  listbox_row = gtk_list_box_row_new ();

  if (GTD_IS_TASK (item))
    {
      row = gtd_task_row_new (item, self->renderer);

      gtd_task_row_set_list_name_visible (GTD_TASK_ROW (row), self->show_list_name);
      gtd_task_row_set_due_date_visible (GTD_TASK_ROW (row), self->show_due_date);

      g_signal_connect_swapped (row, "enter", G_CALLBACK (on_task_row_entered_cb), self);
      g_signal_connect_swapped (row, "exit", G_CALLBACK (on_task_row_exited_cb), self);

      g_signal_connect (row, "remove-task", G_CALLBACK (on_remove_task_row_cb), self);
    }
  else
    {
      row = gtd_new_task_row_new ();

      g_signal_connect_swapped (row, "enter", G_CALLBACK (on_new_task_row_entered_cb), self);
      g_signal_connect_swapped (row, "exit", G_CALLBACK (on_new_task_row_exited_cb), self);

      gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (listbox_row), FALSE);
    }

  gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (listbox_row), row);

  g_object_bind_property (row, "visible", listbox_row, "visible", G_BINDING_BIDIRECTIONAL);

  g_hash_table_insert (self->task_to_row, item, row);

  return listbox_row;
}

static gboolean
scroll_to_bottom_cb (gpointer data)
{
  GtdTaskListView *self = GTD_TASK_LIST_VIEW (data);
  GtkWidget *widget;
  GtkRoot *root;

  widget = GTK_WIDGET (self);
  root = gtk_widget_get_root (widget);

  if (!root)
    return G_SOURCE_CONTINUE;

  self->scroll_to_bottom_handler_id = 0;

  /*
   * Only focus the new task row if the current list is visible,
   * and the focused widget isn't inside this list view.
   */
  if (gtk_widget_get_visible (widget) &&
      gtk_widget_get_child_visible (widget) &&
      gtk_widget_get_mapped (widget) &&
      !gtk_widget_is_ancestor (gtk_window_get_focus (GTK_WINDOW (root)), widget))
    {
      GtkWidget *new_task_row;
      gboolean ignored;

      new_task_row = gtk_widget_get_last_child (GTK_WIDGET (self->listbox));
      gtk_widget_grab_focus (new_task_row);
      g_signal_emit_by_name (self->scrolled_window, "scroll-child", GTK_SCROLL_END, FALSE, &ignored);
    }

  return G_SOURCE_REMOVE;
}

static void
on_task_removed_cb (GObject      *source,
                    GAsyncResult *result,
                    gpointer      user_data)
{
  g_autoptr (GError) error = NULL;

  gtd_provider_remove_task_finish (GTD_PROVIDER (source), result, &error);

  if (error)
    g_warning ("Error removing task list: %s", error->message);
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
  model = self->model;

  for (i = 0; i < g_list_model_get_n_items (model); i++)
    {
      g_autoptr (GtdTask) task = g_list_model_get_item (model, i);

      if (!gtd_task_get_complete (task))
        continue;

      gtd_provider_remove_task (gtd_task_get_provider (task),
                                task,
                                NULL,
                                on_task_removed_cb,
                                self);
    }
}

static void
on_remove_task_action_cb (GtdNotification *notification,
                          gpointer         user_data)
{
  RemoveTaskData *data = user_data;

  gtd_provider_remove_task (gtd_task_get_provider (data->task),
                            data->task,
                            NULL,
                            on_task_removed_cb,
                            data->view);

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

  /* Remove task from the list */
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
on_task_row_entered_cb (GtdTaskListView *self,
                        GtdTaskRow      *row)
{
  set_active_row (self, row);
}

static void
on_task_row_exited_cb (GtdTaskListView *self,
                       GtdTaskRow      *row)
{
  if (row == self->active_row)
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

  if (!task_row)
    GTD_RETURN ();

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
                      GtdTaskListView *self)
{
  GtkWidget *header;
  GtdTask *row_task;
  GtdTask *before_task;

  if (!self->header_func)
    return;

  row_task = before_task = NULL;

  if (!task_row_from_row (row))
    return;

  if (row)
    row_task = gtd_task_row_get_task (task_row_from_row (row));

  if (before)
      before_task = gtd_task_row_get_task (task_row_from_row (before));

  header = self->header_func (row_task, before_task, self->header_user_data);

  if (header)
    {
      GtkWidget *real_header = gtd_widget_new ();
      gtk_widget_insert_before (header, real_header, NULL);

      header = real_header;
    }

  gtk_list_box_row_set_header (row, header);
}


/*
 * Drag n' Drop functions
 */

static void
clear_drag_highlights (GtdTaskListView *self)
{
  if (self->drag_highlight.top)
    {
      gtk_widget_remove_css_class (self->drag_highlight.top, "top-highlight");
      self->drag_highlight.top = NULL;
    }

  if (self->drag_highlight.bottom)
    {
      gtk_widget_remove_css_class (self->drag_highlight.bottom, "bottom-highlight");
      self->drag_highlight.bottom = NULL;
    }
}

static void
update_row_drag_highlight (GtdTaskListView *self,
                           gdouble          y)
{
  GtkAllocation row_allocation;
  GtkListBoxRow *hovered_row;
  GtkListBoxRow *bottom_highlight;
  GtkListBoxRow *top_highlight;

  hovered_row = gtk_list_box_get_row_at_y (self->listbox, y);
  bottom_highlight = NULL;
  top_highlight = NULL;

  gtk_widget_get_allocation (GTK_WIDGET (hovered_row), &row_allocation);

  /*
   * If the pointer if in the top part of the row, move the DnD row to
   * the previous row.
   */
  if (y < row_allocation.y + row_allocation.height / 2)
    {
      GtkWidget *aux;

      top_highlight = hovered_row;

      /* Search for a valid task row */
      for (aux = gtk_widget_get_prev_sibling (GTK_WIDGET (hovered_row));
           aux;
           aux = gtk_widget_get_prev_sibling (aux))
        {
          /* Skip DnD, New task and hidden rows */
          if (!gtk_widget_get_visible (aux))
            continue;

          bottom_highlight = GTK_LIST_BOX_ROW (aux);
          break;
        }
    }
  else
    {
      GtkWidget *aux;

      bottom_highlight = hovered_row;

      /* Search for a valid task row */
      for (aux = gtk_widget_get_next_sibling (GTK_WIDGET (hovered_row));
           aux;
           aux = gtk_widget_get_next_sibling (aux))
        {
          /* Skip DnD, New task and hidden rows */
          if (!gtk_widget_get_visible (aux))
            continue;

          top_highlight = GTK_LIST_BOX_ROW (aux);
          break;
        }
    }

  /* Don't add drag highlights to the new task row */
  if (top_highlight && !GTD_IS_TASK_ROW (task_row_from_row (top_highlight)))
    top_highlight = NULL;
  if (bottom_highlight && !GTD_IS_TASK_ROW (task_row_from_row (bottom_highlight)))
    bottom_highlight = NULL;

  /* Unhighlight previously highlighted rows */
  clear_drag_highlights (self);

  self->drag_highlight.top = GTK_WIDGET (top_highlight);
  self->drag_highlight.bottom = GTK_WIDGET (bottom_highlight);

  /* Highlight new rows */
  if (self->drag_highlight.top)
    gtk_widget_add_css_class (self->drag_highlight.top, "top-highlight");
  if (self->drag_highlight.bottom)
    gtk_widget_add_css_class (self->drag_highlight.bottom, "bottom-highlight");
}

static GtkListBoxRow*
get_drop_row_at_y (GtdTaskListView *self,
                   gdouble          y)
{
  GtkAllocation row_allocation;
  GtkListBoxRow *hovered_row;
  GtkListBoxRow *task_row;
  GtkListBoxRow *drop_row;

  hovered_row = gtk_list_box_get_row_at_y (self->listbox, y);

  /* Small optimization when hovering the first row */
  if (gtk_list_box_row_get_index (hovered_row) == 0)
    return hovered_row;

  drop_row = NULL;
  task_row = hovered_row;

  gtk_widget_get_allocation (GTK_WIDGET (hovered_row), &row_allocation);

  /*
   * If the pointer if in the top part of the row, move the DnD row to
   * the previous row.
   */
  if (y < row_allocation.y + row_allocation.height / 2)
    {
      GtkWidget *aux;

      /* Search for a valid task row */
      for (aux = gtk_widget_get_prev_sibling (GTK_WIDGET (hovered_row));
           aux;
           aux = gtk_widget_get_prev_sibling (aux))
        {
          /* Skip DnD, New task and hidden rows */
          if (!gtk_widget_get_visible (aux))
            continue;

          drop_row = GTK_LIST_BOX_ROW (aux);
          break;
        }
    }
  else
    {
      drop_row = task_row;
    }

  return task_row_from_row (drop_row) ? drop_row : NULL;
}

static inline gboolean
scroll_to_dnd (gpointer user_data)
{
  GtdTaskListView *self = GTD_TASK_LIST_VIEW (user_data);
  GtkAdjustment *vadjustment;
  gint value;

  vadjustment = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->scrolled_window));
  value = gtk_adjustment_get_value (vadjustment) + (self->scroll_up ? -6 : 6);

  gtk_adjustment_set_value (vadjustment,
                            CLAMP (value, 0, gtk_adjustment_get_upper (vadjustment)));

  return G_SOURCE_CONTINUE;
}

static void
check_dnd_scroll (GtdTaskListView *self,
                  gboolean         should_cancel,
                  gdouble          y)
{
  gdouble current_y, height;

  if (should_cancel)
    {
      if (self->scroll_timeout_id > 0)
        {
          g_source_remove (self->scroll_timeout_id);
          self->scroll_timeout_id = 0;
        }

      return;
    }

  height = gtk_widget_get_allocated_height (self->scrolled_window);
  gtk_widget_translate_coordinates (GTK_WIDGET (self->listbox),
                                    self->scrolled_window,
                                    0, y,
                                    NULL, &current_y);

  if (current_y < DND_SCROLL_OFFSET || current_y > height - DND_SCROLL_OFFSET)
    {
      if (self->scroll_timeout_id > 0)
        return;

      /* Start the autoscroll */
      self->scroll_up = current_y < DND_SCROLL_OFFSET;
      self->scroll_timeout_id = g_timeout_add (25,
                                               scroll_to_dnd,
                                               self);
    }
  else
    {
      if (self->scroll_timeout_id == 0)
        return;

      /* Cancel the autoscroll */
      g_source_remove (self->scroll_timeout_id);
      self->scroll_timeout_id = 0;
    }
}

static GdkDragAction
on_drop_target_drag_enter_cb (GtkDropTarget   *drop_target,
                              gdouble          x,
                              gdouble          y,
                              GtdTaskListView *self)
{
  GTD_ENTRY;

  update_row_drag_highlight (self, y);

  GTD_RETURN (GDK_ACTION_MOVE);
}

static void
on_drop_target_drag_leave_cb (GtkDropTarget   *drop_target,
                              GtdTaskListView *self)
{
  GTD_ENTRY;

  clear_drag_highlights (self);
  check_dnd_scroll (self, TRUE, -1);

  GTD_EXIT;
}

static GdkDragAction
on_drop_target_drag_motion_cb (GtkDropTarget   *drop_target,
                               gdouble          x,
                               gdouble          y,
                               GtdTaskListView *self)
{
  GdkDrop *drop;
  GdkDrag *drag;

  GTD_ENTRY;

  drop = gtk_drop_target_get_current_drop (drop_target);
  drag = gdk_drop_get_drag (drop);

  if (!drag)
    {
      g_info ("Only dragging task rows is supported");
      GTD_GOTO (fail);
    }

  update_row_drag_highlight (self, y);
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
  GtkListBoxRow *drop_row;
  GtdTaskRow *hovered_row;
  GtkWidget *row;
  GtdTask *hovered_task;
  GtdTask *source_task;
  GdkDrop *drop;
  GdkDrag *drag;
  gint64 current_position;
  gint64 new_position;

  GTD_ENTRY;

  drop = gtk_drop_target_get_current_drop (drop_target);
  drag = gdk_drop_get_drag (drop);

  if (!drag)
    {
      g_info ("Only dragging task rows is supported");
      GTD_RETURN (FALSE);
    }

  clear_drag_highlights (self);

  source_task = g_value_get_object (value);
  g_assert (source_task != NULL);

  /*
   * When the drag operation began, the source row was hidden. Now is the time
   * to show it again.
   */
  row = g_hash_table_lookup (self->task_to_row, source_task);
  gtk_widget_show (row);

  drop_row = get_drop_row_at_y (self, y);
  if (!drop_row)
    {
      check_dnd_scroll (self, TRUE, -1);
      GTD_RETURN (FALSE);
    }

  hovered_row = task_row_from_row (drop_row);
  hovered_task = gtd_task_row_get_task (hovered_row);
  new_position = gtd_task_get_position (hovered_task);
  current_position = gtd_task_get_position (source_task);

  GTD_TRACE_MSG ("Dropping task %p at %ld", source_task, new_position);

  if (new_position != current_position)
    {
      gtd_task_list_move_task_to_position (GTD_TASK_LIST (self->model),
                                           source_task,
                                           new_position);
    }

  check_dnd_scroll (self, TRUE, -1);

  GTD_RETURN (TRUE);
}


/*
 * GObject overrides
 */

static void
gtd_task_list_view_finalize (GObject *object)
{
  GtdTaskListView *self = GTD_TASK_LIST_VIEW (object);

  g_clear_handle_id (&self->scroll_to_bottom_handler_id, g_source_remove);
  g_clear_pointer (&self->task_to_row, g_hash_table_destroy);
  g_clear_pointer (&self->default_date, g_date_time_unref);
  g_clear_object (&self->incomplete_tasks_model);
  g_clear_object (&self->renderer);
  g_clear_object (&self->model);

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
    case PROP_SHOW_DUE_DATE:
      g_value_set_boolean (value, self->show_due_date);
      break;

    case PROP_SHOW_LIST_NAME:
      g_value_set_boolean (value, self->show_list_name);
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
    case PROP_SHOW_DUE_DATE:
      gtd_task_list_view_set_show_due_date (self, g_value_get_boolean (value));
      break;

    case PROP_SHOW_LIST_NAME:
      gtd_task_list_view_set_show_list_name (self, g_value_get_boolean (value));
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
  self->action_group = G_ACTION_GROUP (g_simple_action_group_new ());

  g_action_map_add_action_entries (G_ACTION_MAP (self->action_group),
                                   gtd_task_list_view_entries,
                                   G_N_ELEMENTS (gtd_task_list_view_entries),
                                   object);
}


/*
 * GtkWidget overrides
 */

static void
gtd_task_list_view_map (GtkWidget *widget)
{
  GtdTaskListView *self = GTD_TASK_LIST_VIEW (widget);
  GtkRoot *root;


  update_empty_state (self);

  GTK_WIDGET_CLASS (gtd_task_list_view_parent_class)->map (widget);

  root = gtk_widget_get_root (widget);

  /* Clear previously added "list" actions */
  gtk_widget_insert_action_group (GTK_WIDGET (root), "list", NULL);

  /* Add this instance's action group */
  gtk_widget_insert_action_group (GTK_WIDGET (root), "list", self->action_group);
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
  g_type_ensure (GTD_TYPE_EMPTY_LIST_WIDGET);

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

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/todo/ui/gtd-task-list-view.ui");

  gtk_widget_class_bind_template_child (widget_class, GtdTaskListView, due_date_sizegroup);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskListView, empty_list_widget);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskListView, listbox);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskListView, main_stack);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskListView, tasklist_name_sizegroup);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskListView, scrolled_window);

  gtk_widget_class_bind_template_callback (widget_class, on_empty_list_widget_add_tasks_cb);
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
  GtkDropTarget *target;

  self->task_to_row = g_hash_table_new (NULL, NULL);

  self->can_toggle = TRUE;
  self->show_due_date = TRUE;
  self->show_due_date = TRUE;

  gtk_widget_init_template (GTK_WIDGET (self));

  target = gtk_drop_target_new (GTD_TYPE_TASK, GDK_ACTION_MOVE);
  gtk_drop_target_set_preload (target, TRUE);
  g_signal_connect (target, "drop", G_CALLBACK (on_drop_target_drag_drop_cb), self);
  g_signal_connect (target, "enter", G_CALLBACK (on_drop_target_drag_enter_cb), self);
  g_signal_connect (target, "leave", G_CALLBACK (on_drop_target_drag_leave_cb), self);
  g_signal_connect (target, "motion", G_CALLBACK (on_drop_target_drag_motion_cb), self);

  gtk_widget_add_controller (GTK_WIDGET (self->listbox), GTK_EVENT_CONTROLLER (target));

  self->renderer = gtd_markdown_renderer_new ();

  self->view_model = gtd_task_list_view_model_new ();
  gtk_list_box_bind_model (self->listbox,
                           G_LIST_MODEL (self->view_model),
                           create_row_for_task_cb,
                           self,
                           NULL);
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

  return view->model;
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
  g_return_if_fail (GTD_IS_TASK_LIST_VIEW (view));
  g_return_if_fail (G_IS_LIST_MODEL (model));

  if (view->model == model)
    return;

  view->model = model;

  gtd_task_list_view_model_set_model (view->view_model, model);
  schedule_scroll_to_bottom (view);
  update_incomplete_tasks_model (view);
  update_empty_state (view);
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

  return view->show_list_name;
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

  if (view->show_list_name != show_list_name)
    {
      GtkWidget *child;

      view->show_list_name = show_list_name;

      for (child = gtk_widget_get_first_child (GTK_WIDGET (view->listbox));
           child;
           child = gtk_widget_get_next_sibling (child))
        {
          GtkWidget *row_child = gtk_list_box_row_get_child (GTK_LIST_BOX_ROW (child));

          if (!GTD_IS_TASK_ROW (row_child))
            continue;

          gtd_task_row_set_list_name_visible (GTD_TASK_ROW (row_child), show_list_name);
        }

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

  return self->show_due_date;
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
  GtkWidget *child;

  g_return_if_fail (GTD_IS_TASK_LIST_VIEW (self));

  if (self->show_due_date == show_due_date)
    return;

  self->show_due_date = show_due_date;

  for (child = gtk_widget_get_first_child (GTK_WIDGET (self->listbox));
       child;
       child = gtk_widget_get_next_sibling (child))
    {
      GtkWidget *row_child = gtk_list_box_row_get_child (GTK_LIST_BOX_ROW (child));

      if (!GTD_IS_TASK_ROW (row_child))
        continue;

      gtd_task_row_set_due_date_visible (GTD_TASK_ROW (row_child), show_due_date);
    }

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
  g_return_if_fail (GTD_IS_TASK_LIST_VIEW (view));

  if (func)
    {
      view->header_func = func;
      view->header_user_data = user_data;

      gtk_list_box_set_header_func (view->listbox,
                                    (GtkListBoxUpdateHeaderFunc) internal_header_func,
                                    view,
                                    NULL);
    }
  else
    {
      view->header_func = NULL;
      view->header_user_data = NULL;

      gtk_list_box_set_header_func (view->listbox,
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
  g_return_val_if_fail (GTD_IS_TASK_LIST_VIEW (self), NULL);

  return self->default_date;
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
  g_return_if_fail (GTD_IS_TASK_LIST_VIEW (self));

  if (self->default_date == default_date)
    return;

  g_clear_pointer (&self->default_date, g_date_time_unref);
  self->default_date = default_date ? g_date_time_ref (default_date) : NULL;
}
