/* gtd-night-light-plugin.c
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

#define G_LOG_DOMAIN "GtdNightLightPlugin"

#include "gtd-debug.h"
#include "gtd-night-light-plugin.h"

#include <glib/gi18n.h>
#include <glib-object.h>

struct _GtdNightLightPlugin
{
  PeasExtensionBase   parent;

  GDBusProxy         *night_light_proxy;

  gboolean            dark_theme_preferred;
  gboolean            plugin_active;
};

static void          gtd_activatable_iface_init                  (GtdActivatableInterface *iface);

G_DEFINE_DYNAMIC_TYPE_EXTENDED (GtdNightLightPlugin, gtd_night_light_plugin, PEAS_TYPE_EXTENSION_BASE,
                                0,
                                G_IMPLEMENT_INTERFACE_DYNAMIC (GTD_TYPE_ACTIVATABLE, gtd_activatable_iface_init))

enum
{
  PROP_0,
  PROP_PREFERENCES_PANEL,
  N_PROPS
};


/*
 * Auxiliary methods
 */

static gboolean
has_night_light_property (GtdNightLightPlugin *self)
{
  g_auto (GStrv) properties = g_dbus_proxy_get_cached_property_names (self->night_light_proxy);
  return properties ? g_strv_contains ((const gchar * const *)properties, "NightLightActive") : FALSE;
}

static void
update_night_light_state (GtdNightLightPlugin *self)
{
  g_autoptr (GVariant) activev = NULL;
  gboolean active;

  GTD_ENTRY;

  g_assert (GTD_IS_NIGHT_LIGHT_PLUGIN (self));
  g_assert (G_IS_DBUS_PROXY (self->night_light_proxy));

  if (!has_night_light_property (self))
    GTD_RETURN ();

  if (!self->plugin_active)
    {
      g_object_set (gtk_settings_get_default (),
                    "gtk-application-prefer-dark-theme", self->dark_theme_preferred,
                    NULL);
      GTD_RETURN ();
    }

  activev = g_dbus_proxy_get_cached_property (self->night_light_proxy, "NightLightActive");
  active = g_variant_get_boolean (activev);

  g_object_get (gtk_settings_get_default (),
                "gtk-application-prefer-dark-theme", &self->dark_theme_preferred,
                NULL);

  if (active == self->dark_theme_preferred)
    GTD_RETURN ();

  g_object_set (gtk_settings_get_default (),
                "gtk-application-prefer-dark-theme", active,
                NULL);

  GTD_TRACE_MSG ("NightLightActive: %d", active);

  GTD_EXIT;
}


/*
 * Callbacks
 */

static void
on_night_light_proxy_properties_changed_cb (GtdNightLightPlugin *self,
                                            GVariant            *properties,
                                            const gchar * const *invalidated,
                                            GDBusProxy          *proxy)
{
  update_night_light_state (self);
}


/*
 * GtdActivatable interface implementation
 */

static void
gtd_night_light_plugin_activate (GtdActivatable *activatable)
{
  GtdNightLightPlugin *self = (GtdNightLightPlugin*) activatable;

  GTD_ENTRY;

  self->plugin_active = TRUE;
  update_night_light_state (self);

  GTD_EXIT;
}

static void
gtd_night_light_plugin_deactivate (GtdActivatable *activatable)
{
  GtdNightLightPlugin *self = (GtdNightLightPlugin*) activatable;

  GTD_ENTRY;

  self->plugin_active = FALSE;
  update_night_light_state (self);

  GTD_EXIT;
}

static GList*
gtd_night_light_plugin_get_header_widgets (GtdActivatable *activatable)
{
  return NULL;
}

static GtkWidget*
gtd_night_light_plugin_get_preferences_panel (GtdActivatable *activatable)
{
  return NULL;
}

static GList*
gtd_night_light_plugin_get_panels (GtdActivatable *activatable)
{
  return NULL;
}

static GList*
gtd_night_light_plugin_get_providers (GtdActivatable *activatable)
{
  return NULL;
}

static void
gtd_activatable_iface_init (GtdActivatableInterface *iface)
{
  iface->activate = gtd_night_light_plugin_activate;
  iface->deactivate = gtd_night_light_plugin_deactivate;
  iface->get_header_widgets = gtd_night_light_plugin_get_header_widgets;
  iface->get_preferences_panel = gtd_night_light_plugin_get_preferences_panel;
  iface->get_panels = gtd_night_light_plugin_get_panels;
  iface->get_providers = gtd_night_light_plugin_get_providers;
}


/*
 * GObject overrides
 */

static void
gtd_night_light_plugin_get_property (GObject    *object,
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
gtd_night_light_plugin_class_init (GtdNightLightPluginClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = gtd_night_light_plugin_get_property;

  g_object_class_override_property (object_class,
                                    PROP_PREFERENCES_PANEL,
                                    "preferences-panel");
}

static void
gtd_night_light_plugin_init (GtdNightLightPlugin *self)
{
  g_autoptr (GDBusProxy) proxy = NULL;

  GTD_ENTRY;

  proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                         G_DBUS_PROXY_FLAGS_NONE,
                                         NULL,
                                         "org.gnome.SettingsDaemon.Color",
                                         "/org/gnome/SettingsDaemon/Color",
                                         "org.gnome.SettingsDaemon.Color",
                                         NULL,
                                         NULL);

  if (!proxy)
    GTD_RETURN ();

  g_debug ("Creating Night Light monitor");

  g_signal_connect_object (proxy,
                           "g-properties-changed",
                           G_CALLBACK (on_night_light_proxy_properties_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);

  self->night_light_proxy = g_steal_pointer (&proxy);

  on_night_light_proxy_properties_changed_cb (self, NULL, NULL, self->night_light_proxy);

  GTD_EXIT;
}

static void
gtd_night_light_plugin_class_finalize (GtdNightLightPluginClass *klass)
{
}

G_MODULE_EXPORT void
gtd_night_light_plugin_register_types (PeasObjectModule *module)
{
  gtd_night_light_plugin_register_type (G_TYPE_MODULE (module));

  peas_object_module_register_extension_type (module,
                                              GTD_TYPE_ACTIVATABLE,
                                              GTD_TYPE_NIGHT_LIGHT_PLUGIN);
}
