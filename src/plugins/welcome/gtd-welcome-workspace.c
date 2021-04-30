/* gtd-welcome-workspace.c
 *
 * Copyright 2020 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "gtd-welcome-action-card.h"
#include "gtd-welcome-workspace.h"

#include <glib/gi18n.h>

struct _GtdWelcomeWorkspace
{
  GtdWidget           parent_instance;

  GtkFilterListModel *today_model;
  GListModel         *inbox_tasks_model;

  GtdWelcomeActionCard *inbox_card;
  GtdWelcomeActionCard *today_card;

  GtkLabel           *welcome_label;
};

static void          gtd_workspace_iface_init                    (GtdWorkspaceInterface  *iface);

G_DEFINE_TYPE_WITH_CODE (GtdWelcomeWorkspace, gtd_welcome_workspace, GTD_TYPE_WIDGET,
                         G_IMPLEMENT_INTERFACE (GTD_TYPE_WORKSPACE, gtd_workspace_iface_init))

enum
{
  PROP_0,
  PROP_ICON,
  PROP_TITLE,
  N_PROPS
};


/*
 * Auxiliary methods
 */

static void
update_welcome_label (GtdWelcomeWorkspace *self)
{
  g_autoptr (GDateTime) now = NULL;
  g_autofree gchar *text = NULL;
  gint hour;

  now = g_date_time_new_now_local ();
  hour = g_date_time_get_hour (now);

  if (hour >= 5 && hour < 12)
    text = g_strdup_printf (_("Good Morning, %s"), g_get_real_name ());
  else if (hour >= 12 && hour < 18)
    text = g_strdup_printf (_("Good Afternoon, %s"), g_get_real_name ());
  else
    text = g_strdup_printf (_("Good Evening, %s"), g_get_real_name ());

  gtk_label_set_text (self->welcome_label, text);
}

static void
update_inbox_counter (GtdWelcomeWorkspace *self)
{
  guint n_tasks;

  n_tasks = g_list_model_get_n_items (self->inbox_tasks_model);
  gtd_welcome_action_card_set_counter (self->inbox_card, n_tasks);
}

static void
update_today_counter (GtdWelcomeWorkspace *self)
{
  guint n_tasks;

  n_tasks = g_list_model_get_n_items (G_LIST_MODEL (self->today_model));
  gtd_welcome_action_card_set_counter (self->today_card, n_tasks);
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


/*
 * Callbacks
 */

static gboolean
inbox_filter_func (gpointer item,
                   gpointer user_data)
{
  GtdTask *task = (GtdTask*) item;
  return !gtd_task_get_complete (task);
}

static gboolean
filter_func (gpointer  item,
             gpointer  user_data)
{
  g_autoptr (GDateTime) task_dt = NULL;
  g_autoptr (GDateTime) now = NULL;
  GtdTask *task;

  task = (GtdTask*) item;

  if (gtd_task_get_complete (task))
    return FALSE;

  now = g_date_time_new_now_local ();
  task_dt = gtd_task_get_due_date (task);

  return is_today (now, task_dt);
}

static void
on_clock_day_changed_cb (GtdClock            *clock,
                         GtdWelcomeWorkspace *self)
{
  GtkFilter *filter;

  filter = gtk_filter_list_model_get_filter (self->today_model);
  gtk_filter_changed (filter, GTK_FILTER_CHANGE_DIFFERENT);
}

static void
on_clock_hour_changed_cb (GtdClock            *clock,
                          GtdWelcomeWorkspace *self)
{
  g_autoptr (GDateTime) now = NULL;

  update_welcome_label (self);

  /* Switch back to the Welcome screen at 5am */
  now = g_date_time_new_now_local ();
  if (g_date_time_get_hour (now) == 5)
    {
      gtk_widget_activate_action (GTK_WIDGET (self),
                                  "win.activate-workspace",
                                  "(sv)",
                                  "welcome",
                                  NULL);
    }
}

static void
on_inbox_model_items_changed_cb (GListModel          *list,
                                 guint                position,
                                 guint                removed,
                                 guint                added,
                                 GtdWelcomeWorkspace *self)
{
  update_inbox_counter (self);
}

static void
on_model_items_changed_cb (GListModel          *model,
                           guint                position,
                           guint                n_removed,
                           guint                n_added,
                           GtdWelcomeWorkspace *self)
{
  update_today_counter (self);
}

/*
 * GtdWorkspace implementation
 */

static const gchar*
gtd_welcome_workspace_get_id (GtdWorkspace *workspace)
{
  return "welcome";
}

static const gchar*
gtd_welcome_workspace_get_title (GtdWorkspace *workspace)
{
  return _("Home");
}

static gint
gtd_welcome_workspace_get_priority (GtdWorkspace *workspace)
{
  return 1500;
}

static GIcon*
gtd_welcome_workspace_get_icon (GtdWorkspace *workspace)
{
  return g_themed_icon_new ("daytime-sunrise-symbolic");
}

static void
gtd_welcome_workspace_activate (GtdWorkspace *workspace,
                                GVariant     *parameters)
{
  GtdWelcomeWorkspace *self = GTD_WELCOME_WORKSPACE (workspace);

  update_welcome_label (self);
}

static void
gtd_workspace_iface_init (GtdWorkspaceInterface  *iface)
{
  iface->get_id = gtd_welcome_workspace_get_id;
  iface->get_title = gtd_welcome_workspace_get_title;
  iface->get_priority = gtd_welcome_workspace_get_priority;
  iface->get_icon = gtd_welcome_workspace_get_icon;
  iface->activate = gtd_welcome_workspace_activate;
}


/*
 * GObject overrides
 */

static void
gtd_welcome_workspace_finalize (GObject *object)
{
  GtdWelcomeWorkspace *self = (GtdWelcomeWorkspace *)object;

  g_clear_object (&self->inbox_tasks_model);
  g_clear_object (&self->today_model);

  G_OBJECT_CLASS (gtd_welcome_workspace_parent_class)->finalize (object);
}


static void
gtd_welcome_workspace_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  GtdWorkspace *workspace = GTD_WORKSPACE (object);

  switch (prop_id)
    {
    case PROP_ICON:
      g_value_take_object (value, gtd_workspace_get_icon (workspace));
      break;

    case PROP_TITLE:
      g_value_set_string (value, gtd_workspace_get_title (workspace));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_welcome_workspace_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
gtd_welcome_workspace_class_init (GtdWelcomeWorkspaceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gtd_welcome_workspace_finalize;
  object_class->get_property = gtd_welcome_workspace_get_property;
  object_class->set_property = gtd_welcome_workspace_set_property;

  g_object_class_override_property (object_class, PROP_ICON, "icon");
  g_object_class_override_property (object_class, PROP_TITLE, "title");

  g_type_ensure (GTD_TYPE_WELCOME_ACTION_CARD);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/todo/plugins/welcome/gtd-welcome-workspace.ui");

  gtk_widget_class_bind_template_child (widget_class, GtdWelcomeWorkspace, inbox_card);
  gtk_widget_class_bind_template_child (widget_class, GtdWelcomeWorkspace, today_card);
  gtk_widget_class_bind_template_child (widget_class, GtdWelcomeWorkspace, welcome_label);

  gtk_widget_class_set_css_name (widget_class, "welcome-workspace");
}

static void
gtd_welcome_workspace_init (GtdWelcomeWorkspace *self)
{
  g_autoptr (GtkFlattenListModel) flatten_inbox_model = NULL;
  GtdManager *manager;
  GListModel *inbox_model;
  GtkCustomFilter *inbox_filter;
  GtkCustomFilter *filter;

  gtk_widget_init_template (GTK_WIDGET (self));

  manager = gtd_manager_get_default ();
  g_signal_connect_object (gtd_manager_get_clock (manager),
                           "hour-changed",
                           G_CALLBACK (on_clock_hour_changed_cb),
                           self,
                           0);
  update_welcome_label (self);

  /* Inbox */
  inbox_filter = gtk_custom_filter_new (inbox_filter_func, self, NULL);
  inbox_model = gtd_manager_get_inbox_model (manager);
  flatten_inbox_model = gtk_flatten_list_model_new (inbox_model);
  self->inbox_tasks_model =
    G_LIST_MODEL (gtk_filter_list_model_new (G_LIST_MODEL (flatten_inbox_model),
                                             GTK_FILTER (inbox_filter)));
  g_signal_connect_object (self->inbox_tasks_model,
                           "items-changed",
                           G_CALLBACK (on_inbox_model_items_changed_cb),
                           self,
                           0);
  update_inbox_counter (self);

  gtk_actionable_set_action_target (GTK_ACTIONABLE (self->inbox_card),
                                    "(sv)",
                                    "task-lists",
                                    g_variant_new ("s", "inbox-panel"));

  /* Today */
  filter = gtk_custom_filter_new (filter_func, self, NULL);
  self->today_model = gtk_filter_list_model_new (gtd_manager_get_tasks_model (manager),
                                                 GTK_FILTER (filter));

  g_signal_connect_object (self->today_model,
                           "items-changed",
                           G_CALLBACK (on_model_items_changed_cb),
                           self,
                           0);

  g_signal_connect_object (gtd_manager_get_clock (manager),
                           "day-changed",
                           G_CALLBACK (on_clock_day_changed_cb),
                           self,
                           0);

  gtk_actionable_set_action_target (GTK_ACTIONABLE (self->today_card),
                                    "(sv)",
                                    "task-lists",
                                    g_variant_new ("s", "panel-today"));

}
