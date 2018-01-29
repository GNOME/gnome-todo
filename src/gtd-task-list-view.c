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

#include "gtd-dnd-row.h"
#include "gtd-edit-pane.h"
#include "gtd-empty-list-widget.h"
#include "gtd-task-list-view.h"
#include "gtd-manager.h"
#include "gtd-markup-renderer.h"
#include "gtd-new-task-row.h"
#include "gtd-notification.h"
#include "gtd-provider.h"
#include "gtd-task.h"
#include "gtd-task-list.h"
#include "gtd-task-row.h"
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
  GtkWidget             *empty_box;
  GtkListBox            *listbox;
  GtkListBoxRow         *new_task_row;
  GtkRevealer           *revealer;
  GtkImage              *done_image;
  GtkLabel              *done_label;
  GtkWidget             *viewport;
  GtkWidget             *scrolled_window;

  /* internal */
  gboolean               can_toggle;
  gint                   complete_tasks;
  gboolean               show_completed : 1;
  gboolean               show_due_date : 1;
  gboolean               show_list_name : 1;
  gboolean               handle_subtasks : 1;
  GList                 *list;
  GtdTaskList           *task_list;
  GDateTime             *default_date;

  /* Markup renderer*/
  GtdMarkupRenderer     *renderer;

  /* DnD autoscroll */
  guint                  scroll_timeout_id;
  gboolean               scroll_up : 1;

  /* color provider */
  GtkCssProvider        *color_provider;
  GdkRGBA               *color;

  /* action */
  GActionGroup          *action_group;

  /* Custom header function data */
  GtdTaskListViewHeaderFunc header_func;
  gpointer                  header_user_data;

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

#define COLOR_TEMPLATE "viewport {background-color: %s;}"
#define LUMINANCE(c)   (0.299 * c->red + 0.587 * c->green + 0.114 * c->blue)

#define TASK_REMOVED_NOTIFICATION_ID             "task-removed-id"

#define DND_SCROLL_OFFSET                        24 // px

/* prototypes */
static void             gtd_task_list_view__clear_completed_tasks    (GSimpleAction     *simple,
                                                                      GVariant          *parameter,
                                                                      gpointer           user_data);

static void             gtd_task_list_view__remove_row_for_task      (GtdTaskListView   *view,
                                                                      GtdTask           *task);

static void             gtd_task_list_view__remove_task              (GtdTaskListView   *view,
                                                                      GtdTask           *task);

static void             gtd_task_list_view__update_done_label         (GtdTaskListView   *view);

static void             task_completed_cb                            (GtdTask           *task,
                                                                      GParamSpec        *pspec,
                                                                      GtdTaskListView   *self);

G_DEFINE_TYPE_WITH_PRIVATE (GtdTaskListView, gtd_task_list_view, GTK_TYPE_OVERLAY)

static const GActionEntry gtd_task_list_view_entries[] = {
  { "clear-completed-tasks", gtd_task_list_view__clear_completed_tasks },
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
  PROP_SHOW_COMPLETED,
  PROP_SHOW_LIST_NAME,
  PROP_SHOW_DUE_DATE,
  PROP_SHOW_NEW_TASK_ROW,
  LAST_PROP
};

typedef gboolean     (*IterateSubtaskFunc)                       (GtdTaskListView    *self,
                                                                  GtdTask            *task);

/*
 * Active row management
 */
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
      {
        gtd_markup_renderer_clear_renderer (priv->renderer);

        gtd_task_row_set_active (GTD_TASK_ROW (priv->active_row), FALSE);
      }
      else
        gtd_new_task_row_set_active (GTD_NEW_TASK_ROW (priv->active_row), FALSE);
    }

  priv->active_row = row;

  if (row)
    {
      if (GTD_IS_TASK_ROW (row))
        {
          gtd_task_row_set_active (GTD_TASK_ROW (row), TRUE);

          gtd_task_row_set_markup_renderer (GTD_TASK_ROW (row), priv->renderer);
        }
      else
        gtd_new_task_row_set_active (GTD_NEW_TASK_ROW (row), TRUE);

      gtk_widget_grab_focus (row);
    }
}

/*
 * Auxiliary function to iterate through a list of subtasks
 */
static void
iterate_subtasks (GtdTaskListView    *self,
                  GtdTask            *task,
                  IterateSubtaskFunc  func,
                  gboolean            depth_first)
{
  GtdTask *aux;
  GQueue *queue;

  aux = task;
  queue = g_queue_new ();

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
        {
          if (depth_first)
            g_queue_push_head (queue, l->data);
          else
            g_queue_push_tail (queue, l->data);
        }

      g_clear_pointer (&subtasks, g_list_free);

      aux = g_queue_pop_head (queue);
    }
  while (aux);

  g_clear_pointer (&queue, g_queue_free);
}

static gboolean
real_save_task (GtdTaskListView *self,
                GtdTask         *task)
{
  GtdTaskListViewPrivate *priv;
  GtdTaskList *list;

  priv = self->priv;
  list = gtd_task_get_list (task);

  /*
   * This will emit GtdTaskList::task-added and we'll readd
   * to the list.
   */
  gtd_task_list_save_task (list, task);

  if (priv->task_list != list && priv->task_list)
    gtd_task_list_save_task (priv->task_list, task);

  return TRUE;
}

static inline gboolean
real_remove_task (GtdTaskListView *self,
                  GtdTask         *task)
{
  gtd_manager_remove_task (gtd_manager_get_default (), task);

  return TRUE;
}

static inline gboolean
remove_task_from_list (GtdTaskListView *self,
                       GtdTask         *task)
{
  GtdTaskListViewPrivate *priv;
  GtdTaskList *list;

  priv = self->priv;
  list = gtd_task_get_list (task);

  /* Remove the task from the list */
  gtd_task_list_remove_task (list, task);

  /*
   * When we're dealing with the special lists (Today & Scheduled),
   * the task's list is different from the current list. We want to
   * remove the task from ~both~ lists.
   */
  if (priv->task_list != list && priv->task_list)
    gtd_task_list_remove_task (priv->task_list, task);

  gtd_task_list_view__remove_row_for_task (self, task);

  return TRUE;
}

static void
remove_task_action (GtdNotification *notification,
                    gpointer         user_data)
{
  RemoveTaskData *data;
  GtdTask *task;

  data = user_data;
  task = data->task;

  if (gtd_task_get_parent (task))
    gtd_task_remove_subtask (gtd_task_get_parent (task), task);

  /* Remove the subtasks recursively */
  iterate_subtasks (data->view,
                    data->task,
                    real_remove_task,
                    FALSE);

  g_clear_pointer (&data, g_free);
}

static void
undo_remove_task_action (GtdNotification *notification,
                         gpointer         user_data)
{
  RemoveTaskData *data = user_data;

  /* Save the subtasks recursively */
  iterate_subtasks (data->view,
                    data->task,
                    real_save_task,
                    FALSE);

  g_free (data);
}

/*
 * Default sorting functions
 */
static gint
compare_task_rows (GtkListBoxRow *row1,
                   GtkListBoxRow *row2)
{
  if (GTD_IS_NEW_TASK_ROW (row1))
    {
      return 1;
    }
  else if (GTD_IS_NEW_TASK_ROW (row2))
    {
      return -1;
    }
  else
    {
      return gtd_task_compare (gtd_task_row_get_task (GTD_TASK_ROW (row1)),
                               gtd_task_row_get_task (GTD_TASK_ROW (row2)));
    }
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
gtd_task_list_view__listbox_sort_func (GtkListBoxRow *row1,
                                       GtkListBoxRow *row2,
                                       gpointer       user_data)
{
  /* Automagically manage the DnD row */
  if (GTD_IS_DND_ROW (row1) || GTD_IS_DND_ROW (row2))
    return compare_dnd_rows (row1, row2);

  return compare_task_rows (row1, row2);
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
{  GtdTask *row1_task;
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
internal_sort_func (GtkListBoxRow   *a,
                    GtkListBoxRow   *b,
                    GtdTaskListView *view)
{
  if (!view->priv->sort_func)
    return 0;

  if (GTD_IS_DND_ROW (a) || GTD_IS_DND_ROW (b))
    return internal_compare_dnd_rows (view, a, b);

  return internal_compare_task_rows (view, a, b);
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

static void
gtd_task_list_view__clear_completed_tasks (GSimpleAction *simple,
                                           GVariant      *parameter,
                                           gpointer       user_data)
{
  GtdTaskListView *view;
  GList *tasks;
  GList *l;

  view = GTD_TASK_LIST_VIEW (user_data);
  tasks = gtd_task_list_view_get_list (view);

  for (l = tasks; l != NULL; l = l->next)
    {
      if (gtd_task_get_complete (l->data))
        {
          GtdTaskList *list;

          list = gtd_task_get_list (l->data);

          gtd_task_list_remove_task (list, l->data);
          gtd_manager_remove_task (gtd_manager_get_default (), l->data);
        }
    }

  gtd_task_list_view__update_done_label (view);

  g_list_free (tasks);
}

static void
gtd_task_list_view__update_empty_state (GtdTaskListView *view)
{
  GtdTaskListViewPrivate *priv;
  gboolean is_empty;
  GList *tasks;
  GList *l;

  g_return_if_fail (GTD_IS_TASK_LIST_VIEW (view));

  priv = view->priv;
  is_empty = TRUE;
  tasks = gtd_task_list_view_get_list (view);

  for (l = tasks; l != NULL; l = l->next)
    {
      if (priv->show_completed || !gtd_task_get_complete (l->data))
        {
          is_empty = FALSE;
          break;
        }
    }

  gtk_widget_set_visible (priv->empty_box, is_empty);
  gtd_empty_list_widget_set_is_empty (GTD_EMPTY_LIST_WIDGET (priv->empty_box),
                                      priv->complete_tasks == 0);

  /*
   * If there are visible tasks, we visually separate the New Task row by
   * increasing the top margin to 12px.
   */
  gtk_widget_set_margin_top (GTK_WIDGET (priv->new_task_row), is_empty ? 3 : 12);

  g_list_free (tasks);
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
remove_task_cb (GtdTaskRow      *row,
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
  iterate_subtasks (self, task, remove_task_from_list, FALSE);

  /*
   * Reset the DnD row, to avoid getting into an inconsistent state where
   * the DnD row points to a row that is not present anymore.
   */
  gtd_dnd_row_set_row_above (GTD_DND_ROW (priv->dnd_row), NULL);

  /* Notify about the removal */
  notification = gtd_notification_new (text, 7500.0);

  gtd_notification_set_primary_action (notification,
                                       (GtdNotificationActionFunc) remove_task_action,
                                       data);

  gtd_notification_set_secondary_action (notification,
                                         _("Undo"),
                                         (GtdNotificationActionFunc) undo_remove_task_action,
                                         data);

  gtd_window_notify (window, notification);

  /* Clear the active row */
  set_active_row (self, NULL);

  g_clear_pointer (&text, g_free);

out:
  g_clear_pointer (&subtasks, g_list_free);
}

static void
gtd_task_list_view__color_changed (GtdTaskListView *self)
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

  gtk_css_provider_load_from_data (priv->color_provider,
                                   parsed_css,
                                   -1,
                                   NULL);

  update_font_color (self);

  g_free (color_str);
}

static void
gtd_task_list_view__update_done_label (GtdTaskListView *view)
{
  gchar *new_label;

  g_return_if_fail (GTD_IS_TASK_LIST_VIEW (view));

  gtk_revealer_set_reveal_child (GTK_REVEALER (view->priv->revealer), view->priv->complete_tasks > 0);

  if (view->priv->complete_tasks == 0)
    {
      new_label = g_strdup_printf ("%s", _("Done"));
    }
  else
    {
      new_label = g_strdup_printf ("%s (%d)",
                                   _("Done"),
                                   view->priv->complete_tasks);
    }

  gtk_label_set_label (view->priv->done_label, new_label);

  g_free (new_label);
}

static gboolean
can_toggle_show_completed (GtdTaskListView *view)
{
  view->priv->can_toggle = TRUE;
  return G_SOURCE_REMOVE;
}

static void
gtd_task_list_view__done_button_clicked (GtkButton *button,
                                         gpointer   user_data)
{
  GtdTaskListView *view = GTD_TASK_LIST_VIEW (user_data);
  gboolean show_completed;

  g_return_if_fail (GTD_IS_TASK_LIST_VIEW (view));

  if (!view->priv->can_toggle)
    return;

  /*
   * The can_toggle bitfield is needed because the user
   * can click mindlessly the Done button, while the row
   * animations are not finished. While the animation is
   * running, we ignore other clicks.
   */
  view->priv->can_toggle = FALSE;

  show_completed = view->priv->show_completed;

  gtd_task_list_view_set_show_completed (view, !show_completed);

  g_timeout_add (205,
                 (GSourceFunc) can_toggle_show_completed,
                 user_data);
}

static void
task_row_entered_cb (GtdTaskListView *self,
                     GtdTaskRow      *row)
{
  set_active_row (self, GTK_WIDGET (row));
}

static void
task_row_exited_cb (GtdTaskListView *self,
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
listbox_row_activated (GtkListBox      *listbox,
                       GtkListBox      *row,
                       GtdTaskListView *self)
{
  if (!GTD_IS_TASK_ROW (row))
    return;

  /* Toggle the row */
  if (gtd_task_row_get_active (GTD_TASK_ROW (row)))
    set_active_row (self, NULL);
  else
    set_active_row (self, GTK_WIDGET (row));
}

static void
insert_task (GtdTaskListView *self,
             GtdTask         *task)
{
  GtdTaskListViewPrivate *priv = self->priv;
  GtkWidget *new_row;

  new_row = gtd_task_row_new (task);

  g_object_bind_property (self,
                          "handle-subtasks",
                          new_row,
                          "handle-subtasks",
                          G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

  gtd_task_row_set_list_name_visible (GTD_TASK_ROW (new_row), priv->show_list_name);
  gtd_task_row_set_due_date_visible (GTD_TASK_ROW (new_row), priv->show_due_date);

  g_signal_connect_swapped (new_row,
                            "enter",
                            G_CALLBACK (task_row_entered_cb),
                            self);

  g_signal_connect_swapped (new_row,
                            "exit",
                            G_CALLBACK (task_row_exited_cb),
                            self);

  g_signal_connect (new_row,
                    "remove-task",
                    G_CALLBACK (remove_task_cb),
                    self);

  gtk_list_box_insert (priv->listbox, new_row, 0);

  /*
   * Setup a sizegroup to let all the tasklist labels have
   * the same width.
   */
  gtd_task_row_set_sizegroups (GTD_TASK_ROW (new_row),
                               priv->tasklist_name_sizegroup,
                               priv->due_date_sizegroup);

  gtd_task_row_reveal (GTD_TASK_ROW (new_row));
}

static void
destroy_task_row (GtdTaskListView *self,
                  GtdTaskRow      *row)
{
  g_signal_handlers_disconnect_by_func (row, task_row_entered_cb, self);
  g_signal_handlers_disconnect_by_func (row, task_row_exited_cb, self);

  if (GTK_WIDGET (row) == self->priv->active_row)
    set_active_row (self, NULL);

  gtd_task_row_destroy (row);
}

static void
remove_task (GtdTaskListView *view,
             GtdTask         *task)
{
  GtdTaskListViewPrivate *priv = view->priv;
  GList *children;
  GList *l;

  children = gtk_container_get_children (GTK_CONTAINER (view->priv->listbox));

  for (l = children; l != NULL; l = l->next)
    {
      if (!GTD_IS_TASK_ROW (l->data))
        continue;

      if (l->data != priv->new_task_row &&
          gtd_task_row_get_task (l->data) == task)
        {
          if (gtd_task_get_complete (task))
            priv->complete_tasks--;

          g_signal_handlers_disconnect_by_func (task,
                                                task_completed_cb,
                                                view);

          destroy_task_row (view, l->data);
          break;
        }
    }

  gtk_revealer_set_reveal_child (priv->revealer, FALSE);

  g_list_free (children);
}

static inline gboolean
has_complete_parent (GtdTask *task)
{
  GtdTask *parent = gtd_task_get_parent (task);

  while (parent)
    {
      if (gtd_task_get_complete (parent))
        return TRUE;

      parent = gtd_task_get_parent (parent);
    }

  return FALSE;
}

static void
gtd_task_list_view__add_task (GtdTaskListView *view,
                              GtdTask         *task)
{
  GtdTaskListViewPrivate *priv = view->priv;

  g_return_if_fail (GTD_IS_TASK_LIST_VIEW (view));
  g_return_if_fail (GTD_IS_TASK (task));

  if (!priv->show_completed &&
      (gtd_task_get_complete (task) || has_complete_parent (task)))
    {
      return;
    }

  insert_task (view, task);

  /* Check if it should show the empty state */
  gtd_task_list_view__update_empty_state (view);
}

static void
gtd_task_list_view__remove_row_for_task (GtdTaskListView *view,
                                         GtdTask         *task)
{
  GtdTaskListViewPrivate *priv = view->priv;
  GList *children;
  GList *l;

  g_return_if_fail (GTD_IS_TASK_LIST_VIEW (view));
  g_return_if_fail (GTD_IS_TASK (task));

  children = gtk_container_get_children (GTK_CONTAINER (priv->listbox));

  for (l = children; l != NULL; l = l->next)
    {
      if (!GTD_IS_TASK_ROW (l->data))
        continue;

      if (gtd_task_row_get_task (l->data) == task)
        {
          destroy_task_row (view, l->data);
          break;
        }
    }

  g_list_free (children);
}

static void
gtd_task_list_view__remove_task (GtdTaskListView *view,
                                 GtdTask         *task)
{
  /* Remove the correspondent row */
  gtd_task_list_view__remove_row_for_task (view, task);

  /* Update the "Done" label */
  if (gtd_task_get_complete (task))
    {
      view->priv->complete_tasks--;
      gtd_task_list_view__update_done_label (view);
    }

  /* Check if it should show the empty state */
  gtd_task_list_view__update_empty_state (view);
}

static inline gboolean
remove_subtasks_of_completed_task (GtdTaskListView *self,
                                   GtdTask         *task)
{
  gtd_task_list_view__remove_row_for_task (self, task);
  return TRUE;
}

static inline gboolean
add_subtasks_of_task (GtdTaskListView *self,
                      GtdTask         *task)
{
  gtd_task_list_view__add_task (self, task);
  return TRUE;
}

static void
task_completed_cb (GtdTask         *task,
                   GParamSpec      *spec,
                   GtdTaskListView *self)
{
  GtdTaskListViewPrivate *priv = gtd_task_list_view_get_instance_private (self);
  gboolean task_complete;

  task_complete = gtd_task_get_complete (task);

  gtd_manager_update_task (gtd_manager_get_default (), task);
  real_save_task (self, task);

  if (task_complete)
    priv->complete_tasks++;
  else
    priv->complete_tasks--;

  if (!priv->show_completed)
    {
      IterateSubtaskFunc func;

      func = task_complete ? remove_subtasks_of_completed_task : add_subtasks_of_task;

      iterate_subtasks (self, task, func, FALSE);
    }

  gtk_list_box_invalidate_sort (priv->listbox);

  gtd_task_list_view__update_empty_state (self);
  gtd_task_list_view__update_done_label (self);
}

static void
gtd_task_list_view__task_added (GtdTaskList     *list,
                                GtdTask         *task,
                                GtdTaskListView *self)
{
  GtdTaskListViewPrivate *priv = gtd_task_list_view_get_instance_private (self);

  /* Add the new task to the list */
  gtd_task_list_view__add_task (self, task);

  /* Also add to the list of current tasks */
  priv->list = g_list_prepend (priv->list, task);

  g_signal_connect (task,
                    "notify::complete",
                    G_CALLBACK (task_completed_cb),
                    self);
}

static void
create_task_cb (GtdTaskRow  *row,
                GtdTask     *task,
                GtdTaskList *list,
                gpointer     user_data)
{
  GtdTaskListViewPrivate *priv;

  priv = GTD_TASK_LIST_VIEW (user_data)->priv;

  /* If there's a task list set, always go for it */
  if (priv->task_list)
    list = priv->task_list;

  /*
   * If there is no current list set, use the default list from the
   * default provider.
   */
  if (!list)
    {
      GtdProvider *provider;

      provider = gtd_manager_get_default_provider (gtd_manager_get_default ());
      list = gtd_provider_get_default_task_list (provider);
    }

  g_return_if_fail (GTD_IS_TASK_LIST (list));

  /*
   * Newly created tasks are not aware of
   * their parent lists.
   */
  gtd_task_set_list (task, list);

  if (priv->default_date)
    gtd_task_set_due_date (task, priv->default_date);

  gtd_task_list_save_task (list, task);
  gtd_manager_create_task (gtd_manager_get_default (), task);
}

static void
gtd_task_list_view_finalize (GObject *object)
{
  GtdTaskListViewPrivate *priv = GTD_TASK_LIST_VIEW (object)->priv;

  g_clear_pointer (&priv->default_date, g_date_time_unref);
  g_clear_pointer (&priv->list, g_list_free);

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

    case PROP_SHOW_COMPLETED:
      g_value_set_boolean (value, self->priv->show_completed);
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

    case PROP_SHOW_COMPLETED:
      gtd_task_list_view_set_show_completed (self, g_value_get_boolean (value));
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

  gtk_style_context_add_provider (gtk_widget_get_style_context (self->priv->viewport),
                                  GTK_STYLE_PROVIDER (self->priv->color_provider),
                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 2);

  /* show a nifty separator between lines */
  gtk_list_box_set_sort_func (self->priv->listbox,
                              (GtkListBoxSortFunc) gtd_task_list_view__listbox_sort_func,
                              NULL,
                              NULL);
}

/*
 * Listbox Drag n' Drop functions
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
                    GdkDragContext  *context,
                    guint            time,
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
                     GdkDragContext  *context,
                     gint             x,
                     gint             y,
                     guint            time,
                     GtdTaskListView *self)
{
  GtdTaskListViewPrivate *priv;
  GtkListBoxRow *hovered_row;
  GtkListBoxRow *source_row;
  GtkListBoxRow *task_row;
  GtkListBoxRow *row_above_dnd;
  GtkWidget *source_widget;
  gboolean success;
  gint row_x, row_y, row_height;

  priv = gtd_task_list_view_get_instance_private (self);
  source_widget = gtk_drag_get_source_widget (context);
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

  gdk_drag_status (context, GDK_ACTION_MOVE, time);

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
  success = gtd_dnd_row_drag_motion (GTK_WIDGET (priv->dnd_row), context, x, y, time);

  check_dnd_scroll (self, FALSE, y);

  return success;

fail:
  return FALSE;
}

static gboolean
listbox_drag_drop (GtkWidget       *widget,
                   GdkDragContext  *context,
                   gint             x,
                   gint             y,
                   guint            time,
                   GtdTaskListView *self)
{
  GtdTaskListViewPrivate *priv;
  gboolean success;

  priv = gtd_task_list_view_get_instance_private (self);
  success = gtd_dnd_row_drag_drop (GTK_WIDGET (priv->dnd_row), context, x, y, time);

  check_dnd_scroll (self, TRUE, -1);

  gtk_drag_finish (context, success, TRUE, time);

  return success;
}

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
   * GtdTaskListView::show-completed:
   *
   * Whether completed tasks are shown.
   */
  g_object_class_install_property (
        object_class,
        PROP_SHOW_COMPLETED,
        g_param_spec_boolean ("show-completed",
                              "Whether completed tasks are shown",
                              "Whether completed tasks are visible or not",
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
  gtk_widget_class_bind_template_child_private (widget_class, GtdTaskListView, empty_box);
  gtk_widget_class_bind_template_child_private (widget_class, GtdTaskListView, listbox);
  gtk_widget_class_bind_template_child_private (widget_class, GtdTaskListView, revealer);
  gtk_widget_class_bind_template_child_private (widget_class, GtdTaskListView, done_image);
  gtk_widget_class_bind_template_child_private (widget_class, GtdTaskListView, done_label);
  gtk_widget_class_bind_template_child_private (widget_class, GtdTaskListView, new_task_row);
  gtk_widget_class_bind_template_child_private (widget_class, GtdTaskListView, tasklist_name_sizegroup);
  gtk_widget_class_bind_template_child_private (widget_class, GtdTaskListView, viewport);
  gtk_widget_class_bind_template_child_private (widget_class, GtdTaskListView, scrolled_window);

  gtk_widget_class_bind_template_callback (widget_class, create_task_cb);
  gtk_widget_class_bind_template_callback (widget_class, gtd_task_list_view__done_button_clicked);
  gtk_widget_class_bind_template_callback (widget_class, listbox_drag_drop);
  gtk_widget_class_bind_template_callback (widget_class, listbox_drag_leave);
  gtk_widget_class_bind_template_callback (widget_class, listbox_drag_motion);
  gtk_widget_class_bind_template_callback (widget_class, listbox_row_activated);
  gtk_widget_class_bind_template_callback (widget_class, task_row_entered_cb);
  gtk_widget_class_bind_template_callback (widget_class, task_row_exited_cb);

  gtk_widget_class_set_css_name (widget_class, "tasklistview");
}

static void
gtd_task_list_view_init (GtdTaskListView *self)
{
  self->priv = gtd_task_list_view_get_instance_private (self);
  self->priv->can_toggle = TRUE;
  self->priv->handle_subtasks = TRUE;
  self->priv->show_due_date = TRUE;

  gtk_widget_init_template (GTK_WIDGET (self));

  set_active_row (self, GTK_WIDGET (self->priv->new_task_row));

  gtk_drag_dest_set (GTK_WIDGET (self->priv->listbox),
                     0,
                     NULL,
                     0,
                     GDK_ACTION_MOVE);

  self->priv->renderer = gtd_markup_renderer_new ();
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
    return gtd_task_list_get_tasks (view->priv->task_list);
  else if (view->priv->list)
    return g_list_copy (view->priv->list);
  else
    return NULL;
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
  GtdTaskListViewPrivate *priv;
  GList *l, *old_list;

  g_return_if_fail (GTD_IS_TASK_LIST_VIEW (view));

  priv = view->priv;
  old_list = priv->list;

  /* Reset the DnD parent row */
  gtd_dnd_row_set_row_above (GTD_DND_ROW (priv->dnd_row), NULL);

  /* Remove the tasks that are in the current list, but not in the new list */
  for (l = old_list; l != NULL; l = l->next)
    {
      if (!g_list_find (list, l->data))
        remove_task (view, l->data);
    }

  /* Add the tasks that are in the new list, but not in the current list */
  for (l = list; l != NULL; l = l->next)
    {
      if (g_list_find (old_list, l->data))
        continue;

      gtd_task_list_view__add_task (view, l->data);

      g_signal_connect (l->data,
                        "notify::complete",
                        G_CALLBACK (task_completed_cb),
                        view);
    }

  g_list_free (old_list);
  priv->list = g_list_copy (list);

  /* Update the completed tasks counter */
  priv->complete_tasks = 0;

  for (l = list; l != NULL; l = l->next)
    priv->complete_tasks += gtd_task_get_complete (l->data);

  gtd_task_list_view__update_done_label (view);

  /* Check if it should show the empty state */
  gtd_task_list_view__update_empty_state (view);
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
  GtdTaskListViewPrivate *priv = view->priv;
  GdkRGBA *color;
  gchar *color_str;
  gchar *parsed_css;
  GList *task_list;

  g_return_if_fail (GTD_IS_TASK_LIST_VIEW (view));

  if (priv->task_list == list)
    return;

  gtd_new_task_row_set_show_list_selector (GTD_NEW_TASK_ROW (priv->new_task_row), list == NULL);

  /*
   * Disconnect the old GtdTaskList signals.
   */
  if (priv->task_list)
    {
      g_signal_handlers_disconnect_by_func (priv->task_list,
                                            gtd_task_list_view__task_added,
                                            view);
      g_signal_handlers_disconnect_by_func (priv->task_list,
                                            gtd_task_list_view__color_changed,
                                            view);
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

  g_debug ("setting style for provider: %s", parsed_css);

  gtk_css_provider_load_from_data (priv->color_provider,
                                   parsed_css,
                                   -1,
                                   NULL);

  g_free (parsed_css);
  gdk_rgba_free (color);
  g_free (color_str);

  update_font_color (view);

  /* Add the tasks from the list */
  task_list = gtd_task_list_get_tasks (list);

  gtd_task_list_view_set_list (view, task_list);

  g_list_free (task_list);

  g_signal_connect (list,
                    "task-added",
                    G_CALLBACK (gtd_task_list_view__task_added),
                    view);
  g_signal_connect_swapped (list,
                            "task-removed",
                            G_CALLBACK (gtd_task_list_view__remove_task),
                            view);
  g_signal_connect_swapped (list,
                            "notify::color",
                            G_CALLBACK (gtd_task_list_view__color_changed),
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
 * gtd_task_list_view_get_show_completed:
 * @view: a #GtdTaskListView
 *
 * Returns %TRUE if completed tasks are visible, %FALSE otherwise.
 *
 * Returns: %TRUE if completed tasks are visible, %FALSE if they are hidden
 */
gboolean
gtd_task_list_view_get_show_completed (GtdTaskListView *view)
{
  g_return_val_if_fail (GTD_IS_TASK_LIST_VIEW (view), FALSE);

  return view->priv->show_completed;
}

/**
 * gtd_task_list_view_set_show_completed:
 * @view: a #GtdTaskListView
 * @show_completed: %TRUE to show completed tasks, %FALSE to hide them
 *
 * Sets the ::show-completed property to @show_completed.
 */
void
gtd_task_list_view_set_show_completed (GtdTaskListView *view,
                                       gboolean         show_completed)
{
  GtdTaskListViewPrivate *priv = view->priv;

  g_return_if_fail (GTD_IS_TASK_LIST_VIEW (view));

  if (priv->show_completed != show_completed)
    {

      priv->show_completed = show_completed;

      gtk_image_set_from_icon_name (view->priv->done_image,
                                    show_completed ? "zoom-out-symbolic" : "zoom-in-symbolic",
                                    GTK_ICON_SIZE_BUTTON);


      /* insert or remove list rows */
      if (show_completed)
        {
          GList *list_of_tasks;
          GList *l;

          list_of_tasks = gtd_task_list_view_get_list (view);

          for (l = list_of_tasks; l != NULL; l = l->next)
            {
              /*
               * Consider that not-complete tasks, and non-complete tasks with a non-complete
               * parent, are already present.
               */
              if (!gtd_task_get_complete (l->data) && !has_complete_parent (l->data))
                continue;

              insert_task (view, l->data);
            }

            g_list_free (list_of_tasks);
        }
      else
        {
          GList *children;
          GList *l;

          children = gtk_container_get_children (GTK_CONTAINER (priv->listbox));

          for (l = children; l != NULL; l = l->next)
            {
              GtdTask *task;

              if (!GTD_IS_TASK_ROW (l->data))
                continue;

              task = gtd_task_row_get_task (l->data);

              /* Remove completed tasks, and also tasks with a completed parent */
              if (gtd_task_get_complete (task) || has_complete_parent (task))
                destroy_task_row (view, l->data);
            }

          g_list_free (children);
        }

      /* Check if it should show the empty state */
      gtd_task_list_view__update_empty_state (view);

      g_object_notify (G_OBJECT (view), "show-completed");
    }
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
                                  (GtkListBoxSortFunc) internal_sort_func,
                                  view,
                                  NULL);
    }
  else
    {
      priv->sort_func = NULL;
      priv->sort_user_data = NULL;

      gtk_list_box_set_sort_func (priv->listbox,
                                  (GtkListBoxSortFunc) gtd_task_list_view__listbox_sort_func,
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

  if (priv->default_date != default_date)
    {
      g_clear_pointer (&priv->default_date, g_date_time_unref);
      priv->default_date = default_date ? g_date_time_ref (default_date) : NULL;

      gtk_list_box_invalidate_headers (priv->listbox);
      gtk_list_box_invalidate_sort (priv->listbox);
    }
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

      gtd_task_list_view__color_changed (self);

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
