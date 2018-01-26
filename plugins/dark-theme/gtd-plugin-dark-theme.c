/* gtd-plugin-dark-theme.c
 *
 * Copyright (C) 2016 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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

#define G_LOG_DOMAIN "GtdPluginDarkTheme"

#include "gtd-plugin-dark-theme.h"

#include <glib/gi18n.h>
#include <glib-object.h>

struct _GtdPluginDarkTheme
{
  PeasExtensionBase   parent;
};

static void          gtd_activatable_iface_init                  (GtdActivatableInterface *iface);

G_DEFINE_DYNAMIC_TYPE_EXTENDED (GtdPluginDarkTheme, gtd_plugin_dark_theme, PEAS_TYPE_EXTENSION_BASE,
                                0,
                                G_IMPLEMENT_INTERFACE_DYNAMIC (GTD_TYPE_ACTIVATABLE, gtd_activatable_iface_init))

enum {
  PROP_0,
  PROP_PREFERENCES_PANEL,
  N_PROPS
};

/*
 * GtdActivatable interface implementation
 */
static void
gtd_plugin_dark_theme_activate (GtdActivatable *activatable)
{
  g_object_set (gtk_settings_get_default (),
                "gtk-application-prefer-dark-theme", TRUE,
                NULL);
}

static void
gtd_plugin_dark_theme_deactivate (GtdActivatable *activatable)
{
  g_object_set (gtk_settings_get_default (),
                "gtk-application-prefer-dark-theme", FALSE,
                NULL);
}

static GList*
gtd_plugin_dark_theme_get_header_widgets (GtdActivatable *activatable)
{
  return NULL;
}

static GtkWidget*
gtd_plugin_dark_theme_get_preferences_panel (GtdActivatable *activatable)
{
  return NULL;
}

static GList*
gtd_plugin_dark_theme_get_panels (GtdActivatable *activatable)
{
  return NULL;
}

static GList*
gtd_plugin_dark_theme_get_providers (GtdActivatable *activatable)
{
  return NULL;
}

static void
gtd_activatable_iface_init (GtdActivatableInterface *iface)
{
  iface->activate = gtd_plugin_dark_theme_activate;
  iface->deactivate = gtd_plugin_dark_theme_deactivate;
  iface->get_header_widgets = gtd_plugin_dark_theme_get_header_widgets;
  iface->get_preferences_panel = gtd_plugin_dark_theme_get_preferences_panel;
  iface->get_panels = gtd_plugin_dark_theme_get_panels;
  iface->get_providers = gtd_plugin_dark_theme_get_providers;
}

static void
gtd_plugin_dark_theme_get_property (GObject    *object,
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
gtd_plugin_dark_theme_class_init (GtdPluginDarkThemeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = gtd_plugin_dark_theme_get_property;

  g_object_class_override_property (object_class,
                                    PROP_PREFERENCES_PANEL,
                                    "preferences-panel");
}

static void
gtd_plugin_dark_theme_init (GtdPluginDarkTheme *self)
{
}

static void
gtd_plugin_dark_theme_class_finalize (GtdPluginDarkThemeClass *klass)
{
}

G_MODULE_EXPORT void
gtd_plugin_dark_theme_register_types (PeasObjectModule *module)
{
  gtd_plugin_dark_theme_register_type (G_TYPE_MODULE (module));

  peas_object_module_register_extension_type (module,
                                              GTD_TYPE_ACTIVATABLE,
                                              GTD_TYPE_PLUGIN_DARK_THEME);
}
