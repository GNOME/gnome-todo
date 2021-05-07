/* gtd-inbox-panel.c
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


#define G_LOG_DOMAIN "GtdInboxPanel"

#include "gtd-inbox-panel.h"

#include "gnome-todo.h"

#include <glib/gi18n.h>
#include <math.h>


#define GTD_INBOX_PANEL_NAME     "inbox-panel"
#define GTD_INBOX_PANEL_PRIORITY 2000

struct _GtdInboxPanel
{
  GtkBox              parent;

  GIcon              *icon;

  guint               number_of_tasks;
  GtdTaskListView    *view;

  GtkFilterListModel *filter_model;
};

static void          gtd_panel_iface_init                        (GtdPanelInterface  *iface);

G_DEFINE_TYPE_WITH_CODE (GtdInboxPanel, gtd_inbox_panel, GTK_TYPE_BOX,
                         G_IMPLEMENT_INTERFACE (GTD_TYPE_PANEL, gtd_panel_iface_init))

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
filter_func (gpointer item,
             gpointer user_data)
{
  GtdTaskList *task_list;
  GtdTask *task;

  task = (GtdTask*) item;
  task_list = gtd_task_get_list (task);

  return gtd_task_list_is_inbox (task_list) && !gtd_task_get_complete (task);
}

static void
on_model_items_changed_cb (GListModel       *model,
                           guint             position,
                           guint             n_removed,
                           guint             n_added,
                           GtdInboxPanel *self)
{
  guint n_items = g_list_model_get_n_items (model);

  if (self->number_of_tasks == n_items)
    return;

  self->number_of_tasks = n_items;
  g_object_notify (G_OBJECT (self), "subtitle");
}

/*
 * GtdPanel iface
 */

static const gchar*
gtd_panel_inbox_get_panel_name (GtdPanel *panel)
{
  return GTD_INBOX_PANEL_NAME;
}

static const gchar*
gtd_panel_inbox_get_panel_title (GtdPanel *panel)
{
  return _("Inbox");
}

static GList*
gtd_panel_inbox_get_header_widgets (GtdPanel *panel)
{
  return NULL;
}

static const GMenu*
gtd_panel_inbox_get_menu (GtdPanel *panel)
{
  return NULL;
}

static GIcon*
gtd_panel_inbox_get_icon (GtdPanel *panel)
{
  return g_object_ref (GTD_INBOX_PANEL (panel)->icon);
}

static guint32
gtd_panel_inbox_get_priority (GtdPanel *panel)
{
  return GTD_INBOX_PANEL_PRIORITY;
}

static gchar*
gtd_panel_inbox_get_subtitle (GtdPanel *panel)
{
  GtdInboxPanel *self = GTD_INBOX_PANEL (panel);

  return g_strdup_printf ("%d", self->number_of_tasks);
}

static void
gtd_panel_iface_init (GtdPanelInterface *iface)
{
  iface->get_panel_name = gtd_panel_inbox_get_panel_name;
  iface->get_panel_title = gtd_panel_inbox_get_panel_title;
  iface->get_header_widgets = gtd_panel_inbox_get_header_widgets;
  iface->get_menu = gtd_panel_inbox_get_menu;
  iface->get_icon = gtd_panel_inbox_get_icon;
  iface->get_priority = gtd_panel_inbox_get_priority;
  iface->get_subtitle = gtd_panel_inbox_get_subtitle;
}


/*
 * GObject overrides
 */

static void
gtd_inbox_panel_finalize (GObject *object)
{
  GtdInboxPanel *self = (GtdInboxPanel *)object;

  g_clear_object (&self->icon);
  g_clear_object (&self->filter_model);

  G_OBJECT_CLASS (gtd_inbox_panel_parent_class)->finalize (object);
}

static void
gtd_inbox_panel_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  GtdInboxPanel *self = GTD_INBOX_PANEL (object);

  switch (prop_id)
    {
    case PROP_ICON:
      g_value_set_object (value, self->icon);
      break;

    case PROP_MENU:
      g_value_set_object (value, NULL);
      break;

    case PROP_NAME:
      g_value_set_string (value, GTD_INBOX_PANEL_NAME);
      break;

    case PROP_PRIORITY:
      g_value_set_uint (value, GTD_INBOX_PANEL_PRIORITY);
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
gtd_inbox_panel_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
gtd_inbox_panel_class_init (GtdInboxPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gtd_inbox_panel_finalize;
  object_class->get_property = gtd_inbox_panel_get_property;
  object_class->set_property = gtd_inbox_panel_set_property;

  g_object_class_override_property (object_class, PROP_ICON, "icon");
  g_object_class_override_property (object_class, PROP_MENU, "menu");
  g_object_class_override_property (object_class, PROP_NAME, "name");
  g_object_class_override_property (object_class, PROP_PRIORITY, "priority");
  g_object_class_override_property (object_class, PROP_SUBTITLE, "subtitle");
  g_object_class_override_property (object_class, PROP_TITLE, "title");
}

static void
gtd_inbox_panel_init (GtdInboxPanel *self)
{
  GtdManager *manager = gtd_manager_get_default ();
  GtkCustomFilter *filter;

  self->icon = g_themed_icon_new ("mail-inbox-symbolic");

  filter = gtk_custom_filter_new (filter_func, self, NULL);
  self->filter_model = gtk_filter_list_model_new (gtd_manager_get_tasks_model (manager),
                                                  GTK_FILTER (filter));

  /* The main view */
  self->view = GTD_TASK_LIST_VIEW (gtd_task_list_view_new ());
  gtd_task_list_view_set_model (GTD_TASK_LIST_VIEW (self->view), G_LIST_MODEL (self->filter_model));
  gtd_task_list_view_set_show_list_name (GTD_TASK_LIST_VIEW (self->view), FALSE);
  gtd_task_list_view_set_show_due_date (GTD_TASK_LIST_VIEW (self->view), FALSE);

  gtk_widget_set_hexpand (GTK_WIDGET (self->view), TRUE);
  gtk_widget_set_vexpand (GTK_WIDGET (self->view), TRUE);
  gtk_box_append (GTK_BOX (self), GTK_WIDGET (self->view));

  g_signal_connect_object (self->filter_model,
                           "items-changed",
                           G_CALLBACK (on_model_items_changed_cb),
                           self,
                           0);
}

GtkWidget*
gtd_inbox_panel_new (void)
{
  return g_object_new (GTD_TYPE_INBOX_PANEL, NULL);
}
