/* gtd-plugin-all-tasks-panel.c
 *
 * Copyright 2018 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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


#define G_LOG_DOMAIN "GtdPluginAllTasksPanel"

#include "gtd-all-tasks-panel.h"

#include "gtd-plugin-all-tasks-panel.h"

#include <glib/gi18n.h>
#include <glib-object.h>

struct _GtdPluginAllTasksPanel
{
  PeasExtensionBase   parent;

  GList              *panels;
  GtkCssProvider     *css_provider;
};

static void          gtd_activatable_iface_init                  (GtdActivatableInterface  *iface);

G_DEFINE_DYNAMIC_TYPE_EXTENDED (GtdPluginAllTasksPanel, gtd_plugin_all_tasks_panel, PEAS_TYPE_EXTENSION_BASE,
                                0,
                                G_IMPLEMENT_INTERFACE_DYNAMIC (GTD_TYPE_ACTIVATABLE,
                                                               gtd_activatable_iface_init))

enum
{
  PROP_0,
  PROP_PREFERENCES_PANEL,
  N_PROPS
};

/*
 * GtdActivatable interface implementation
 */
static void
gtd_plugin_all_tasks_panel_activate (GtdActivatable *activatable)
{
}

static void
gtd_plugin_all_tasks_panel_deactivate (GtdActivatable *activatable)
{
}

static GList*
gtd_plugin_all_tasks_panel_get_header_widgets (GtdActivatable *activatable)
{
  return NULL;
}

static GtkWidget*
gtd_plugin_all_tasks_panel_get_preferences_panel (GtdActivatable *activatable)
{
  return NULL;
}

static GList*
gtd_plugin_all_tasks_panel_get_panels (GtdActivatable *activatable)
{
  GtdPluginAllTasksPanel *plugin = GTD_PLUGIN_ALL_TASKS_PANEL (activatable);

  return plugin->panels;
}

static GList*
gtd_plugin_all_tasks_panel_get_providers (GtdActivatable *activatable)
{
  return NULL;
}

static void
gtd_activatable_iface_init (GtdActivatableInterface *iface)
{
  iface->activate = gtd_plugin_all_tasks_panel_activate;
  iface->deactivate = gtd_plugin_all_tasks_panel_deactivate;
  iface->get_header_widgets = gtd_plugin_all_tasks_panel_get_header_widgets;
  iface->get_preferences_panel = gtd_plugin_all_tasks_panel_get_preferences_panel;
  iface->get_panels = gtd_plugin_all_tasks_panel_get_panels;
  iface->get_providers = gtd_plugin_all_tasks_panel_get_providers;
}

static void
gtd_plugin_all_tasks_panel_finalize (GObject *object)
{
  GtdPluginAllTasksPanel *self = (GtdPluginAllTasksPanel *)object;

  g_list_free (self->panels);

  G_OBJECT_CLASS (gtd_plugin_all_tasks_panel_parent_class)->finalize (object);
}

static void
gtd_plugin_all_tasks_panel_get_property (GObject    *object,
                                         guint       prop_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
  switch (prop_id)
    {
    case PROP_PREFERENCES_PANEL:
      g_value_set_object (value, NULL);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_plugin_all_tasks_panel_class_init (GtdPluginAllTasksPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gtd_plugin_all_tasks_panel_finalize;
  object_class->get_property = gtd_plugin_all_tasks_panel_get_property;

  g_object_class_override_property (object_class, PROP_PREFERENCES_PANEL, "preferences-panel");
}

static void
gtd_plugin_all_tasks_panel_init (GtdPluginAllTasksPanel *self)
{
  g_autoptr (GSettings) settings = NULL;
  g_autoptr (GFile) css_file = NULL;
  g_autofree gchar *theme_name = NULL;
  g_autofree gchar *theme_uri = NULL;

  /* Load CSS provider */
  settings = g_settings_new ("org.gnome.desktop.interface");
  theme_name = g_settings_get_string (settings, "gtk-theme");
  theme_uri = g_build_filename ("resource:///org/gnome/todo/theme/all-tasks-panel", theme_name, ".css", NULL);
  css_file = g_file_new_for_uri (theme_uri);

  self->css_provider = gtk_css_provider_new ();
  gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                              GTK_STYLE_PROVIDER (self->css_provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  if (g_file_query_exists (css_file, NULL))
    gtk_css_provider_load_from_file (self->css_provider, css_file);
  else
    gtk_css_provider_load_from_resource (self->css_provider, "/org/gnome/todo/theme/all-tasks-panel/Adwaita.css");

  /* And then the panel */
  self->panels = g_list_append (NULL, gtd_all_tasks_panel_new ());
}

static void
gtd_plugin_all_tasks_panel_class_finalize (GtdPluginAllTasksPanelClass *klass)
{
}

G_MODULE_EXPORT void
gtd_plugin_all_tasks_panel_register_types (PeasObjectModule *module)
{
  gtd_plugin_all_tasks_panel_register_type (G_TYPE_MODULE (module));

  peas_object_module_register_extension_type (module,
                                              GTD_TYPE_ACTIVATABLE,
                                              GTD_TYPE_PLUGIN_ALL_TASKS_PANEL);
}
