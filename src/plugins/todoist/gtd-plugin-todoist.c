/* gtd-plugin-todoist.c
 *
 * Copyright (C) 2017 Rohit Kaushik <kaushikrohit325@gmail.com>
 *               2018 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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

#define G_LOG_DOMAIN "GtdPluginTodoist"

#include "gtd-debug.h"
#include "gtd-plugin-todoist.h"
#include "gtd-provider-todoist.h"
#include "gtd-todoist-preferences-panel.h"

#include <glib/gi18n.h>
#include <glib-object.h>

/**
 * The #GtdPluginTodoist is a class that loads Todoist
 * provider of GNOME To Do.
 */

struct _GtdPluginTodoist
{
  PeasExtensionBase   parent;

  GtkWidget          *preferences;

  GoaClient          *goa_client;

  gboolean            active;

  /* Providers */
  GList              *providers;
};

enum
{
  PROP_0,
  PROP_PREFERENCES_PANEL,
  LAST_PROP
};

static void          gtd_activatable_iface_init                  (GtdActivatableInterface  *iface);

G_DEFINE_DYNAMIC_TYPE_EXTENDED (GtdPluginTodoist, gtd_plugin_todoist, PEAS_TYPE_EXTENSION_BASE,
                                0,
                                G_IMPLEMENT_INTERFACE_DYNAMIC (GTD_TYPE_ACTIVATABLE,
                                                               gtd_activatable_iface_init))

/*
 * GtdActivatable interface implementation
 */
static void
gtd_plugin_todoist_activate (GtdActivatable *activatable)
{
}

static void
gtd_plugin_todoist_deactivate (GtdActivatable *activatable)
{
}

static GList*
gtd_plugin_todoist_get_header_widgets (GtdActivatable *activatable)
{
  return NULL;
}

static GtkWidget*
gtd_plugin_todoist_get_preferences_panel (GtdActivatable *activatable)
{
  GtdPluginTodoist *self = GTD_PLUGIN_TODOIST (activatable);

  return self->preferences;

}

static GList*
gtd_plugin_todoist_get_panels (GtdActivatable *activatable)
{
  return NULL;
}

static GList*
gtd_plugin_todoist_get_providers (GtdActivatable *activatable)
{
  GtdPluginTodoist *plugin = GTD_PLUGIN_TODOIST (activatable);

  return plugin->providers;
}

static void
emit_connection_error (void)
{
  gtd_manager_emit_error_message (gtd_manager_get_default (),
                                  _("GNOME To Do cannot connect to Todoist due to network issue"),
                                  _("Not able to communicate with Todoist. Please check your internet connectivity."),
                                  NULL,
                                  NULL);
}

static void
on_account_added_cb (GoaClient        *client,
                     GoaObject        *account_object,
                     GtdPluginTodoist *self)
{
  GtdProviderTodoist *provider;
  GoaAccount *goa_account;
  const gchar *provider_name;

  if (!self->active)
    return;

  goa_account = goa_object_get_account (account_object);
  provider_name = goa_account_get_provider_name (goa_account);

  g_object_unref (goa_account);

  if (g_strcmp0 (provider_name, "Todoist") != 0)
    return;

  provider = gtd_provider_todoist_new (account_object);

  self->providers = g_list_append (self->providers, provider);

  g_signal_emit_by_name (self, "provider-added", provider);
}

static void
on_account_removed_cb (GoaClient        *client,
                       GoaObject        *account_object,
                       GtdPluginTodoist *self)
{
  GoaAccount *goa_account;
  const gchar *provider_name;
  GList *l;

  if (!self->active)
    return;

  goa_account = goa_object_get_account (account_object);
  provider_name = goa_account_get_provider_name (goa_account);
  l = NULL;

  g_object_unref (goa_account);

  if (g_strcmp0 (provider_name, "Todoist") != 0)
    return;

  for (l = self->providers; l != NULL; l = l->next)
    {
      GoaObject *object;

      object = gtd_provider_todoist_get_goa_object (l->data);

      if (object == account_object)
        {
          self->providers = g_list_remove (self->providers, l->data);

          g_signal_emit_by_name (self, "provider-removed", l->data);
          break;
        }
    }
}

static void
on_goa_client_ready_cb (GObject          *source,
                        GAsyncResult     *result,
                        GtdPluginTodoist *self)
{
  g_autoptr (GError) error = NULL;
  GList *accounts;
  GList *l;

  self->goa_client = goa_client_new_finish (result, &error);

  if (error)
    {
      g_warning ("Error retriving GNOME Online Accounts client: %s", error->message);
      return;
    }

  gtd_todoist_preferences_panel_set_client (GTD_TODOIST_PREFERENCES_PANEL (self->preferences), self->goa_client);

  if (!self->active)
    return;

  accounts = goa_client_get_accounts (self->goa_client);

  for (l = accounts; l != NULL; l = l->next)
    {
      GoaAccount *account;
      const gchar *provider_type;

      account = goa_object_get_account (l->data);
      provider_type = goa_account_get_provider_type (account);

      if (g_strcmp0 (provider_type, "todoist") == 0)
        on_account_added_cb (self->goa_client, l->data, self);

      g_object_unref (account);
    }

  /* Connect signals */
  g_signal_connect (self->goa_client, "account-added", G_CALLBACK (on_account_added_cb), self);
  g_signal_connect (self->goa_client, "account-removed", G_CALLBACK (on_account_removed_cb), self);

  g_list_free_full (accounts, g_object_unref);
}

static void
remove_providers (GtdPluginTodoist *self)
{
  GList *l;

  GTD_ENTRY;

  self->active = FALSE;

  for (l = self->providers; l != NULL; l = l->next)
    {
      GtdProviderTodoist *provider;

      provider = GTD_PROVIDER_TODOIST (l->data);

      self->providers = g_list_remove (self->providers, l->data);

      g_signal_emit_by_name (self, "provider-removed", provider);
    }

  /* Disconnect handlers */
  g_signal_handlers_disconnect_by_func (self->goa_client, on_account_added_cb, self);
  g_signal_handlers_disconnect_by_func (self->goa_client, on_account_removed_cb, self);

  GTD_RETURN ();
}

static void
readd_providers (GtdPluginTodoist *self)
{
  GList *accounts;
  GList *l;

  GTD_ENTRY;

  self->active = TRUE;
  accounts = goa_client_get_accounts (self->goa_client);

  for (l = accounts; l != NULL; l = l->next)
    {
      GoaAccount *account;
      const gchar *provider_type;

      account = goa_object_get_account (l->data);
      provider_type = goa_account_get_provider_type (account);

      if (g_strcmp0 (provider_type, "todoist") == 0)
        on_account_added_cb (self->goa_client, l->data, self);

      g_object_unref (account);
    }

  /* Connect signals */
  g_signal_connect (self->goa_client, "account-added", G_CALLBACK (on_account_added_cb), self);
  g_signal_connect (self->goa_client, "account-removed", G_CALLBACK (on_account_removed_cb), self);

  GTD_RETURN ();
}

static void
can_reach_todoist_cb (GNetworkMonitor  *monitor,
                      GAsyncResult     *result,
                      GtdPluginTodoist *self)
{
  g_autoptr (GError) error = NULL;
  gboolean reachable;

  reachable = g_network_monitor_can_reach_finish (monitor, result, &error);

  if (!reachable && self->active)
    {
      remove_providers (self);
      self->active = FALSE;
      emit_connection_error ();
    }
  else if (reachable && !self->active)
    {
      readd_providers (self);
      self->active = TRUE;
    }
}

static void
can_reach_todoist (GtdPluginTodoist  *self,
                   GNetworkMonitor   *monitor)
{
  GSocketConnectable *addr;
  gboolean status;
  gboolean connected;

  GTD_ENTRY;

  addr = g_network_address_new ("www.todoist.com", 80);
  connected = FALSE;

  status = g_network_monitor_get_connectivity (monitor);

  switch (status)
    {
      case G_NETWORK_CONNECTIVITY_LOCAL:
        g_debug ("G_NETWORK_CONNECTIVITY_LOCAL");
        connected = FALSE;
        break;

      case G_NETWORK_CONNECTIVITY_LIMITED:
        g_debug ("G_NETWORK_CONNECTIVITY_LIMITED");
        connected = FALSE;
        break;

      case G_NETWORK_CONNECTIVITY_PORTAL:
        g_debug ("G_NETWORK_CONNECTIVITY_PORTAL");
        connected = FALSE;
        break;

      case G_NETWORK_CONNECTIVITY_FULL:
        connected = TRUE;
        break;
    }

  if (connected)
    {
      g_network_monitor_can_reach_async (monitor, addr, NULL, (GAsyncReadyCallback) can_reach_todoist_cb, self);
    }
  else if (self->active)
    {
      remove_providers (self);
      self->active = FALSE;
      emit_connection_error ();
    }
}

static void
connectivity_changed (GNetworkMonitor  *monitor,
                      gboolean          available,
                      GtdPluginTodoist *self)
{
  /*
   * Handles the removal and adding of providers if connectivity to Todoist changes
   * Sends an error message before removing the providers if the plugin goes from
   * active to inactive state
   */
  can_reach_todoist (self, monitor);
}

static void
gtd_activatable_iface_init (GtdActivatableInterface *iface)
{
  iface->activate = gtd_plugin_todoist_activate;
  iface->deactivate = gtd_plugin_todoist_deactivate;
  iface->get_header_widgets = gtd_plugin_todoist_get_header_widgets;
  iface->get_preferences_panel = gtd_plugin_todoist_get_preferences_panel;
  iface->get_panels = gtd_plugin_todoist_get_panels;
  iface->get_providers = gtd_plugin_todoist_get_providers;
}

/*
 * GObject overrides
 */

static void
gtd_plugin_todoist_finalize (GObject *object)
{
  GtdPluginTodoist *self = (GtdPluginTodoist *) object;

  g_list_free_full (self->providers, g_object_unref);
  self->providers = NULL;

  g_clear_object (&self->goa_client);

  G_OBJECT_CLASS (gtd_plugin_todoist_parent_class)->finalize (object);
}

static void
gtd_plugin_todoist_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  GtdPluginTodoist *self = GTD_PLUGIN_TODOIST (object);
  switch (prop_id)
    {
    case PROP_PREFERENCES_PANEL:
      g_value_set_object (value, self->preferences);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_plugin_todoist_class_init (GtdPluginTodoistClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gtd_plugin_todoist_finalize;
  object_class->get_property = gtd_plugin_todoist_get_property;

  g_object_class_override_property (object_class, PROP_PREFERENCES_PANEL, "preferences-panel");
}

static void
gtd_plugin_todoist_init (GtdPluginTodoist *self)
{
  self->preferences = GTK_WIDGET (gtd_todoist_preferences_panel_new ());

  goa_client_new (NULL, (GAsyncReadyCallback) on_goa_client_ready_cb, self);

  /*
   * Need to check if Todoist is reachable. By Default the plugin is in inactive state
   * i.e (self->active = FALSE) If connection to Todoist can be established the providers
   * are added in the can_reach_todoist callback
   */
  can_reach_todoist (self, g_network_monitor_get_default ());

  /* Connect network-changed signal */
  g_signal_connect (g_network_monitor_get_default (), "network-changed", G_CALLBACK (connectivity_changed), self);
}

/* Empty class_finalize method */
static void
gtd_plugin_todoist_class_finalize (GtdPluginTodoistClass *klass)
{
}

G_MODULE_EXPORT void
gtd_plugin_todoist_register_types (PeasObjectModule *module)
{
  gtd_plugin_todoist_register_type (G_TYPE_MODULE (module));

  peas_object_module_register_extension_type (module,
                                              GTD_TYPE_ACTIVATABLE,
                                              GTD_TYPE_PLUGIN_TODOIST);
}
