/* gtd-plugin-eds.c
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

#define G_LOG_DOMAIN "GtdPluginEds"

#include "gtd-plugin-eds.h"
#include "gtd-provider-goa.h"
#include "gtd-provider-local.h"

#include <glib/gi18n.h>
#include <glib-object.h>

/**
 * The #GtdPluginEds is a class that loads all the
 * essential providers of GNOME To Do.
 *
 * It basically loads #ESourceRegistry which provides
 * #GtdProviderLocal. Immediately after that, it loads
 * #GoaClient which provides one #GtdProviderGoa per
 * supported account.
 *
 * The currently supported Online Accounts are Google,
 * ownCloud and Microsoft Exchange ones.
 */

struct _GtdPluginEds
{
  GObject                 parent;

  ESourceRegistry        *registry;

  /* Providers */
  GList                  *providers;
};

enum
{
  PROP_0,
  PROP_PREFERENCES_PANEL,
  LAST_PROP
};

const gchar *supported_accounts[] = {
  "exchange",
  "google",
  "owncloud",
  NULL
};

static void          gtd_activatable_iface_init                  (GtdActivatableInterface  *iface);

G_DEFINE_TYPE_WITH_CODE (GtdPluginEds, gtd_plugin_eds, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GTD_TYPE_ACTIVATABLE, gtd_activatable_iface_init))

/*
 * GtdActivatable interface implementation
 */
static void
gtd_plugin_eds_activate (GtdActivatable *activatable)
{
  ;
}

static void
gtd_plugin_eds_deactivate (GtdActivatable *activatable)
{
  ;
}

static GtkWidget*
gtd_plugin_eds_get_preferences_panel (GtdActivatable *activatable)
{
  return NULL;
}

static void
gtd_activatable_iface_init (GtdActivatableInterface *iface)
{
  iface->activate = gtd_plugin_eds_activate;
  iface->deactivate = gtd_plugin_eds_deactivate;
  iface->get_preferences_panel = gtd_plugin_eds_get_preferences_panel;
}


/*
 * Init
 */

static void
gtd_plugin_eds_goa_account_removed_cb (GoaClient    *client,
                                       GoaObject    *object,
                                       GtdPluginEds *self)
{
  GtdManager *manager;
  GoaAccount *account;
  GList *l;

  account = goa_object_peek_account (object);
  manager = gtd_manager_get_default ();

  if (!g_strv_contains (supported_accounts, goa_account_get_provider_type (account)))
    return;

  for (l = self->providers; l != NULL; l = l->next)
    {
      if (!GTD_IS_PROVIDER_GOA (l->data))
        continue;

      if (account == gtd_provider_goa_get_account (l->data))
        {
          GtdProviderGoa *provider = GTD_PROVIDER_GOA (l->data);

          self->providers = g_list_remove (self->providers, l->data);
          gtd_manager_add_provider (manager, GTD_PROVIDER (provider));
          break;
        }
    }
}

static void
gtd_plugin_eds_goa_account_added_cb (GoaClient    *client,
                                     GoaObject    *object,
                                     GtdPluginEds *self)
{
  GtdManager *manager;
  GoaAccount *account;

  account = goa_object_get_account (object);
  manager = gtd_manager_get_default ();

  if (g_strv_contains (supported_accounts, goa_account_get_provider_type (account)))
    {
      GtdProviderGoa *provider;

      provider = gtd_provider_goa_new (self->registry, account);

      self->providers = g_list_append (self->providers, provider);
      gtd_manager_add_provider (manager, GTD_PROVIDER (provider));
    }
}

static void
gtd_plugin_eds_goa_client_finish_cb (GObject      *client,
                                     GAsyncResult *result,
                                     gpointer      user_data)
{
  g_autoptr (GError) error = NULL;
  GtdPluginEds *self;
  GtdManager *manager;
  GoaClient *goa_client;
  GList *accounts;
  GList *l;

  self = GTD_PLUGIN_EDS (user_data);
  goa_client = goa_client_new_finish (result, &error);
  manager = gtd_manager_get_default ();

  if (error)
    {
      g_warning ("%s: %s: %s",
                 G_STRFUNC,
                 "Error loading GNOME Online Accounts",
                 error->message);

      gtd_manager_emit_error_message (gtd_manager_get_default (),
                                      _("Error loading GNOME Online Accounts"),
                                      error->message,
                                      NULL,
                                      NULL);
      g_clear_error (&error);
    }

  /* Load each supported GoaAccount into a GtdProviderGoa */
  accounts = goa_client_get_accounts (goa_client);

  for (l = accounts; l != NULL; l = l->next)
    {
      GtdProviderGoa *provider;
      GoaAccount *account;
      GoaObject *object;

      object = l->data;
      account = goa_object_get_account (object);

      if (!g_strv_contains (supported_accounts, goa_account_get_provider_type (account)))
        {
          g_object_unref (account);
          continue;
        }

      g_debug ("Creating new provider for account '%s'", goa_account_get_identity (account));

      /* Create the new GOA provider */
      provider = gtd_provider_goa_new (self->registry, account);

      self->providers = g_list_append (self->providers, provider);
      gtd_manager_add_provider (manager, GTD_PROVIDER (provider));

      g_object_unref (account);
    }

  /* Connect GoaClient signals */
  g_signal_connect (goa_client,
                    "account-added",
                    G_CALLBACK (gtd_plugin_eds_goa_account_added_cb),
                    user_data);

  g_signal_connect (goa_client,
                    "account-removed",
                    G_CALLBACK (gtd_plugin_eds_goa_account_removed_cb),
                    user_data);

  g_list_free_full (accounts, g_object_unref);
}



static void
gtd_plugin_eds_source_registry_finish_cb (GObject      *source_object,
                                          GAsyncResult *result,
                                          gpointer      user_data)
{
  GtdPluginEds *self = GTD_PLUGIN_EDS (user_data);
  GtdProviderLocal *provider;
  ESourceRegistry *registry;
  GtdManager *manager;
  GError *error = NULL;

  manager = gtd_manager_get_default ();
  registry = e_source_registry_new_finish (result, &error);
  self->registry = registry;

  /* Abort on error */
  if (error)
    {
      g_warning ("%s: %s",
                 "Error loading Evolution-Data-Server backend",
                 error->message);

      g_clear_error (&error);
      return;
    }

  /* Load the local provider */
  provider = gtd_provider_local_new (registry);

  self->providers = g_list_append (self->providers, provider);
  gtd_manager_add_provider (manager, GTD_PROVIDER (provider));

  /* We only start loading Goa accounts after
   * ESourceRegistry is get, since it'd be way
   * too hard to synchronize these two asynchronous
   * calls.
   */
  goa_client_new (NULL,
                  (GAsyncReadyCallback) gtd_plugin_eds_goa_client_finish_cb,
                  self);
}

static void
gtd_plugin_eds_finalize (GObject *object)
{
  GtdPluginEds *self = (GtdPluginEds *)object;

  g_list_free_full (self->providers, g_object_unref);
  self->providers = NULL;

  G_OBJECT_CLASS (gtd_plugin_eds_parent_class)->finalize (object);
}

static void
gtd_plugin_eds_get_property (GObject    *object,
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
gtd_plugin_eds_class_init (GtdPluginEdsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gtd_plugin_eds_finalize;
  object_class->get_property = gtd_plugin_eds_get_property;

  g_object_class_override_property (object_class,
                                    PROP_PREFERENCES_PANEL,
                                    "preferences-panel");
}

static void
gtd_plugin_eds_init (GtdPluginEds *self)
{
  /* load the source registry */
  e_source_registry_new (NULL,
                         (GAsyncReadyCallback) gtd_plugin_eds_source_registry_finish_cb,
                         self);
}
