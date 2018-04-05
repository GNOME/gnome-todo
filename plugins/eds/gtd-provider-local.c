/* gtd-provider-local.c
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

#define G_LOG_DOMAIN "GtdProviderLocal"

#include "gtd-provider-local.h"
#include "gtd-task-list-eds.h"

#include <glib/gi18n.h>

struct _GtdProviderLocal
{
  GtdProviderEds          parent;

  GIcon                  *icon;
  GList                  *tasklists;
};

G_DEFINE_TYPE (GtdProviderLocal, gtd_provider_local, GTD_TYPE_PROVIDER_EDS)


/*
 * GtdProviderEds overrides
 */

static const gchar*
gtd_provider_local_get_id (GtdProviderEds *provider)
{
  return "local";
}

static const gchar*
gtd_provider_local_get_name (GtdProviderEds *provider)
{
  return _("On This Computer");
}

static const gchar*
gtd_provider_local_get_provider_type (GtdProviderEds *provider)
{
  return "local";
}

static const gchar*
gtd_provider_local_get_description (GtdProviderEds *provider)
{
  return _("Local");
}

static gboolean
gtd_provider_local_get_enabled (GtdProviderEds *provider)
{
  return TRUE;
}

static GIcon*
gtd_provider_local_get_icon (GtdProviderEds *provider)
{
  GtdProviderLocal *self = GTD_PROVIDER_LOCAL (provider);

  return self->icon;
}

static ESource*
gtd_provider_local_create_source (GtdProviderEds *provider)
{
  ESourceExtension *extension;
  ESource *source;

  /* Create the source */
  source = e_source_new (NULL, NULL, NULL);

  if (!source)
    return NULL;

  /* Make it a local source */
  extension = e_source_get_extension (source, E_SOURCE_EXTENSION_TASK_LIST);

  e_source_set_parent (source, "local-stub");
  e_source_backend_set_backend_name (E_SOURCE_BACKEND (extension), "local");

  return source;
}

static void
gtd_provider_local_finalize (GObject *object)
{
  GtdProviderLocal *self = (GtdProviderLocal *)object;

  g_clear_object (&self->icon);

  G_OBJECT_CLASS (gtd_provider_local_parent_class)->finalize (object);
}

static gboolean
gtd_provider_local_should_load_source (GtdProviderEds *provider,
                                       ESource        *source)
{
  if (e_source_has_extension (source, E_SOURCE_EXTENSION_TASK_LIST))
    return g_strcmp0 (e_source_get_parent (source), "local-stub") == 0;

  return FALSE;
}

static void
gtd_provider_local_class_init (GtdProviderLocalClass *klass)
{
  GtdProviderEdsClass *eds_class = GTD_PROVIDER_EDS_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  eds_class->get_id = gtd_provider_local_get_id;
  eds_class->get_name = gtd_provider_local_get_name;
  eds_class->get_provider_type = gtd_provider_local_get_provider_type;
  eds_class->get_description = gtd_provider_local_get_description;
  eds_class->get_enabled = gtd_provider_local_get_enabled;
  eds_class->get_icon = gtd_provider_local_get_icon;
  eds_class->create_source = gtd_provider_local_create_source;
  eds_class->should_load_source = gtd_provider_local_should_load_source;

  object_class->finalize = gtd_provider_local_finalize;
}

static void
gtd_provider_local_init (GtdProviderLocal *self)
{
  self->icon = G_ICON (g_themed_icon_new_with_default_fallbacks ("computer-symbolic"));
}

GtdProviderLocal*
gtd_provider_local_new (ESourceRegistry *registry)
{
  return g_object_new (GTD_TYPE_PROVIDER_LOCAL,
                       "registry", registry,
                       NULL);
}
