/* gtd-provider-goa.c
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

#define G_LOG_DOMAIN "GtdProviderGoa"

#include "gtd-eds-autoptr.h"
#include "gtd-provider-eds.h"
#include "gtd-provider-goa.h"

#include <glib/gi18n.h>

struct _GtdProviderGoa
{
  GtdProviderEds          parent;

  GoaAccount             *account;
  GIcon                  *icon;

  gchar                  *id;
};

G_DEFINE_TYPE (GtdProviderGoa, gtd_provider_goa, GTD_TYPE_PROVIDER_EDS)

enum
{
  PROP_0,
  PROP_ACCOUNT,
  N_PROPS
};


/*
 * GtdProviderEds overrides
 */

static const gchar*
gtd_provider_goa_get_id (GtdProviderEds *provider)
{
  GtdProviderGoa *self = GTD_PROVIDER_GOA (provider);

  return self->id;
}

static const gchar*
gtd_provider_goa_get_name (GtdProviderEds *provider)
{
  GtdProviderGoa *self = GTD_PROVIDER_GOA (provider);

  return goa_account_get_provider_name (self->account);
}

static const gchar*
gtd_provider_goa_get_provider_type (GtdProviderEds *provider)
{
  GtdProviderGoa *self = GTD_PROVIDER_GOA (provider);

  return goa_account_get_provider_type (self->account);
}

static const gchar*
gtd_provider_goa_get_description (GtdProviderEds *provider)
{
  GtdProviderGoa *self = GTD_PROVIDER_GOA (provider);

  return goa_account_get_identity (self->account);
}

static gboolean
gtd_provider_goa_get_enabled (GtdProviderEds *provider)
{
  GtdProviderGoa *self = GTD_PROVIDER_GOA (provider);

  return !goa_account_get_calendar_disabled (self->account);
}

static GIcon*
gtd_provider_goa_get_icon (GtdProviderEds *provider)
{
  GtdProviderGoa *self = GTD_PROVIDER_GOA (provider);

  return self->icon;
}

static void
gtd_provider_goa_set_account (GtdProviderGoa *provider,
                              GoaAccount     *account)
{
  if (provider->account != account)
    {
      g_autofree gchar *icon_name = NULL;

      g_set_object (&provider->account, account);
      g_object_notify (G_OBJECT (provider), "account");

      g_debug ("Setting up Online Account: %s (%s)",
               goa_account_get_identity (account),
               goa_account_get_id (account));

      /* Update icon */
      icon_name = g_strdup_printf ("goa-account-%s", goa_account_get_provider_type (provider->account));
      g_set_object (&provider->icon, g_themed_icon_new (icon_name));
      g_object_notify (G_OBJECT (provider), "icon");

      /* Provider id */
      provider->id = g_strdup_printf ("%s@%s",
                                      goa_account_get_provider_type (provider->account),
                                      goa_account_get_id (provider->account));
    }
}


/*
 * GObject overrides
 */

static void
gtd_provider_goa_finalize (GObject *object)
{
  GtdProviderGoa *self = (GtdProviderGoa *)object;

  g_clear_pointer (&self->id, g_free);

  g_clear_object (&self->account);
  g_clear_object (&self->icon);

  G_OBJECT_CLASS (gtd_provider_goa_parent_class)->finalize (object);
}

static void
gtd_provider_goa_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  GtdProviderGoa *self = GTD_PROVIDER_GOA (object);

  switch (prop_id)
    {

    case PROP_ACCOUNT:
      g_value_set_object (value, self->account);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_provider_goa_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  GtdProviderGoa *self = GTD_PROVIDER_GOA (object);

  switch (prop_id)
    {
    case PROP_ACCOUNT:
      gtd_provider_goa_set_account (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static gboolean
gtd_provider_goa_should_load_source (GtdProviderEds *provider,
                                     ESource        *source)
{
  g_autoptr (ESource) ancestor = NULL;
  GtdProviderGoa *self;
  gboolean retval;

  self = GTD_PROVIDER_GOA (provider);
  retval = FALSE;

  ancestor = e_source_registry_find_extension (gtd_provider_eds_get_registry (provider),
                                               source,
                                               E_SOURCE_EXTENSION_GOA);

  /* If we detect that the given source is provided by a GOA account, check the account id */
  if (ancestor)
    {
      ESourceExtension *extension;
      const gchar *ancestor_id;
      const gchar *account_id;

      extension = e_source_get_extension (ancestor, E_SOURCE_EXTENSION_GOA);
      ancestor_id = e_source_goa_get_account_id (E_SOURCE_GOA (extension));
      account_id = goa_account_get_id (self->account);

      /* When the ancestor's GOA id matches the current account's id, we shall load this list */
      retval = g_strcmp0 (ancestor_id, account_id) == 0;
    }

  return retval;
}

static void
gtd_provider_goa_class_init (GtdProviderGoaClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtdProviderEdsClass *eds_class = GTD_PROVIDER_EDS_CLASS (klass);

  eds_class->get_id = gtd_provider_goa_get_id;
  eds_class->get_name = gtd_provider_goa_get_name;
  eds_class->get_provider_type = gtd_provider_goa_get_provider_type;
  eds_class->get_description = gtd_provider_goa_get_description;
  eds_class->get_enabled = gtd_provider_goa_get_enabled;
  eds_class->get_icon = gtd_provider_goa_get_icon;
  eds_class->should_load_source = gtd_provider_goa_should_load_source;

  object_class->finalize = gtd_provider_goa_finalize;
  object_class->get_property = gtd_provider_goa_get_property;
  object_class->set_property = gtd_provider_goa_set_property;

  g_object_class_install_property (object_class,
                                   PROP_ACCOUNT,
                                   g_param_spec_object ("account",
                                                        "Account of the provider",
                                                        "The Online Account of the provider",
                                                        GOA_TYPE_ACCOUNT,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
gtd_provider_goa_init (GtdProviderGoa *self)
{
}

GtdProviderGoa*
gtd_provider_goa_new (ESourceRegistry *registry,
                      GoaAccount      *account)
{
  return g_object_new (GTD_TYPE_PROVIDER_GOA,
                       "account", account,
                       "registry", registry,
                       NULL);
}

GoaAccount*
gtd_provider_goa_get_account (GtdProviderGoa *provider)
{
  return provider->account;
}
