/* gtd-provider-eds.c
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

#define G_LOG_DOMAIN "GtdProviderEds"

#include "gtd-debug.h"
#include "gtd-eds-autoptr.h"
#include "gtd-provider-eds.h"
#include "gtd-task-eds.h"
#include "gtd-task-list-eds.h"

#include <glib/gi18n.h>

/**
 * #GtdProviderEds is the base class of #GtdProviderLocal
 * and #GtdProviderGoa. It provides the common functionality
 * shared between these two providers.
 *
 * The subclasses basically have to implement GtdProviderEds->should_load_source
 * which decides whether a given #ESource should be loaded (and added to the
 * sources list) or not. #GtdProviderLocal for example would filter out
 * sources whose backend is not "local".
 */

typedef struct
{
  GHashTable           *task_lists;

  ESourceRegistry      *source_registry;

  GCancellable         *cancellable;

  gint                  lazy_load_id;
} GtdProviderEdsPrivate;


static void          gtd_provider_iface_init                     (GtdProviderInterface *iface);


G_DEFINE_TYPE_WITH_CODE (GtdProviderEds, gtd_provider_eds, GTD_TYPE_OBJECT,
                         G_ADD_PRIVATE (GtdProviderEds)
                         G_IMPLEMENT_INTERFACE (GTD_TYPE_PROVIDER, gtd_provider_iface_init))


enum
{
  PROP_0,
  PROP_ENABLED,
  PROP_DEFAULT_TASKLIST,
  PROP_DESCRIPTION,
  PROP_ICON,
  PROP_ID,
  PROP_NAME,
  PROP_PROVIDER_TYPE,
  PROP_REGISTRY,
  N_PROPS
};


/*
 * Auxiliary methods
 */

static void
set_default_list (GtdProviderEds *self,
                  GtdTaskList    *list)
{
  GtdProviderEdsPrivate *priv;
  GtdManager *manager;
  ESource *source;

  priv = gtd_provider_eds_get_instance_private (self);
  source = gtd_task_list_eds_get_source (GTD_TASK_LIST_EDS (list));
  manager = gtd_manager_get_default ();

  e_source_registry_set_default_task_list (priv->source_registry, source);

  if (gtd_manager_get_default_provider (manager) != (GtdProvider*) self)
    gtd_manager_set_default_provider (manager, GTD_PROVIDER (self));
}

static void
ensure_offline_sync (GtdProviderEds *self,
                     ESource        *source)
{
  GtdProviderEdsPrivate *priv = gtd_provider_eds_get_instance_private (self);
  ESourceOffline *extension;

  extension = e_source_get_extension (source, E_SOURCE_EXTENSION_OFFLINE);
  e_source_offline_set_stay_synchronized (extension, TRUE);

  e_source_registry_commit_source (priv->source_registry, source, NULL, NULL, NULL);
}


/*
 * Callbacks
 */

static void
on_client_connected_cb (GObject      *source_object,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  g_autoptr (ESource) default_source = NULL;
  g_autoptr (ESource) parent = NULL;
  g_autoptr (GError) error = NULL;
  GtdProviderEdsPrivate *priv;
  GtdProviderEds *self;
  GtdTaskListEds *list;
  ECalClient *client;
  ESource *source;

  self = GTD_PROVIDER_EDS (user_data);
  priv = gtd_provider_eds_get_instance_private (self);
  source = e_client_get_source (E_CLIENT (source_object));
  client = E_CAL_CLIENT (e_cal_client_connect_finish (result, &error));

  if (error)
    {
      g_warning ("Failed to connect to task list '%s': %s", e_source_get_uid (source), error->message);

      gtd_manager_emit_error_message (gtd_manager_get_default (),
                                      _("Failed to connect to task list"),
                                      error->message,
                                      NULL,
                                      NULL);
      gtd_object_pop_loading (GTD_OBJECT (self));
      return;
    }

  ensure_offline_sync (self, source);

  /* parent source's display name is list's origin */
  parent = e_source_registry_ref_source (priv->source_registry, e_source_get_parent (source));

  /* creates a new task list */
  list = gtd_task_list_eds_new (GTD_PROVIDER (self), source, client);

  g_hash_table_add (priv->task_lists, list);

  g_object_set_data (G_OBJECT (source), "task-list", list);

  /* Check if the current list is the default one */
  default_source = e_source_registry_ref_default_task_list (priv->source_registry);

  if (default_source == source)
    g_object_notify (G_OBJECT (self), "default-task-list");

  g_debug ("Task list '%s' successfully connected", e_source_get_display_name (source));
}

static void
on_source_added_cb (GtdProviderEds *provider,
                    ESource        *source)
{
  /* Don't load the source if it's not a tasklist */
  if (!e_source_has_extension (source, E_SOURCE_EXTENSION_TASK_LIST) ||
      !GTD_PROVIDER_EDS_CLASS (G_OBJECT_GET_CLASS (provider))->should_load_source (provider, source))
    {
      return;
    }

  /*
   * The pop_loading() is actually emited by GtdTaskListEds, after the
   * ECalClientView sends the :complete signal.
   */
  gtd_object_push_loading (GTD_OBJECT (provider));
  gtd_object_push_loading (GTD_OBJECT (gtd_manager_get_default ()));

  e_cal_client_connect (source,
                        E_CAL_CLIENT_SOURCE_TYPE_TASKS,
                        10, /* seconds to wait */
                        NULL,
                        on_client_connected_cb,
                        provider);
}

static void
on_source_removed_cb (GtdProviderEds *provider,
                      ESource        *source)
{
  GtdProviderEdsPrivate *priv;
  GtdTaskList *list;

  GTD_ENTRY;

  priv = gtd_provider_eds_get_instance_private (provider);
  list = g_object_get_data (G_OBJECT (source), "task-list");

  if (!g_hash_table_contains (priv->task_lists, list))
    GTD_RETURN ();

  g_hash_table_remove (priv->task_lists, list);

  /*
   * Since all subclasses will have this signal given that they
   * are all GtdProvider implementations, it's not that bad
   * to let it stay here.
   */
  g_signal_emit_by_name (provider, "list-removed", list);

  GTD_EXIT;
}

static void
on_default_tasklist_changed_cb (ESourceRegistry *source_registry,
                                GParamSpec      *pspec,
                                GtdProviderEds  *self)
{
  GtdTaskList *list;
  ESource *default_source;

  default_source = e_source_registry_ref_default_task_list (source_registry);
  list = g_object_get_data (G_OBJECT (default_source), "task-list");

  /* The list might not be loaded yet */
  if (!list || gtd_task_list_get_provider (list) != (GtdProvider*) self)
    goto out;

  g_object_notify (G_OBJECT (self), "default-task-list");

out:
  g_clear_object (&default_source);
}

#define REPORT_ERROR(title,error) \
G_STMT_START \
  if (error) \
    { \
      g_warning ("%s: %s", title, error->message); \
      gtd_manager_emit_error_message (gtd_manager_get_default (), title, error->message, NULL, NULL); \
      GTD_RETURN (); \
    } \
G_STMT_END

static void
on_task_created_cb (ECalClient   *client,
                    GAsyncResult *result,
                    GtdTask      *task)
{
  g_autoptr (GError) error = NULL;
  g_autofree gchar *new_uid = NULL;
  GtdProviderEds *self;
  GtdTaskList *list;

  GTD_ENTRY;

  self = GTD_PROVIDER_EDS (gtd_task_get_provider (task));
  list = gtd_task_get_list (task);

  gtd_object_pop_loading (GTD_OBJECT (self));
  gtd_object_pop_loading (GTD_OBJECT (task));

  e_cal_client_create_object_finish (client, result, &new_uid, &error);

  REPORT_ERROR (_("An error occurred while creating a task"), error);

  /* Update the default tasklist */
  set_default_list (self, list);

  /*
   * In the case the task UID changes because of creation proccess,
   * reapply it to the task.
   */
  if (new_uid)
    gtd_object_set_uid (GTD_OBJECT (task), new_uid);

  GTD_EXIT;
}

static void
on_task_modified_cb (ECalClient   *client,
                     GAsyncResult *result,
                     GtdTask      *task)
{
  g_autoptr (GError) error = NULL;
  GtdProviderEds *self;

  GTD_ENTRY;

  self = GTD_PROVIDER_EDS (gtd_task_get_provider (task));

  gtd_object_pop_loading (GTD_OBJECT (task));
  gtd_object_pop_loading (GTD_OBJECT (self));

  e_cal_client_modify_object_finish (client, result, &error);

  REPORT_ERROR (_("An error occurred while modifying a task"), error);

  GTD_EXIT;
}

static void
on_task_removed_cb (GObject      *object,
                    GAsyncResult *result,
                    gpointer      user_data)
{
  g_autoptr (GError) error = NULL;
  GtdProviderEds *self;

  GTD_ENTRY;

  self = GTD_PROVIDER_EDS (user_data);

  gtd_object_pop_loading (GTD_OBJECT (self));

  e_cal_client_remove_object_finish (E_CAL_CLIENT (object), result, &error);

  REPORT_ERROR (_("An error occurred while removing a task"), error);

  GTD_EXIT;
}

static void
on_task_list_created_cb (ESourceRegistry *registry,
                         GAsyncResult    *result,
                         GtdProviderEds  *self)
{
  g_autoptr (GError) error = NULL;

  GTD_ENTRY;

  gtd_object_pop_loading (GTD_OBJECT (self));

  e_source_registry_commit_source_finish (registry, result, &error);

  REPORT_ERROR (_("An error occurred while creating a task list"), error);

  GTD_EXIT;
}

static void
on_task_list_modified_cb (ESourceRegistry *registry,
                          GAsyncResult    *result,
                          GtdTaskList     *list)
{
  g_autoptr (GError) error = NULL;
  GtdProviderEds *self;

  GTD_ENTRY;

  self = GTD_PROVIDER_EDS (gtd_task_list_get_provider (list));

  gtd_object_pop_loading (GTD_OBJECT (self));
  gtd_object_pop_loading (GTD_OBJECT (list));

  e_source_registry_commit_source_finish (registry, result, &error);

  REPORT_ERROR (_("An error occurred while modifying a task list"), error);

  g_signal_emit_by_name (self, "list-changed", list);

  GTD_EXIT;
}

static void
on_task_list_removed_cb (ESource      *source,
                         GAsyncResult *result,
                         GtdTaskList  *list)
{
  g_autoptr (GError) error = NULL;
  GtdProviderEds *self;

  GTD_ENTRY;

  self = GTD_PROVIDER_EDS (gtd_task_list_get_provider (list));

  gtd_object_pop_loading (GTD_OBJECT (self));

  e_source_remove_finish (source, result, &error);

  REPORT_ERROR (_("An error occurred while modifying a task list"), error);

  /*
   * Note: we don't need to emit the "list-removed" signal here
   * because EDS will emit the "source-removed" signal, and we
   * emit "list-removed" there.
   */

  GTD_EXIT;
}


/*
 * GtdProvider iface
 */

static const gchar*
gtd_provider_eds_get_id (GtdProvider *provider)
{
  g_return_val_if_fail (GTD_IS_PROVIDER_EDS (provider), NULL);

  return GTD_PROVIDER_EDS_CLASS (G_OBJECT_GET_CLASS (provider))->get_id (GTD_PROVIDER_EDS (provider));
}

static const gchar*
gtd_provider_eds_get_name (GtdProvider *provider)
{
  g_return_val_if_fail (GTD_IS_PROVIDER_EDS (provider), NULL);

  return GTD_PROVIDER_EDS_CLASS (G_OBJECT_GET_CLASS (provider))->get_name (GTD_PROVIDER_EDS (provider));
}

static const gchar*
gtd_provider_eds_get_provider_type (GtdProvider *provider)
{
  g_return_val_if_fail (GTD_IS_PROVIDER_EDS (provider), NULL);

  return GTD_PROVIDER_EDS_CLASS (G_OBJECT_GET_CLASS (provider))->get_provider_type (GTD_PROVIDER_EDS (provider));
}

static const gchar*
gtd_provider_eds_get_description (GtdProvider *provider)
{
  g_return_val_if_fail (GTD_IS_PROVIDER_EDS (provider), NULL);

  return GTD_PROVIDER_EDS_CLASS (G_OBJECT_GET_CLASS (provider))->get_description (GTD_PROVIDER_EDS (provider));
}


static gboolean
gtd_provider_eds_get_enabled (GtdProvider *provider)
{
  g_return_val_if_fail (GTD_IS_PROVIDER_EDS (provider), FALSE);

  return GTD_PROVIDER_EDS_CLASS (G_OBJECT_GET_CLASS (provider))->get_enabled (GTD_PROVIDER_EDS (provider));
}

static GIcon*
gtd_provider_eds_get_icon (GtdProvider *provider)
{
  g_return_val_if_fail (GTD_IS_PROVIDER_EDS (provider), NULL);

  return GTD_PROVIDER_EDS_CLASS (G_OBJECT_GET_CLASS (provider))->get_icon (GTD_PROVIDER_EDS (provider));
}

static void
gtd_provider_eds_create_task (GtdProvider *provider,
                              GtdTaskList *list,
                              const gchar *title,
                              GDateTime   *due_date)
{
  GtdTaskListEds *tasklist;
  GtdProviderEds *self;
  ECalComponent *component;
  ECalClient *client;
  GtdTask *new_task;

  g_return_if_fail (GTD_IS_TASK_LIST_EDS (list));

  GTD_ENTRY;

  self = GTD_PROVIDER_EDS (provider);
  tasklist = GTD_TASK_LIST_EDS (list);
  client = gtd_task_list_eds_get_client (tasklist);

  /* Create the new task */
  component = e_cal_component_new ();
  e_cal_component_set_new_vtype (component, E_CAL_COMPONENT_TODO);

  new_task = gtd_task_eds_new (component);
  gtd_task_set_title (new_task, title);
  gtd_task_set_due_date (new_task, due_date);
  gtd_task_set_list (new_task, list);

  gtd_task_list_save_task (list, new_task);

  /* The task is not ready until we finish the operation */
  gtd_object_push_loading (GTD_OBJECT (self));
  gtd_object_push_loading (GTD_OBJECT (new_task));

  e_cal_client_create_object (client,
                              e_cal_component_get_icalcomponent (component),
                              NULL,
                              (GAsyncReadyCallback) on_task_created_cb,
                              new_task);

  GTD_EXIT;
}

static void
gtd_provider_eds_update_task (GtdProvider *provider,
                              GtdTask     *task)
{
  GtdTaskListEds *tasklist;
  ECalComponent *component;
  ECalClient *client;

  GTD_ENTRY;

  g_return_if_fail (GTD_IS_TASK (task));
  g_return_if_fail (GTD_IS_TASK_LIST_EDS (gtd_task_get_list (task)));

  tasklist = GTD_TASK_LIST_EDS (gtd_task_get_list (task));
  client = gtd_task_list_eds_get_client (tasklist);
  component = gtd_task_eds_get_component (GTD_TASK_EDS (task));

  e_cal_component_commit_sequence (component);

  /* The task is not ready until we finish the operation */
  gtd_object_push_loading (GTD_OBJECT (task));
  gtd_object_push_loading (GTD_OBJECT (provider));

  e_cal_client_modify_object (client,
                              e_cal_component_get_icalcomponent (component),
                              E_CAL_OBJ_MOD_THIS,
                              NULL,
                              (GAsyncReadyCallback) on_task_modified_cb,
                              task);

  GTD_EXIT;
}

static void
gtd_provider_eds_remove_task (GtdProvider *provider,
                              GtdTask     *task)
{
  g_autoptr (ECalComponentId) id = NULL;
  GtdTaskListEds *tasklist;
  ECalComponent *component;
  ECalClient *client;

  GTD_ENTRY;

  g_return_if_fail (GTD_IS_TASK (task));
  g_return_if_fail (GTD_IS_TASK_LIST_EDS (gtd_task_get_list (task)));

  tasklist = GTD_TASK_LIST_EDS (gtd_task_get_list (task));
  client = gtd_task_list_eds_get_client (tasklist);
  component = gtd_task_eds_get_component (GTD_TASK_EDS (task));
  id = e_cal_component_get_id (component);

  gtd_object_push_loading (GTD_OBJECT (provider));

  e_cal_client_remove_object (client,
                              id->uid,
                              id->rid,
                              E_CAL_OBJ_MOD_THIS,
                              NULL,
                              (GAsyncReadyCallback) on_task_removed_cb,
                              provider);

  GTD_EXIT;
}

static void
gtd_provider_eds_create_task_list (GtdProvider *provider,
                                   const gchar *name)
{
  GtdProviderEdsPrivate *priv;
  GtdProviderEds *self;
  ESource *source;

  GTD_ENTRY;

  self = GTD_PROVIDER_EDS (provider);
  priv = gtd_provider_eds_get_instance_private (self);
  source = NULL;

  /* Create an ESource */
  if (GTD_PROVIDER_EDS_CLASS (G_OBJECT_GET_CLASS (provider))->create_source)
    source = GTD_PROVIDER_EDS_CLASS (G_OBJECT_GET_CLASS (provider))->create_source (self);

  if (!source)
    return;

  gtd_object_push_loading (GTD_OBJECT (provider));

  /* EDS properties */
  e_source_set_display_name (source, name);

  e_source_registry_commit_source (priv->source_registry,
                                   source,
                                   NULL,
                                   (GAsyncReadyCallback) on_task_list_created_cb,
                                   provider);

  GTD_EXIT;
}

static void
gtd_provider_eds_update_task_list (GtdProvider *provider,
                                   GtdTaskList *list)
{
  GtdProviderEdsPrivate *priv;
  ESource *source;

  GTD_ENTRY;

  g_assert (GTD_IS_TASK_LIST_EDS (list));
  g_assert (gtd_task_list_eds_get_source (GTD_TASK_LIST_EDS (list)) != NULL);

  priv = gtd_provider_eds_get_instance_private (GTD_PROVIDER_EDS (provider));
  source = gtd_task_list_eds_get_source (GTD_TASK_LIST_EDS (list));

  gtd_object_push_loading (GTD_OBJECT (provider));
  gtd_object_push_loading (GTD_OBJECT (list));

  e_source_registry_commit_source (priv->source_registry,
                                   source,
                                   NULL,
                                   (GAsyncReadyCallback) on_task_list_modified_cb,
                                   list);

  GTD_EXIT;
}

static void
gtd_provider_eds_remove_task_list (GtdProvider *provider,
                                   GtdTaskList *list)
{
  ESource *source;

  GTD_ENTRY;

  g_assert (GTD_IS_TASK_LIST_EDS (list));
  g_assert (gtd_task_list_eds_get_source (GTD_TASK_LIST_EDS (list)) != NULL);

  source = gtd_task_list_eds_get_source (GTD_TASK_LIST_EDS (list));

  gtd_object_push_loading (GTD_OBJECT (provider));

  e_source_remove (source,
                   NULL,
                   (GAsyncReadyCallback) on_task_list_removed_cb,
                   list);

  GTD_EXIT;
}

static GList*
gtd_provider_eds_get_task_lists (GtdProvider *provider)
{
  GtdProviderEdsPrivate *priv = gtd_provider_eds_get_instance_private (GTD_PROVIDER_EDS (provider));

  return g_hash_table_get_keys (priv->task_lists);
}

static GtdTaskList*
gtd_provider_eds_get_default_task_list (GtdProvider *provider)
{
  GtdProviderEdsPrivate *priv;
  GtdTaskList *default_task_list;
  ESource *default_source;

  priv = gtd_provider_eds_get_instance_private (GTD_PROVIDER_EDS (provider));
  default_source = e_source_registry_ref_default_task_list (priv->source_registry);
  default_task_list = g_object_get_data (G_OBJECT (default_source), "task-list");

  g_clear_object (&default_source);

  if (default_task_list &&
      gtd_task_list_get_provider (default_task_list) != GTD_PROVIDER (provider))
    {
      return NULL;
    }

  return default_task_list;
}

static void
gtd_provider_eds_set_default_task_list (GtdProvider *provider,
                                        GtdTaskList *list)
{
  GtdProviderEdsPrivate *priv;
  ESource *source;

  g_assert (GTD_IS_TASK_LIST_EDS (list));

  priv = gtd_provider_eds_get_instance_private (GTD_PROVIDER_EDS (provider));
  source = gtd_task_list_eds_get_source (GTD_TASK_LIST_EDS (list));

  e_source_registry_set_default_task_list (priv->source_registry, source);

  g_object_notify (G_OBJECT (provider), "default-task-list");
}

static void
gtd_provider_iface_init (GtdProviderInterface *iface)
{
  iface->get_id = gtd_provider_eds_get_id;
  iface->get_name = gtd_provider_eds_get_name;
  iface->get_provider_type = gtd_provider_eds_get_provider_type;
  iface->get_description = gtd_provider_eds_get_description;
  iface->get_enabled = gtd_provider_eds_get_enabled;
  iface->get_icon = gtd_provider_eds_get_icon;
  iface->create_task = gtd_provider_eds_create_task;
  iface->update_task = gtd_provider_eds_update_task;
  iface->remove_task = gtd_provider_eds_remove_task;
  iface->create_task_list = gtd_provider_eds_create_task_list;
  iface->update_task_list = gtd_provider_eds_update_task_list;
  iface->remove_task_list = gtd_provider_eds_remove_task_list;
  iface->get_task_lists = gtd_provider_eds_get_task_lists;
  iface->get_default_task_list = gtd_provider_eds_get_default_task_list;
  iface->set_default_task_list = gtd_provider_eds_set_default_task_list;
}


/*
 * GObject overrides
 */

static void
gtd_provider_eds_finalize (GObject *object)
{
  GtdProviderEds *self = (GtdProviderEds *)object;
  GtdProviderEdsPrivate *priv = gtd_provider_eds_get_instance_private (self);

  g_signal_handlers_disconnect_by_func (priv->source_registry, on_default_tasklist_changed_cb, self);

  g_cancellable_cancel (priv->cancellable);

  g_clear_object (&priv->cancellable);
  g_clear_object (&priv->source_registry);
  g_clear_pointer (&priv->task_lists, g_hash_table_destroy);

  G_OBJECT_CLASS (gtd_provider_eds_parent_class)->finalize (object);
}

static void
gtd_provider_eds_constructed (GObject *object)
{
  GtdProviderEdsPrivate *priv;
  GtdProviderEds *self;
  g_autoptr (GError) error = NULL;
  GList *sources;
  GList *l;

  self = GTD_PROVIDER_EDS (object);
  priv = gtd_provider_eds_get_instance_private (self);

  if (error)
    {
      g_warning ("%s: %s", "Error loading task manager", error->message);
      return;
    }

  /* Load task list sources */
  sources = e_source_registry_list_sources (priv->source_registry, E_SOURCE_EXTENSION_TASK_LIST);

  for (l = sources; l != NULL; l = l->next)
    on_source_added_cb (self, l->data);

  g_list_free_full (sources, g_object_unref);

  /* listen to the signals, so new sources don't slip by */
  g_signal_connect_swapped (priv->source_registry,
                            "source-added",
                            G_CALLBACK (on_source_added_cb),
                            self);

  g_signal_connect_swapped (priv->source_registry,
                            "source-removed",
                            G_CALLBACK (on_source_removed_cb),
                            self);

  g_signal_connect (priv->source_registry,
                    "notify::default-task-list",
                    G_CALLBACK (on_default_tasklist_changed_cb),
                    self);
}

static void
gtd_provider_eds_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  GtdProvider *provider = GTD_PROVIDER (object);
  GtdProviderEdsPrivate *priv = gtd_provider_eds_get_instance_private (GTD_PROVIDER_EDS (object));


  switch (prop_id)
    {
    case PROP_DEFAULT_TASKLIST:
      g_value_set_object (value, gtd_provider_eds_get_default_task_list (provider));
      break;

    case PROP_DESCRIPTION:
      g_value_set_string (value, gtd_provider_eds_get_description (provider));
      break;

    case PROP_ENABLED:
      g_value_set_boolean (value, gtd_provider_eds_get_enabled (provider));
      break;

    case PROP_ICON:
      g_value_set_object (value, gtd_provider_eds_get_icon (provider));
      break;

    case PROP_ID:
      g_value_set_string (value, gtd_provider_eds_get_id (provider));
      break;

    case PROP_NAME:
      g_value_set_string (value, gtd_provider_eds_get_name (provider));
      break;

    case PROP_PROVIDER_TYPE:
      g_value_set_string (value, gtd_provider_eds_get_provider_type (provider));
      break;

    case PROP_REGISTRY:
      g_value_set_object (value, priv->source_registry);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_provider_eds_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  GtdProviderEds *self = GTD_PROVIDER_EDS (object);
  GtdProviderEdsPrivate *priv = gtd_provider_eds_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_DEFAULT_TASKLIST:
      gtd_provider_eds_set_default_task_list (GTD_PROVIDER (self), g_value_get_object (value));
      break;

    case PROP_REGISTRY:
      if (g_set_object (&priv->source_registry, g_value_get_object (value)))
        g_object_notify (object, "registry");
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_provider_eds_class_init (GtdProviderEdsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gtd_provider_eds_finalize;
  object_class->constructed = gtd_provider_eds_constructed;
  object_class->get_property = gtd_provider_eds_get_property;
  object_class->set_property = gtd_provider_eds_set_property;

  g_object_class_override_property (object_class, PROP_DEFAULT_TASKLIST, "default-task-list");
  g_object_class_override_property (object_class, PROP_DESCRIPTION, "description");
  g_object_class_override_property (object_class, PROP_ENABLED, "enabled");
  g_object_class_override_property (object_class, PROP_ICON, "icon");
  g_object_class_override_property (object_class, PROP_ID, "id");
  g_object_class_override_property (object_class, PROP_NAME, "name");
  g_object_class_override_property (object_class, PROP_PROVIDER_TYPE, "provider-type");

  g_object_class_install_property (object_class,
                                   PROP_REGISTRY,
                                   g_param_spec_object ("registry",
                                                        "Source registry",
                                                        "The EDS source registry object",
                                                        E_TYPE_SOURCE_REGISTRY,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

static void
gtd_provider_eds_init (GtdProviderEds *self)
{
  GtdProviderEdsPrivate *priv = gtd_provider_eds_get_instance_private (self);

  priv->cancellable = g_cancellable_new ();
  priv->task_lists = g_hash_table_new (g_direct_hash, g_direct_equal);
}

GtdProviderEds*
gtd_provider_eds_new (ESourceRegistry *registry)
{
  return g_object_new (GTD_TYPE_PROVIDER_EDS,
                       "registry", registry,
                       NULL);
}

ESourceRegistry*
gtd_provider_eds_get_registry (GtdProviderEds *provider)
{
  GtdProviderEdsPrivate *priv;

  g_return_val_if_fail (GTD_IS_PROVIDER_EDS (provider), NULL);

  priv = gtd_provider_eds_get_instance_private (provider);

  return priv->source_registry;
}
