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

  gint                day_change_callback_id;

  gchar              *title;
  guint               number_of_tasks;
  GList              *task_list;
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
  PROP_TITLE,
  N_PROPS
};

static gboolean
is_today (GDateTime *today,
          GDateTime *dt)
{
  if (!dt)
    return FALSE;

  if (g_date_time_get_year (dt) == g_date_time_get_year (today) &&
      g_date_time_get_month (dt) == g_date_time_get_month (today) &&
      g_date_time_get_day_of_month (dt) == g_date_time_get_day_of_month (today))
    {
      return TRUE;
    }

  return FALSE;
}

static void
gtd_panel_today_count_tasks (GtdPanelToday *panel)
{
  g_autoptr (GDateTime) now;
  GtdManager *manager;
  GList *tasklists;
  GList *l;
  guint number_of_tasks;

  now = g_date_time_new_now_local ();
  manager = gtd_manager_get_default ();
  tasklists = gtd_manager_get_task_lists (manager);
  number_of_tasks = 0;

  g_clear_pointer (&panel->task_list, g_list_free);

  /* Recount tasks */
  for (l = tasklists; l != NULL; l = l->next)
    {
      GList *tasks;
      GList *t;

      tasks = gtd_task_list_get_tasks (l->data);

      for (t = tasks; t != NULL; t = t->next)
        {
          GDateTime *task_dt;

          task_dt = gtd_task_get_due_date (t->data);

          if (!is_today (now, task_dt))
            {
              g_clear_pointer (&task_dt, g_date_time_unref);
              continue;
            }

          panel->task_list = g_list_prepend (panel->task_list, t->data);

          if (!gtd_task_get_complete (t->data))
            number_of_tasks++;

          g_clear_pointer (&task_dt, g_date_time_unref);
        }

      g_list_free (tasks);
    }

  /* Add the tasks to the view */
  gtd_task_list_view_set_list (GTD_TASK_LIST_VIEW (panel->view), panel->task_list);
  gtd_task_list_view_set_default_date (GTD_TASK_LIST_VIEW (panel->view), now);

  if (number_of_tasks != panel->number_of_tasks)
    {
      panel->number_of_tasks = number_of_tasks;

      /* Update title */
      g_clear_pointer (&panel->title, g_free);
      if (number_of_tasks == 0)
        {
          panel->title = g_strdup (_("Today"));
        }
      else
        {
          panel->title = g_strdup_printf ("%s (%d)",
                                          _("Today"),
                                          panel->number_of_tasks);
        }

      g_object_notify (G_OBJECT (panel), "title");
    }

  g_list_free (tasklists);
}

/**********************
 * GtdPanel iface init
 **********************/
static const gchar*
gtd_panel_today_get_panel_name (GtdPanel *panel)
{
  return GTD_PANEL_TODAY_NAME;
}

static const gchar*
gtd_panel_today_get_panel_title (GtdPanel *panel)
{
  return GTD_PANEL_TODAY (panel)->title;
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

static void
gtd_panel_iface_init (GtdPanelInterface *iface)
{
  iface->get_panel_name = gtd_panel_today_get_panel_name;
  iface->get_panel_title = gtd_panel_today_get_panel_title;
  iface->get_header_widgets = gtd_panel_today_get_header_widgets;
  iface->get_menu = gtd_panel_today_get_menu;
  iface->get_icon = gtd_panel_today_get_icon;
  iface->get_priority = gtd_panel_today_get_priority;
}

static void
gtd_panel_today_finalize (GObject *object)
{
  GtdPanelToday *self = (GtdPanelToday *)object;

  g_clear_object (&self->icon);
  g_clear_object (&self->menu);
  g_clear_pointer (&self->title, g_free);
  g_clear_pointer (&self->task_list, g_list_free);

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

    case PROP_TITLE:
      g_value_set_string (value, self->title);
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
  g_object_class_override_property (object_class, PROP_TITLE, "title");
}

static void
gtd_panel_today_init (GtdPanelToday *self)
{
  GtdManager *manager;
  GDateTime *now;

  self->icon = g_themed_icon_new ("daytime-sunset-symbolic");

  /* Connect to GtdManager::list-* signals to update the title */
  manager = gtd_manager_get_default ();
  now = g_date_time_new_now_local ();

  g_signal_connect_swapped (manager,
                            "list-added",
                            G_CALLBACK (gtd_panel_today_count_tasks),
                            self);

  g_signal_connect_swapped (manager,
                            "list-removed",
                            G_CALLBACK (gtd_panel_today_count_tasks),
                            self);

  g_signal_connect_swapped (manager,
                            "list-changed",
                            G_CALLBACK (gtd_panel_today_count_tasks),
                            self);

  /* Setup a title */
  self->title = g_strdup (_("Today"));

  /* Menu */
  self->menu = g_menu_new ();
  g_menu_append (self->menu,
                 _("Clear completed tasks…"),
                 "list.clear-completed-tasks");

  /* The main view */
  self->view = gtd_task_list_view_new ();
  gtd_task_list_view_set_handle_subtasks (GTD_TASK_LIST_VIEW (self->view), FALSE);
  gtd_task_list_view_set_show_list_name (GTD_TASK_LIST_VIEW (self->view), TRUE);
  gtd_task_list_view_set_show_due_date (GTD_TASK_LIST_VIEW (self->view), FALSE);
  gtd_task_list_view_set_default_date (GTD_TASK_LIST_VIEW (self->view), now);

  gtk_widget_set_hexpand (self->view, TRUE);
  gtk_widget_set_vexpand (self->view, TRUE);
  gtk_container_add (GTK_CONTAINER (self), self->view);

  gtk_widget_show_all (GTK_WIDGET (self));

  /* Start timer */
  g_signal_connect_swapped (gtd_manager_get_timer (manager),
                            "update",
                            G_CALLBACK (gtd_panel_today_count_tasks),
                            self);

  g_clear_pointer (&now, g_date_time_unref);
}

GtkWidget*
gtd_panel_today_new (void)
{
  return g_object_new (GTD_TYPE_PANEL_TODAY, NULL);
}
