/* gtd-search-provider.c
 *
 * Copyright 2018 Vyas Giridharan <vyasgiridhar27@gmail.com>
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

#define G_LOG_DOMAIN "GtdSearchProvider"

#include <gio/gio.h>

#include <string.h>

#include "gtd-debug.h"
#include "gtd-search-provider-generated.h"
#include "gtd-search-provider.h"
#include "gtd-task.h"
#include "gtd-manager.h"
#include "models/gtd-task-model.h"
#include "models/gtd-list-model-filter.h"

struct _GtdSearchProvider
{
  GObject parent_instance;

  GHashTable *cache;

  GtdShellSearchProvider2 *skeleton;
};

G_DEFINE_TYPE (GtdSearchProvider, gtd_search_provider, G_TYPE_OBJECT)

typedef struct
{
  GDBusMethodInvocation  *invocation;
  gchar                 **results;
  GtdSearchProvider      *provider;
} ResultMetaData;

static void
result_metadata_free (ResultMetaData *data)
{
  g_clear_object (&data->provider);
  g_clear_object (&data->invocation);
  g_strfreev (data->results);

  g_slice_free (ResultMetaData, data);
}

static gboolean
filter_func (GObject *object,
             gpointer user_data)
{
  GtdTask *task = GTD_TASK (object);
  const gchar *task_title;
  gchar **terms = user_data;
  guint i;

  task_title = gtd_task_get_title (task);

  for (i = 0; terms[i] != NULL; i++)
    {
      if (strstr (task_title, terms[i]))
        {
          return TRUE;
        }
    }
  return FALSE;
}

static gboolean
task_id_filter_func (GObject *object,
                     gpointer user_data)
{
  const gchar *id;
  gchar **results = user_data;
  guint i;

  id = gtd_object_get_uid (GTD_OBJECT (object));

  for (i = 0; results[i] != NULL; i++)
    {
      if (g_strcmp0 (results[i], id) == 0)
        {
          return TRUE;
        }
    }
  return FALSE;
}

static void
create_result_metas_async (GTask        *g_task,
                           gpointer      source_object,
                           gpointer      task_data,
                           GCancellable *cancellable)
{
  GtdSearchProvider *self = source_object;
  GListModel *model = gtd_manager_get_tasks_model (gtd_manager_get_default ());
  GtdListModelFilter *filter = gtd_list_model_filter_new (model);
  ResultMetaData *data = task_data;
  GVariantBuilder meta;
  GVariant *meta_variant;
  GtdTask *task;
  gint n_tasks, i;

  GTD_ENTRY;

  g_application_hold (g_application_get_default ());

  gtd_list_model_filter_set_filter_func (filter, task_id_filter_func, data->results, NULL);
  n_tasks = g_list_model_get_n_items (G_LIST_MODEL (filter));

  if (n_tasks == 0)
    {
      g_application_release (g_application_get_default ());
    }

  for (i = 0; i < n_tasks; i++)
    {
      if (g_hash_table_lookup (self->cache, data->results[i]))
        {
          continue;
        }

      g_variant_builder_init (&meta, G_VARIANT_TYPE ("a{sv}"));

      task = GTD_TASK (g_list_model_get_object (G_LIST_MODEL (filter), i));

      g_variant_builder_add (&meta, "{sv}",
                             "id", g_variant_new_string (data->results[i]));
      g_variant_builder_add (&meta, "{sv}",
                             "name", g_variant_new_string (gtd_task_get_title (task)));
      g_variant_builder_add (&meta, "{sv}",
                             "description", g_variant_new_string (gtd_task_get_description (task)));

      meta_variant = g_variant_builder_end (&meta);

      g_hash_table_insert (self->cache, g_strdup (data->results[i]), g_variant_ref_sink (meta_variant));
    }

  g_task_return_pointer (g_task, NULL, NULL);
}

static void
result_meta_return_from_cache (ResultMetaData *data)
{
  GVariantBuilder builder;
  GVariant *meta;
  gint idx;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("aa{sv}"));

  for (idx = 0; data->results[idx] != NULL; idx++)
    {
      meta = g_hash_table_lookup (data->provider->cache,
                                  data->results[idx]);
      g_variant_builder_add_value (&builder, meta);
    }

  g_dbus_method_invocation_return_value (data->invocation,
                                         g_variant_new ("(aa{sv})", &builder));
}

static void
result_meta_callback (GObject      *self,
                      GAsyncResult *result,
                      gpointer      user_data)
{
  GVariantBuilder builder;
  GVariant *meta;
  ResultMetaData *data;
  guint i;

  GTD_ENTRY;

  data = user_data;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("aa{sv}"));

  for (i = 0; i < g_strv_length (data->results); i++)
    {
      meta = g_hash_table_lookup (data->provider->cache, data->results[i]);
      g_variant_builder_add_value (&builder, meta);
    }

  g_dbus_method_invocation_return_value (data->invocation, g_variant_new ("(aa{sv})", &builder));

  result_metadata_free (data);
  g_application_release (g_application_get_default ());
}

static void
create_result_metas_from_id (GtdSearchProvider     *self,
                             GDBusMethodInvocation *invocation,
                             char                 **results)
{
  GTask *task;
  ResultMetaData *data;
  gint idx;
  gboolean cache_miss = FALSE;
  gchar *result;

  for (idx = 0; results[idx] != NULL; idx++)
    {
      result = results[idx];

      if (!g_hash_table_lookup (self->cache, result))
        {
          cache_miss = TRUE;
        }
    }

  data = g_slice_new (ResultMetaData);
  data->invocation = g_object_ref (invocation);
  data->results =  g_strdupv (results);
  data->provider = g_object_ref (self);

  if (cache_miss == FALSE)
    {
      result_meta_return_from_cache (data);
      result_metadata_free (data);
      return;
    }

  task = g_task_new (self, NULL, result_meta_callback, data);
  g_task_set_task_data (task, data, NULL);
  g_task_run_in_thread (task, create_result_metas_async);
  g_object_unref (task);
}

static void
execute_search (GtdSearchProvider      *self,
                GDBusMethodInvocation  *invocation,
                gchar                 **terms)
{
  GListModel *model = gtd_manager_get_tasks_model (gtd_manager_get_default ());
  GtdListModelFilter *filter = gtd_list_model_filter_new (model);
  GtdTask *task;
  GVariantBuilder builder;
  gint n_result, i;
  const gchar *result_id;

  if (g_strv_length (terms) == 1 &&
      g_utf8_strlen (terms[0], -1) == 1)
    {
      g_dbus_method_invocation_return_value (invocation, g_variant_new ("(as)", NULL));
      return;
    }

  g_application_hold (g_application_get_default ());

  gtd_list_model_filter_set_filter_func (filter, filter_func, terms, NULL);
  n_result = g_list_model_get_n_items (G_LIST_MODEL (filter));

  if (n_result == 0)
    {
      g_dbus_method_invocation_return_value (invocation, g_variant_new ("(as)", NULL));
      g_application_release (g_application_get_default ());
      return;
    }

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("as"));

  for (i = 0; i < n_result; i++)
    {
      task = GTD_TASK (g_list_model_get_object (G_LIST_MODEL (filter), i));
      result_id = gtd_object_get_uid (GTD_OBJECT (task));
      g_variant_builder_add (&builder, "s", result_id);
    }

  g_dbus_method_invocation_return_value (invocation, g_variant_new ("(as)", &builder));

  g_application_release (g_application_get_default ());
}

static gboolean
handle_get_initial_result_set (GtdShellSearchProvider2  *skeleton,
                               GDBusMethodInvocation    *invocation,
                               gchar                   **terms,
                               gpointer                  user_data)
{
  GtdSearchProvider *self = user_data;

  GTD_ENTRY;
  execute_search (self, invocation, terms);

  return TRUE;
}

static gboolean
handle_get_subsearch_result_set (GtdShellSearchProvider2  *skeleton,
                                 GDBusMethodInvocation    *invocation,
                                 gchar                   **previous_results,
                                 gchar                   **terms,
                                 gpointer                  user_data)
{
  GtdSearchProvider *self = user_data;

  GTD_ENTRY;
  execute_search (self, invocation, terms);

  return TRUE;
}

static gboolean
handle_get_result_metas (GtdShellSearchProvider2  *skeleton,
                         GDBusMethodInvocation    *invocation,
                         gchar                   **result,
                         gpointer                  user_data)
{
  GtdSearchProvider *self = user_data;

  GTD_ENTRY;
  create_result_metas_from_id (self, invocation, result);

  return TRUE;
}

static gboolean
handle_activate_result (GtdShellSearchProvider2  *skeleton,
                        GDBusMethodInvocation    *invocation,
                        gchar                    *result,
                        gchar                   **terms,
                        guint32                   timestamp,
                        gpointer                  user_data)
{
  GTD_ENTRY;
  gtd_shell_search_provider2_complete_activate_result (skeleton, invocation);
  return TRUE;
}

static gboolean
handle_launch_search (GtdShellSearchProvider2  *skeleton,
                      GDBusMethodInvocation    *invocation,
                      gchar                   **terms,
                      guint32                   timestamp,
                      gpointer                  user_data)
{
  return TRUE;
}

static void
gtd_search_provider_dispose (GObject *object)
{
  GtdSearchProvider *self = GTD_SEARCH_PROVIDER (object);

  g_clear_object (&self->skeleton);
  g_hash_table_destroy (self->cache);

  G_OBJECT_CLASS (gtd_search_provider_parent_class)->finalize (object);
}

static void
gtd_search_provider_class_init (GtdSearchProviderClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = gtd_search_provider_dispose;
}

static void
gtd_search_provider_init (GtdSearchProvider *self)
{
  self->skeleton = gtd_shell_search_provider2_skeleton_new ();

  self->cache = g_hash_table_new_full (g_str_hash, g_str_equal,
                                       g_free, (GDestroyNotify) g_variant_unref);

  g_signal_connect (self->skeleton, "handle-get-initial-result-set",
                    G_CALLBACK (handle_get_initial_result_set), self);
  g_signal_connect (self->skeleton, "handle-get-subsearch-result-set",
                    G_CALLBACK (handle_get_subsearch_result_set), self);
  g_signal_connect (self->skeleton, "handle-get-result-metas",
                    G_CALLBACK (handle_get_result_metas), self);
  g_signal_connect (self->skeleton, "handle-activate-result",
                    G_CALLBACK (handle_activate_result), self);
  g_signal_connect (self->skeleton, "handle-launch-search",
                    G_CALLBACK (handle_launch_search), self);
}

GtdSearchProvider *
gtd_search_provider_new (void)
{
  return g_object_new (GTD_TYPE_SEARCH_PROVIDER, NULL);
}

gboolean
gtd_search_provider_register (GtdSearchProvider *self,
                                    GDBusConnection   *connection,
                                    GError            *error)
{
  return g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self->skeleton),
                                           connection,
                                           "/org/gnome/todo/SearchProvider", &error);
}

void
gtd_search_provider_unregister (GtdSearchProvider *self)
{
  g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (self->skeleton));
}

gboolean
gtd_search_provider_dbus_export (GtdSearchProvider *self,
                                 GDBusConnection *connection,
                                 const gchar *object_path,
                                 GError **error)
{
  return g_dbus_interface_skeleton_export (
           G_DBUS_INTERFACE_SKELETON (self->skeleton),
           connection,
           object_path,
           error);
}

void
gtd_search_provider_dbus_unexport (GtdSearchProvider *self,
                                   GDBusConnection *connection,
                                   const gchar *object_path)
{
  if (g_dbus_interface_skeleton_has_connection (
        G_DBUS_INTERFACE_SKELETON (self->skeleton),
        connection))
    {
      g_dbus_interface_skeleton_unexport_from_connection (
        G_DBUS_INTERFACE_SKELETON (self->skeleton),
        connection);
    }
}
