/* gtd-panel-today.c
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

#define G_LOG_DOMAIN "GtdPanelToday"

#include "gnome-todo.h"
#include "gtd-panel-today.h"

#include <glib/gi18n.h>

struct _GtdPanelToday
{
  GtkBox              parent;

  GtkWidget          *view;
  GMenu              *menu;
  GIcon              *icon;

  GListStore         *model;

  gint                day_change_callback_id;

  guint               number_of_tasks;
};

static void          gtd_panel_iface_init                        (GtdPanelInterface  *iface);

G_DEFINE_TYPE_EXTENDED (GtdPanelToday, gtd_panel_today, GTK_TYPE_BOX,
                        0,
                        G_IMPLEMENT_INTERFACE (GTD_TYPE_PANEL,
                                               gtd_panel_iface_init))


#define GTD_PANEL_TODAY_NAME     "panel-today"
#define GTD_PANEL_TODAY_PRIORITY 1000

enum
{
  PROP_0,
  PROP_ICON,
  PROP_MENU,
  PROP_NAME,
  PROP_PRIORITY,
  PROP_SUBTITLE,
  PROP_TITLE,
  N_PROPS
};


/*
 * Auxiliary methods
 */

static gboolean
is_overdue (GDateTime *today,
            GDateTime *dt)
{
  if (!dt)
    return FALSE;

  if (g_date_time_get_year (dt) > g_date_time_get_year (today))
    return FALSE;

  if (g_date_time_get_year (dt) < g_date_time_get_year (today))
    return TRUE;

  return g_date_time_get_day_of_year (dt) < g_date_time_get_day_of_year (today);
}

static gboolean
is_today (GDateTime *today,
          GDateTime *dt)
{
  if (!dt)
    return FALSE;

  if (g_date_time_get_year (dt) == g_date_time_get_year (today) &&
      g_date_time_get_day_of_year (dt) == g_date_time_get_day_of_year (today))
    {
      return TRUE;
    }

  return FALSE;
}

static inline gboolean
should_be_added (GDateTime *today,
                 GDateTime *dt)
{
  return is_today (today, dt) || is_overdue (today, dt);
}

static gint
sort_func (gconstpointer a,
           gconstpointer b,
           gpointer      user_data)
{
  g_autoptr (GDateTime) dt1 = NULL;
  g_autoptr (GDateTime) dt2 = NULL;
  GtdTask *task1;
  GtdTask *task2;
  GDate dates[2];
  gint result;

  task1 = (GtdTask*) a;
  task2 = (GtdTask*) b;

  dt1 = gtd_task_get_due_date (task1);
  dt2 = gtd_task_get_due_date (task2);

  g_date_clear (dates, 2);

  g_date_set_dmy (&dates[0],
                  g_date_time_get_day_of_month (dt1),
                  g_date_time_get_month (dt1),
                  g_date_time_get_year (dt1));

  g_date_set_dmy (&dates[1],
                  g_date_time_get_day_of_month (dt2),
                  g_date_time_get_month (dt2),
                  g_date_time_get_year (dt2));

  result = g_date_days_between (&dates[1], &dates[0]);

  if (result != 0)
    return result;

  return gtd_task_compare (task1, task2);
}

static void
update_tasks (GtdPanelToday *panel)
{
  g_autoptr (GDateTime) now = NULL;
  g_autoptr (GList) tasklists = NULL;
  GtdManager *manager;
  GList *l;
  guint number_of_tasks;

  now = g_date_time_new_now_local ();
  manager = gtd_manager_get_default ();
  tasklists = gtd_manager_get_task_lists (manager);
  number_of_tasks = 0;

  g_list_store_remove_all (panel->model);

  /* Recount tasks */
  for (l = tasklists; l != NULL; l = l->next)
    {
      guint i;

      for (i = 0; i < g_list_model_get_n_items (l->data); i++)
        {
          g_autoptr (GDateTime) task_dt = NULL;
          GtdTask *task;

          task = g_list_model_get_item (l->data, i);
          task_dt = gtd_task_get_due_date (task);

          /* Ignore completed tasks, and tasks in the future */
          if (gtd_task_get_complete (task) || !should_be_added (now, task_dt))
            continue;

          g_list_store_insert_sorted (panel->model, task, sort_func, NULL);

          if (!gtd_task_get_complete (task))
            number_of_tasks++;
        }
    }

  /* Add the tasks to the view */
  gtd_task_list_view_set_default_date (GTD_TASK_LIST_VIEW (panel->view), now);

  if (number_of_tasks != panel->number_of_tasks)
    {
      panel->number_of_tasks = number_of_tasks;
      g_object_notify (G_OBJECT (panel), "subtitle");
    }
}

static GtkWidget*
create_label (const gchar *text,
              gboolean     overdue)
{
  GtkStyleContext *context;
  GtkWidget *label;

  label = g_object_new (GTK_TYPE_LABEL,
                        "visible", TRUE,
                        "label", text,
                        "margin", 6,
                        "margin-top", overdue ? 6 : 18,
                        "xalign", 0.0,
                        "hexpand", TRUE,
                        NULL);

  context = gtk_widget_get_style_context (label);
  gtk_style_context_add_class (context, overdue ? "date-overdue" : "date-scheduled");

  return label;
}

static void
custom_header_func (GtkListBoxRow *row,
                    GtdTask       *task,
                    GtkListBoxRow *previous_row,
                    GtdTask       *previous_task,
                    gpointer       user_data)
{
  g_autoptr (GDateTime) now = NULL;
  g_autoptr (GDateTime) dt = NULL;
  GtkWidget *header = NULL;

  now = g_date_time_new_now_local ();
  dt = gtd_task_get_due_date (task);

  /* Only show a header if the we have overdue tasks */
  if (!previous_row && is_overdue (now, dt))
    {
      header = create_label (_("Overdue"), TRUE);
    }
  else if (previous_row)
    {
      g_autoptr (GDateTime) previous_dt = NULL;

      previous_dt = gtd_task_get_due_date (previous_task);

      if (is_today (now, dt) != is_today (now, previous_dt))
        header = create_label (_("Today"), FALSE);
    }

  gtk_list_box_row_set_header (row, header);
}


/*
 * Callbacks
 */

static void
on_manager_task_list_changed_cb (GtdPanelToday *self)
{
  update_tasks (self);
}

/*
 * GtdPanel iface
 */

static const gchar*
gtd_panel_today_get_panel_name (GtdPanel *panel)
{
  return GTD_PANEL_TODAY_NAME;
}

static const gchar*
gtd_panel_today_get_panel_title (GtdPanel *panel)
{
  return _("Today");
}

static GList*
gtd_panel_today_get_header_widgets (GtdPanel *panel)
{
  return NULL;
}

static const GMenu*
gtd_panel_today_get_menu (GtdPanel *panel)
{
  return GTD_PANEL_TODAY (panel)->menu;
}

static GIcon*
gtd_panel_today_get_icon (GtdPanel *panel)
{
  return g_object_ref (GTD_PANEL_TODAY (panel)->icon);
}

static guint32
gtd_panel_today_get_priority (GtdPanel *panel)
{
  return GTD_PANEL_TODAY_PRIORITY;
}

static gchar*
gtd_panel_today_get_subtitle (GtdPanel *panel)
{
  GtdPanelToday *self = GTD_PANEL_TODAY (panel);

  return g_strdup_printf ("%d", self->number_of_tasks);
}

static void
gtd_panel_iface_init (GtdPanelInterface *iface)
{
  iface->get_panel_name = gtd_panel_today_get_panel_name;
  iface->get_panel_title = gtd_panel_today_get_panel_title;
  iface->get_header_widgets = gtd_panel_today_get_header_widgets;
  iface->get_menu = gtd_panel_today_get_menu;
  iface->get_icon = gtd_panel_today_get_icon;
  iface->get_priority = gtd_panel_today_get_priority;
  iface->get_subtitle = gtd_panel_today_get_subtitle;
}

static void
gtd_panel_today_finalize (GObject *object)
{
  GtdPanelToday *self = (GtdPanelToday *)object;

  g_clear_object (&self->icon);
  g_clear_object (&self->menu);
  g_clear_object (&self->model);

  G_OBJECT_CLASS (gtd_panel_today_parent_class)->finalize (object);
}

static void
gtd_panel_today_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  GtdPanelToday *self = GTD_PANEL_TODAY (object);

  switch (prop_id)
    {
    case PROP_ICON:
      g_value_set_object (value, self->icon);
      break;

    case PROP_MENU:
      g_value_set_object (value, NULL);
      break;

    case PROP_NAME:
      g_value_set_string (value, GTD_PANEL_TODAY_NAME);
      break;

    case PROP_PRIORITY:
      g_value_set_uint (value, GTD_PANEL_TODAY_PRIORITY);
      break;

    case PROP_SUBTITLE:
      g_value_take_string (value, gtd_panel_get_subtitle (GTD_PANEL (self)));
      break;

    case PROP_TITLE:
      g_value_set_string (value, gtd_panel_get_panel_title (GTD_PANEL (self)));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_panel_today_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
gtd_panel_today_class_init (GtdPanelTodayClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gtd_panel_today_finalize;
  object_class->get_property = gtd_panel_today_get_property;
  object_class->set_property = gtd_panel_today_set_property;

  g_object_class_override_property (object_class, PROP_ICON, "icon");
  g_object_class_override_property (object_class, PROP_MENU, "menu");
  g_object_class_override_property (object_class, PROP_NAME, "name");
  g_object_class_override_property (object_class, PROP_PRIORITY, "priority");
  g_object_class_override_property (object_class, PROP_SUBTITLE, "subtitle");
  g_object_class_override_property (object_class, PROP_TITLE, "title");
}

static void
gtd_panel_today_init (GtdPanelToday *self)
{
  g_autoptr (GDateTime) now = NULL;
  GtdManager *manager;

  self->icon = g_themed_icon_new ("view-tasks-today-symbolic");
  self->model = g_list_store_new (GTD_TYPE_TASK);

  /* Connect to GtdManager::list-* signals to update the title */
  manager = gtd_manager_get_default ();
  now = g_date_time_new_now_local ();

  g_signal_connect_object (manager,
                           "list-added",
                           G_CALLBACK (on_manager_task_list_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (manager,
                           "list-removed",
                           G_CALLBACK (on_manager_task_list_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (manager,
                           "list-changed",
                           G_CALLBACK (on_manager_task_list_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);

  /* Menu */
  self->menu = g_menu_new ();
  g_menu_append (self->menu,
                 _("Clear completed tasks…"),
                 "list.clear-completed-tasks");

  /* The main view */
  self->view = gtd_task_list_view_new ();
  gtd_task_list_view_set_model (GTD_TASK_LIST_VIEW (self->view), G_LIST_MODEL (self->model));
  gtd_task_list_view_set_handle_subtasks (GTD_TASK_LIST_VIEW (self->view), FALSE);
  gtd_task_list_view_set_show_list_name (GTD_TASK_LIST_VIEW (self->view), TRUE);
  gtd_task_list_view_set_show_due_date (GTD_TASK_LIST_VIEW (self->view), FALSE);
  gtd_task_list_view_set_default_date (GTD_TASK_LIST_VIEW (self->view), now);
  gtd_task_list_view_set_header_func (GTD_TASK_LIST_VIEW (self->view), custom_header_func, self);

  gtk_widget_set_hexpand (self->view, TRUE);
  gtk_widget_set_vexpand (self->view, TRUE);
  gtk_container_add (GTK_CONTAINER (self), self->view);

  /* Start timer */
  g_signal_connect_object (gtd_manager_get_timer (manager),
                           "update",
                           G_CALLBACK (on_manager_task_list_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);
}

GtkWidget*
gtd_panel_today_new (void)
{
  return g_object_new (GTD_TYPE_PANEL_TODAY, NULL);
}
