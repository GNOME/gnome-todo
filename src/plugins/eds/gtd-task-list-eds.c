/* gtd-task-list-eds.c
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

#define G_LOG_DOMAIN "GtdTaskListEds"

#include "e-source-gnome-todo.h"
#include "gtd-debug.h"
#include "gtd-eds-autoptr.h"
#include "gtd-provider-eds.h"
#include "gtd-task-eds.h"
#include "gtd-task-list-eds.h"

#include <glib/gi18n.h>

struct _GtdTaskListEds
{
  GtdTaskList         parent;

  ECalClient         *client;
  ECalClientView     *client_view;
  ESource            *source;

  GCancellable       *cancellable;
};

typedef struct
{
  GtdProvider        *provider;
  ESource            *source;
  ECalClient         *client;
} NewTaskListData;

static void          on_client_objects_modified_for_migration_cb (GObject            *object,
                                                                  GAsyncResult       *result,
                                                                  gpointer            user_data);

G_DEFINE_TYPE (GtdTaskListEds, gtd_task_list_eds, GTD_TYPE_TASK_LIST)

enum
{
  PROP_0,
  PROP_CLIENT,
  PROP_SOURCE,
  N_PROPS
};


/*
 * Auxiliary methods
 */

static void
new_task_list_data_free (gpointer data)
{
  NewTaskListData *list_data = data;

  if (!list_data)
    return;

  g_clear_object (&list_data->provider);
  g_clear_object (&list_data->source);
  g_clear_object (&list_data->client);
  g_free (list_data);
}

static void
update_changed_tasks (GtdTaskListEds *self,
                      GHashTable     *changed_tasks)
{
  g_autoptr (GSList) components = NULL;
  GHashTableIter iter;
  GtdProvider *provider;
  GtdTask *task;
  guint n_changed_tasks;

  GTD_ENTRY;

  n_changed_tasks = g_hash_table_size (changed_tasks);
  provider = gtd_task_list_get_provider (GTD_TASK_LIST (self));

  /* Nothing changed, list is ready */
  if (n_changed_tasks == 0)
    {
      gtd_object_pop_loading (GTD_OBJECT (provider));
      g_signal_emit_by_name (provider, "list-added", self);
      GTD_RETURN ();
    }

  GTD_TRACE_MSG ("%u task(s) changed", n_changed_tasks);

  g_hash_table_iter_init (&iter, changed_tasks);
  while (g_hash_table_iter_next (&iter, (gpointer *) &task, NULL))
    {
      ICalComponent *ical_comp;
      ECalComponent *comp;

      comp = gtd_task_eds_get_component (GTD_TASK_EDS (task));
      ical_comp = e_cal_component_get_icalcomponent (comp);

      components = g_slist_prepend (components, ical_comp);
    }

  e_cal_client_modify_objects (self->client,
                               components,
                               E_CAL_OBJ_MOD_THIS,
                               E_CAL_OPERATION_FLAG_NONE,
                               self->cancellable,
                               on_client_objects_modified_for_migration_cb,
                               self);

  GTD_EXIT;
}

static void
migrate_to_v1 (GtdTaskListEds *self,
               GHashTable     *changed_tasks)
{
  GListModel *model;
  guint n_tasks;
  guint i;

  model = G_LIST_MODEL (self);
  n_tasks = g_list_model_get_n_items (model);

  for (i = 0; i < n_tasks; i++)
    {
      g_autoptr (GtdTask) task = NULL;

      task = g_list_model_get_item (model, i);

      /* Don't notify to avoid carpet-bombing GtdTaskList */
      g_object_freeze_notify (G_OBJECT (task));

      gtd_task_set_position (task, i);

      g_hash_table_add (changed_tasks, task);
    }

  for (i = 0; i < n_tasks; i++)
    {
      g_autoptr (GtdTask) task = NULL;

      task = g_list_model_get_item (model, i);
      g_object_thaw_notify (G_OBJECT (task));
    }
}

struct
{
  guint api_version;
  void  (* migrate) (GtdTaskListEds *self,
                     GHashTable     *changed_tasks);
}
migration_vtable[] =
{
  { 0, migrate_to_v1 },
};

static void
maybe_migrate_todo_api_version (GtdTaskListEds *self)
{
  g_autoptr (GHashTable) changed_tasks = NULL;
  ESourceGnomeTodo *gnome_todo_extension;
  gboolean api_version_changed;
  guint api_version;
  guint i;

  GTD_ENTRY;

  /*
   * Ensure the type so that it is available for introspection when
   * calling e_source_get_extension().
   */
  g_type_ensure (E_TYPE_SOURCE_GNOME_TODO);

  api_version_changed = FALSE;
  gnome_todo_extension = e_source_get_extension (self->source, E_SOURCE_EXTENSION_GNOME_TODO);
  api_version = e_source_gnome_todo_get_api_version (gnome_todo_extension);
  changed_tasks = g_hash_table_new (g_direct_hash, g_direct_equal);

  g_debug ("%s: GNOME To Do API version %u",
           gtd_task_list_get_name (GTD_TASK_LIST (self)),
           api_version);

  for (i = 0; i < G_N_ELEMENTS (migration_vtable); i++)
    {
      guint new_api_version = i + 1;

      if (api_version > migration_vtable[i].api_version)
        continue;

      g_debug ("  Migrating task list to GNOME To Do API v%u", new_api_version);

      migration_vtable[i].migrate (self, changed_tasks);

      e_source_gnome_todo_set_api_version (gnome_todo_extension, new_api_version);
      api_version_changed = TRUE;
    }

  if (api_version_changed)
    {
      g_debug ("Saving new API version");

      e_source_write (self->source, NULL, NULL, NULL);
    }

  update_changed_tasks (self, changed_tasks);

  GTD_EXIT;
}


/*
 * Callbacks
 */

static gboolean
new_task_list_in_idle_cb (gpointer data)
{
  g_autoptr (GtdTaskListEds) list_eds = NULL;
  g_autoptr (GTask) task = data;
  NewTaskListData *list_data;

  list_data = g_task_get_task_data (task);
  list_eds = g_object_new (GTD_TYPE_TASK_LIST_EDS,
                           "provider", list_data->provider,
                           "source", list_data->source,
                           "client", list_data->client,
                           NULL);

  g_task_return_pointer (task, g_steal_pointer (&list_eds), NULL);

  return G_SOURCE_REMOVE;
}

static void
on_client_objects_modified_for_migration_cb (GObject      *object,
                                             GAsyncResult *result,
                                             gpointer      user_data)
{
  g_autoptr (GError) error = NULL;
  GtdProvider *provider;
  GtdTaskListEds *self;

  GTD_ENTRY;

  self = GTD_TASK_LIST_EDS (user_data);
  provider = gtd_task_list_get_provider (GTD_TASK_LIST (self));

  e_cal_client_modify_objects_finish (self->client, result, &error);

  if (error && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    g_warning ("Error migrating tasks to new API version: %s", error->message);

  gtd_object_pop_loading (GTD_OBJECT (provider));
  g_signal_emit_by_name (provider, "list-added", self);

  GTD_EXIT;
}

static void
on_view_objects_added_cb (ECalClientView *view,
                          const GSList   *objects,
                          GtdTaskList    *self)
{
  g_autoptr (ECalClient) client = NULL;
  GSList *l;

  GTD_ENTRY;

  client = e_cal_client_view_ref_client (view);

  for (l = (GSList*) objects; l; l = l->next)
    {
      g_autoptr (ECalComponent) component = NULL;
      GtdTask *task;
      const gchar *uid;

      component = e_cal_component_new_from_icalcomponent (i_cal_component_clone (l->data));
      uid = e_cal_component_get_uid (component);

      task = gtd_task_list_get_task_by_id (self, uid);

      /* If the task already exists, we must instead update its component */
      if (task)
        {
          gtd_task_eds_set_component (GTD_TASK_EDS (task), component);

          gtd_task_list_update_task (self, task);

          GTD_TRACE_MSG ("Updated task '%s' to tasklist '%s'",
                         gtd_task_get_title (task),
                         gtd_task_list_get_name (self));

          continue;
        }

      /* Add the new task */
      task = gtd_task_eds_new (component);
      gtd_task_set_list (task, self);

      gtd_task_list_add_task (self, task);

      GTD_TRACE_MSG ("Added task '%s' (%s) to tasklist '%s'",
                     gtd_task_get_title (task),
                     gtd_object_get_uid (GTD_OBJECT (task)),
                     gtd_task_list_get_name (self));
    }

  GTD_EXIT;
}

static void
on_view_objects_modified_cb (ECalClientView *view,
                             const GSList   *objects,
                             GtdTaskList    *self)
{
  g_autoptr (ECalClient) client = NULL;
  GSList *l;

  GTD_ENTRY;

  client = e_cal_client_view_ref_client (view);

  for (l = (GSList*) objects; l; l = l->next)
    {
      g_autoptr (ECalComponent) component = NULL;
      GtdTask *task;
      const gchar *uid;

      component = e_cal_component_new_from_icalcomponent (i_cal_component_clone (l->data));
      uid = e_cal_component_get_uid (component);

      task = gtd_task_list_get_task_by_id (self, uid);

      if (!task)
        continue;

      gtd_task_eds_set_component (GTD_TASK_EDS (task), component);

      GTD_TRACE_MSG ("Updated task '%s' from tasklist '%s'",
                     gtd_task_get_title (GTD_TASK (task)),
                     gtd_task_list_get_name (self));
    }

  GTD_EXIT;
}

static void
on_view_objects_removed_cb (ECalClientView *view,
                            const GSList   *uids,
                            GtdTaskList    *self)
{
  GSList *l;

  GTD_ENTRY;

  for (l = (GSList*) uids; l; l = l->next)
    {
      ECalComponentId *id;
      GtdTask *task;

      id = l->data;
      task = gtd_task_list_get_task_by_id (self, e_cal_component_id_get_uid (id));

      if (!task)
        continue;

      gtd_task_list_remove_task (self, task);

      GTD_TRACE_MSG ("Removed task '%s' from tasklist '%s'",
                     gtd_task_get_title (task),
                     gtd_task_list_get_name (self));
    }

  GTD_EXIT;
}

static void
on_view_completed_cb (ECalClientView *view,
                      const GError   *error,
                      GtdTaskList    *self)
{
  gtd_object_pop_loading (GTD_OBJECT (gtd_manager_get_default ()));
  gtd_object_pop_loading (GTD_OBJECT (self));

  if (error)
    {
      g_warning ("Error fetching tasks from list: %s", error->message);

      gtd_manager_emit_error_message (gtd_manager_get_default (),
                                      _("Error fetching tasks from list"),
                                      error->message,
                                      NULL,
                                      NULL);
      return;
    }

  maybe_migrate_todo_api_version (GTD_TASK_LIST_EDS (self));
}

static void
on_client_view_acquired_cb (GObject      *client,
                            GAsyncResult *result,
                            gpointer      user_data)
{
  g_autoptr (GError) error = NULL;
  GtdTaskListEds *self;

  self = GTD_TASK_LIST_EDS (user_data);

  e_cal_client_get_view_finish (E_CAL_CLIENT (client), result, &self->client_view, &error);

  if (error)
    {
      g_warning ("Error fetching tasks from list: %s", error->message);

      gtd_manager_emit_error_message (gtd_manager_get_default (),
                                      _("Error fetching tasks from list"),
                                      error->message,
                                      NULL,
                                      NULL);
      return;
    }

  g_debug ("ECalClientView for tasklist '%s' successfully acquired",
           gtd_task_list_get_name (GTD_TASK_LIST (self)));

  g_signal_connect (self->client_view, "objects-added", G_CALLBACK (on_view_objects_added_cb), self);
  g_signal_connect (self->client_view, "objects-removed", G_CALLBACK (on_view_objects_removed_cb), self);
  g_signal_connect (self->client_view, "objects-modified", G_CALLBACK (on_view_objects_modified_cb), self);
  g_signal_connect (self->client_view, "complete", G_CALLBACK (on_view_completed_cb), self);

  gtd_object_push_loading (GTD_OBJECT (self));

  e_cal_client_view_start (self->client_view, &error);

  if (error)
    {
      g_warning ("Error starting view: %s", error->message);

      gtd_manager_emit_error_message (gtd_manager_get_default (),
                                      _("Error fetching tasks from list"),
                                      error->message,
                                      NULL,
                                      NULL);
    }
}

static void
on_source_removable_changed_cb (GtdTaskListEds *list)
{
  gtd_task_list_set_is_removable (GTD_TASK_LIST (list),
                                  e_source_get_removable (list->source) ||
                                  e_source_get_remote_deletable (list->source));
}

static void
on_source_selectable_selected_changed_cb (ESourceSelectable *selectable,
                                          GParamSpec        *pspec,
                                          GtdTaskListEds    *self)
{
  g_debug ("%s (%s): ESourceSelectable:selected changed, notifying...",
           e_source_get_uid (self->source),
           e_source_get_display_name (self->source));

  g_object_notify (G_OBJECT (self), "archived");
}

static void
on_save_task_list_finished_cb (GObject      *source,
                               GAsyncResult *result,
                               gpointer      user_data)
{
  GtdTaskListEds *list;
  GError *error;

  list = user_data;
  error = NULL;

  gtd_object_pop_loading (GTD_OBJECT (list));

  e_source_write_finish (E_SOURCE (source), result, &error);

  if (error)
    {
      g_warning ("%s: %s: %s",
                 G_STRFUNC,
                 "Error saving task list",
                 error->message);
      g_clear_error (&error);
    }
}

static void
save_task_list (GtdTaskListEds *list)
{
  if (e_source_get_writable (list->source))
    {
      if (!list->cancellable)
        list->cancellable = g_cancellable_new ();

      gtd_object_push_loading (GTD_OBJECT (list));

      e_source_write (list->source,
                      list->cancellable,
                      on_save_task_list_finished_cb,
                      list);
    }
}

static gboolean
color_to_string (GBinding     *binding,
                 const GValue *from_value,
                 GValue       *to_value,
                 gpointer      user_data)
{
  GdkRGBA *color;
  gchar *color_str;

  color = g_value_get_boxed (from_value);
  color_str = gdk_rgba_to_string (color);

  g_value_set_string (to_value, color_str);

  g_free (color_str);

  return TRUE;
}

static gboolean
string_to_color (GBinding     *binding,
                 const GValue *from_value,
                 GValue       *to_value,
                 gpointer      user_data)
{
  GdkRGBA color;

  if (!gdk_rgba_parse (&color, g_value_get_string (from_value)))
    gdk_rgba_parse (&color, "#ffffff"); /* calendar default colour */

  g_value_set_boxed (to_value, &color);

  return TRUE;
}


/*
 * GtdTaskList overrides
 */

static gboolean
gtd_task_list_eds_get_archived (GtdTaskList *list)
{
  ESourceSelectable *selectable;
  GtdTaskListEds *self;

  self = GTD_TASK_LIST_EDS (list);
  selectable = e_source_get_extension (self->source, E_SOURCE_EXTENSION_TASK_LIST);

  return !e_source_selectable_get_selected (selectable);
}

static void
gtd_task_list_eds_set_archived (GtdTaskList *list,
                                gboolean     archived)
{
  ESourceSelectable *selectable;
  GtdTaskListEds *self;

  GTD_ENTRY;

  self = GTD_TASK_LIST_EDS (list);
  selectable = e_source_get_extension (self->source, E_SOURCE_EXTENSION_TASK_LIST);

  g_signal_handlers_block_by_func (selectable, on_source_selectable_selected_changed_cb, self);

  e_source_selectable_set_selected (selectable, !archived);

  g_signal_handlers_unblock_by_func (selectable, on_source_selectable_selected_changed_cb, self);

  GTD_EXIT;
}


/*
 * GtdObject overrides
 */

static const gchar*
gtd_task_list_eds_get_uid (GtdObject *object)
{
  GtdTaskListEds *self = GTD_TASK_LIST_EDS (object);

  return e_source_get_uid (self->source);
}

static void
gtd_task_list_eds_set_uid (GtdObject   *object,
                           const gchar *uid)
{
  g_assert_not_reached ();
}


/*
 * GObject overrides
 */

static void
gtd_task_list_eds_finalize (GObject *object)
{
  GtdTaskListEds *self = GTD_TASK_LIST_EDS (object);

  g_cancellable_cancel (self->cancellable);

  g_clear_object (&self->cancellable);
  g_clear_object (&self->client);
  g_clear_object (&self->client_view);
  g_clear_object (&self->source);

  G_OBJECT_CLASS (gtd_task_list_eds_parent_class)->finalize (object);
}

static void
gtd_task_list_eds_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  GtdTaskListEds *self = GTD_TASK_LIST_EDS (object);

  switch (prop_id)
    {
    case PROP_CLIENT:
      g_value_set_object (value, self->client);
      break;

    case PROP_SOURCE:
      g_value_set_object (value, self->source);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_task_list_eds_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  GtdTaskListEds *self = GTD_TASK_LIST_EDS (object);

  switch (prop_id)
    {
    case PROP_CLIENT:
      if (!g_set_object (&self->client, g_value_get_object (value)) || !self->client)
        return;

      e_cal_client_get_view (self->client,
                             "#t",
                             self->cancellable,
                             on_client_view_acquired_cb,
                             self);

      g_object_notify (object, "client");
      break;

    case PROP_SOURCE:
      gtd_task_list_eds_set_source (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_task_list_eds_class_init (GtdTaskListEdsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtdObjectClass *gtd_object_class = GTD_OBJECT_CLASS (klass);
  GtdTaskListClass *task_list_class = GTD_TASK_LIST_CLASS (klass);

  task_list_class->get_archived = gtd_task_list_eds_get_archived;
  task_list_class->set_archived = gtd_task_list_eds_set_archived;

  gtd_object_class->get_uid = gtd_task_list_eds_get_uid;
  gtd_object_class->set_uid = gtd_task_list_eds_set_uid;

  object_class->finalize = gtd_task_list_eds_finalize;
  object_class->get_property = gtd_task_list_eds_get_property;
  object_class->set_property = gtd_task_list_eds_set_property;


  /**
   * GtdTaskListEds::client:
   *
   * The #ECalClient of this #GtdTaskListEds
   */
  g_object_class_install_property (object_class,
                                   PROP_CLIENT,
                                   g_param_spec_object ("client",
                                                        "ECalClient of this list",
                                                        "The ECalClient of this list",
                                                        E_TYPE_CAL_CLIENT,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  /**
   * GtdTaskListEds::source:
   *
   * The #ESource of this #GtdTaskListEds
   */
  g_object_class_install_property (object_class,
                                   PROP_SOURCE,
                                   g_param_spec_object ("source",
                                                        "ESource of this list",
                                                        "The ESource of this list",
                                                        E_TYPE_SOURCE,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
}

static void
gtd_task_list_eds_init (GtdTaskListEds *self)
{
}

void
gtd_task_list_eds_new (GtdProvider         *provider,
                       ESource             *source,
                       ECalClient          *client,
                       GAsyncReadyCallback  callback,
                       GCancellable        *cancellable,
                       gpointer             user_data)
{
  g_autoptr (GTask) task = NULL;
  NewTaskListData *data;

  data = g_new (NewTaskListData, 1);
  data->provider = g_object_ref (provider);
  data->source = g_object_ref (source);
  data->client = g_object_ref (client);

  task = g_task_new (NULL, cancellable, callback, user_data);
  g_task_set_task_data (task, data, new_task_list_data_free);
  g_task_set_source_tag (task, gtd_task_list_eds_new);

  g_idle_add (new_task_list_in_idle_cb, g_steal_pointer (&task));
}

GtdTaskListEds*
gtd_task_list_eds_new_finish (GAsyncResult  *result,
                              GError       **error)
{
  g_return_val_if_fail (G_IS_TASK (result), NULL);
  g_return_val_if_fail (!error || !*error, NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

ESource*
gtd_task_list_eds_get_source (GtdTaskListEds *list)
{
  g_return_val_if_fail (GTD_IS_TASK_LIST_EDS (list), NULL);

  return list->source;
}

void
gtd_task_list_eds_set_source (GtdTaskListEds *self,
                              ESource        *source)
{
  ESourceSelectable *selectable;
  ESourceRefresh *refresh;
  GdkRGBA color;
  gboolean is_inbox;

  g_return_if_fail (GTD_IS_TASK_LIST_EDS (self));

  if (!g_set_object (&self->source, source))
    return;

  is_inbox = g_str_equal (e_source_get_uid (source), GTD_PROVIDER_EDS_INBOX_ID);

  /* Setup color */
  selectable = E_SOURCE_SELECTABLE (e_source_get_extension (source, E_SOURCE_EXTENSION_TASK_LIST));

  if (!gdk_rgba_parse (&color, e_source_selectable_get_color (selectable)))
    gdk_rgba_parse (&color, "#ffffff"); /* calendar default color */

  gtd_task_list_set_color (GTD_TASK_LIST (self), &color);

  g_object_bind_property_full (self,
                               "color",
                               selectable,
                               "color",
                               G_BINDING_BIDIRECTIONAL,
                               color_to_string,
                               string_to_color,
                               self,
                               NULL);

  g_signal_connect_object (selectable,
                           "notify::selected",
                           G_CALLBACK (on_source_selectable_selected_changed_cb),
                           self,
                           0);

  /* Setup tasklist name */
  if (is_inbox)
    gtd_task_list_set_name (GTD_TASK_LIST (self), _("Inbox"));
  else
    gtd_task_list_set_name (GTD_TASK_LIST (self), e_source_get_display_name (source));

  g_object_bind_property (source,
                          "display-name",
                          self,
                          "name",
                          G_BINDING_BIDIRECTIONAL);

  /* Save the task list every time something changes */
  g_signal_connect_swapped (source,
                            "notify",
                            G_CALLBACK (save_task_list),
                            self);

  /* Update ::is-removable property */
  gtd_task_list_set_is_removable (GTD_TASK_LIST (self),
                                  e_source_get_removable (source) ||
                                  e_source_get_remote_deletable (source));

  g_signal_connect_swapped (source,
                            "notify::removable",
                            G_CALLBACK (on_source_removable_changed_cb),
                            self);

  g_signal_connect_swapped (source,
                            "notify::remote-deletable",
                            G_CALLBACK (on_source_removable_changed_cb),
                            self);

  /* Refresh timeout */
  refresh = e_source_get_extension (source, E_SOURCE_EXTENSION_REFRESH);
  e_source_refresh_set_enabled (refresh, TRUE);
  e_source_refresh_set_interval_minutes (refresh, 5);

  e_source_write (source, NULL, NULL, NULL);

  g_object_notify (G_OBJECT (self), "source");
}

ECalClient*
gtd_task_list_eds_get_client (GtdTaskListEds *self)
{
  g_return_val_if_fail (GTD_IS_TASK_LIST_EDS (self), NULL);

  return self->client;
}
