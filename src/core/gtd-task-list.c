/* gtd-task-list.c
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

#define G_LOG_DOMAIN "GtdTaskList"

#include "gtd-debug.h"
#include "gtd-provider.h"
#include "gtd-task.h"
#include "gtd-task-list.h"

#include <glib/gi18n.h>

/**
 * SECTION:gtd-task-list
 * @short_description:a list of tasks
 * @title:GtdTaskList
 * @stability:Unstable
 * @see_also:#GtdTask
 *
 * A #GtdTaskList represents a task list, and contains a list of tasks, a
 * color, a name and the provider who generated it.
 *
 * Only a #GtdProvider can create a #GtdTaskList. Equally, a #GtdTaskList
 * is only valid when associated with a #GtdProvider.
 *
 * It implements #GListModel, and can be used as the model for #GtkListBox.
 */

typedef struct
{
  GtdProvider         *provider;
  GdkRGBA             *color;

  GHashTable          *task_to_uid;
  GHashTable          *tasks;
  GSequence           *sorted_tasks;
  guint                n_tasks;

  guint                freeze_counter;

  gchar               *name;
  gboolean             removable;
  gboolean             archived;
} GtdTaskListPrivate;


static gint          compare_tasks_cb                            (gconstpointer      a,
                                                                  gconstpointer      b,
                                                                  gpointer           user_data);

static void          task_changed_cb                             (GtdTask            *task,
                                                                  GParamSpec         *pspec,
                                                                  GtdTaskList        *self);

static void          g_list_model_iface_init                     (GListModelInterface *iface);


G_DEFINE_TYPE_WITH_CODE (GtdTaskList, gtd_task_list, GTD_TYPE_OBJECT,
                         G_ADD_PRIVATE (GtdTaskList)
                         G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, g_list_model_iface_init))

enum
{
  TASK_ADDED,
  TASK_REMOVED,
  TASK_UPDATED,
  NUM_SIGNALS
};

enum
{
  PROP_0,
  PROP_ARCHIVED,
  PROP_COLOR,
  PROP_IS_REMOVABLE,
  PROP_NAME,
  PROP_PROVIDER,
  N_PROPS
};

static guint signals[NUM_SIGNALS] = { 0, };
static GParamSpec *properties[N_PROPS] = { NULL, };


/*
 * Auxiliary functions
 */

static void
update_task_uid (GtdTaskList *self,
                 GtdTask     *task)
{
  GtdTaskListPrivate *priv;
  GSequenceIter *iter;
  const gchar *old_uid;
  gchar *new_uid;

  priv = gtd_task_list_get_instance_private (self);

  g_debug ("Updating uid of task '%s'", gtd_task_get_title (task));

  new_uid = g_strdup (gtd_object_get_uid (GTD_OBJECT (task)));

  old_uid = g_hash_table_lookup (priv->task_to_uid, task);
  iter = g_hash_table_lookup (priv->tasks, old_uid);

  g_assert (g_sequence_get (iter) == task);

  g_hash_table_remove (priv->tasks, old_uid);

  g_hash_table_insert (priv->task_to_uid, task, new_uid);
  g_hash_table_insert (priv->tasks, new_uid, iter);
}

static guint
add_task (GtdTaskList *self,
          GtdTask     *task)
{
  GtdTaskListPrivate *priv;
  GSequenceIter *iter;
  gchar *uid;

  priv = gtd_task_list_get_instance_private (self);

  uid = g_strdup (gtd_object_get_uid (GTD_OBJECT (task)));
  iter = g_sequence_insert_sorted (priv->sorted_tasks,
                                   g_object_ref (task),
                                   compare_tasks_cb,
                                   NULL);

  g_hash_table_insert (priv->task_to_uid, task, uid);
  g_hash_table_insert (priv->tasks, uid, iter);

  g_signal_connect (task, "notify", G_CALLBACK (task_changed_cb), self);

  priv->n_tasks++;

  g_signal_emit (self, signals[TASK_ADDED], 0, task);

  return g_sequence_iter_get_position (iter);
}

static guint
remove_task (GtdTaskList *self,
             GtdTask     *task)
{
  GtdTaskListPrivate *priv;
  GSequenceIter *iter;
  const gchar *uid;
  guint position;

  priv = gtd_task_list_get_instance_private (self);

  g_signal_handlers_disconnect_by_func (task, task_changed_cb, self);

  uid = gtd_object_get_uid (GTD_OBJECT (task));
  iter = g_hash_table_lookup (priv->tasks, uid);
  position = g_sequence_iter_get_position (iter);

  g_hash_table_remove (priv->task_to_uid, task);
  g_hash_table_remove (priv->tasks, uid);

  g_sequence_remove (iter);

  priv->n_tasks--;

  g_signal_emit (self, signals[TASK_REMOVED], 0, task);

  return position;
}


/*
 * Callbacks
 */

static void
on_task_updated_cb (GObject      *object,
                    GAsyncResult *result,
                    gpointer      user_data)
{
  g_autoptr (GError) error = NULL;

  gtd_provider_update_task_finish (GTD_PROVIDER (object), result, &error);

  if (error)
    {
      g_warning ("Error updating task: %s", error->message);
      return;
    }
}

static gint
compare_tasks_cb (gconstpointer a,
                  gconstpointer b,
                  gpointer      user_data)
{
  return gtd_task_compare ((GtdTask*) a, (GtdTask*) b);
}

static void
task_changed_cb (GtdTask     *task,
                 GParamSpec  *pspec,
                 GtdTaskList *self)
{
  GtdTaskListPrivate *priv;
  GSequenceIter *iter;
  guint old_position;
  guint new_position;

  GTD_ENTRY;

  priv = gtd_task_list_get_instance_private (self);

  if (g_strcmp0 (g_param_spec_get_name (pspec), "loading") == 0)
    GTD_RETURN ();

  if (g_strcmp0 (g_param_spec_get_name (pspec), "uid") == 0)
    {
      update_task_uid (self, task);
      GTD_RETURN ();
    }

  /* Don't update when the list is frozen */
  if (priv->freeze_counter > 0)
    GTD_RETURN ();

  iter = g_hash_table_lookup (priv->tasks, gtd_object_get_uid (GTD_OBJECT (task)));

  old_position = g_sequence_iter_get_position (iter);
  g_sequence_sort_changed (iter, compare_tasks_cb, NULL);
  new_position = g_sequence_iter_get_position (iter);

  if (old_position != new_position)
    {
      GTD_TRACE_MSG ("Old position: %u, New position: %u", old_position, new_position);

      g_list_model_items_changed (G_LIST_MODEL (self), old_position, 1, 0);
      g_list_model_items_changed (G_LIST_MODEL (self), new_position, 0, 1);
    }

  GTD_EXIT;
}


/*
 * GListModel iface
 */

static GType
gtd_list_model_get_type (GListModel *model)
{
  return GTD_TYPE_TASK;
}

static guint
gtd_list_model_get_n_items (GListModel *model)
{
  GtdTaskListPrivate *priv = gtd_task_list_get_instance_private (GTD_TASK_LIST (model));
  return priv->n_tasks;
}

static gpointer
gtd_list_model_get_item (GListModel *model,
                         guint       i)
{
  GtdTaskListPrivate *priv;
  GSequenceIter *iter;
  GtdTask *task;

  priv = gtd_task_list_get_instance_private (GTD_TASK_LIST (model));
  iter = g_sequence_get_iter_at_pos (priv->sorted_tasks, i);
  task = g_sequence_get (iter);

  return g_object_ref (task);
}

static void
g_list_model_iface_init (GListModelInterface *iface)
{
  iface->get_item_type = gtd_list_model_get_type;
  iface->get_n_items = gtd_list_model_get_n_items;
  iface->get_item = gtd_list_model_get_item;
}


/*
 * GtdTaskList overrides
 */

static gboolean
gtd_task_list_real_get_archived (GtdTaskList *self)
{
  GtdTaskListPrivate *priv = gtd_task_list_get_instance_private (self);

  return priv->archived;
}

static void
gtd_task_list_real_set_archived (GtdTaskList *self,
                                 gboolean     archived)
{
  GtdTaskListPrivate *priv = gtd_task_list_get_instance_private (self);

  priv->archived = archived;
}


/*
 * GObject overrides
 */

static void
gtd_task_list_finalize (GObject *object)
{
  GtdTaskList *self = (GtdTaskList*) object;
  GtdTaskListPrivate *priv = gtd_task_list_get_instance_private (self);

  g_clear_object (&priv->provider);

  g_clear_pointer (&priv->color, gdk_rgba_free);
  g_clear_pointer (&priv->name, g_free);
  g_clear_pointer (&priv->sorted_tasks, g_sequence_free);
  g_clear_pointer (&priv->tasks, g_hash_table_destroy);
  g_clear_pointer (&priv->task_to_uid, g_hash_table_destroy);

  G_OBJECT_CLASS (gtd_task_list_parent_class)->finalize (object);
}

static void
gtd_task_list_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  GtdTaskList *self = GTD_TASK_LIST (object);
  GtdTaskListPrivate *priv = gtd_task_list_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_ARCHIVED:
      g_value_set_boolean (value, gtd_task_list_get_archived (self));
      break;

    case PROP_COLOR:
      {
        GdkRGBA *color = gtd_task_list_get_color (self);
        g_value_set_boxed (value, color);
        gdk_rgba_free (color);
        break;
      }

    case PROP_IS_REMOVABLE:
      g_value_set_boolean (value, gtd_task_list_is_removable (self));
      break;

    case PROP_NAME:
      g_value_set_string (value, priv->name);
      break;

    case PROP_PROVIDER:
      g_value_set_object (value, priv->provider);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_task_list_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  GtdTaskList *self = GTD_TASK_LIST (object);

  switch (prop_id)
    {
    case PROP_ARCHIVED:
      gtd_task_list_set_archived (self, g_value_get_boolean (value));
      break;

    case PROP_COLOR:
      gtd_task_list_set_color (self, g_value_get_boxed (value));
      break;

    case PROP_IS_REMOVABLE:
      gtd_task_list_set_is_removable (self, g_value_get_boolean (value));
      break;

    case PROP_NAME:
      gtd_task_list_set_name (self, g_value_get_string (value));
      break;

    case PROP_PROVIDER:
      gtd_task_list_set_provider (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_task_list_class_init (GtdTaskListClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gtd_task_list_finalize;
  object_class->get_property = gtd_task_list_get_property;
  object_class->set_property = gtd_task_list_set_property;

  klass->get_archived = gtd_task_list_real_get_archived;
  klass->set_archived = gtd_task_list_real_set_archived;

  /**
   * GtdTaskList::archived:
   *
   * Whether the task list is archived or not.
   */
  properties[PROP_ARCHIVED] = g_param_spec_boolean ("archived",
                                                    "Whether the list is archived",
                                                    "Whether the list is archived or not",
                                                    FALSE,
                                                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
  /**
   * GtdTaskList::color:
   *
   * The color of the list.
   */
  properties[PROP_COLOR] = g_param_spec_boxed ("color",
                                               "Color of the list",
                                               "The color of the list",
                                               GDK_TYPE_RGBA,
                                               G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * GtdTaskList::is-removable:
   *
   * Whether the task list can be removed from the system.
   */
  properties[PROP_IS_REMOVABLE] = g_param_spec_boolean ("is-removable",
                                                        "Whether the task list is removable",
                                                        "Whether the task list can be removed from the system",
                                                        FALSE,
                                                        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * GtdTaskList::name:
   *
   * The display name of the list.
   */
  properties[PROP_NAME] = g_param_spec_string ("name",
                                               "Name of the list",
                                               "The name of the list",
                                               NULL,
                                               G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * GtdTaskList::provider:
   *
   * The data provider of the list.
   */
  properties[PROP_PROVIDER] =  g_param_spec_object ("provider",
                                                    "Provider of the list",
                                                    "The provider that handles the list",
                                                    GTD_TYPE_PROVIDER,
                                                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, N_PROPS, properties);

  /**
   * GtdTaskList::task-added:
   * @list: a #GtdTaskList
   * @task: a #GtdTask
   *
   * The ::task-added signal is emmited after a #GtdTask
   * is added to the list.
   */
  signals[TASK_ADDED] = g_signal_new ("task-added",
                                      GTD_TYPE_TASK_LIST,
                                      G_SIGNAL_RUN_LAST,
                                      G_STRUCT_OFFSET (GtdTaskListClass, task_added),
                                      NULL,
                                      NULL,
                                      NULL,
                                      G_TYPE_NONE,
                                      1,
                                      GTD_TYPE_TASK);

  /**
   * GtdTaskList::task-removed:
   * @list: a #GtdTaskList
   * @task: a #GtdTask
   *
   * The ::task-removed signal is emmited after a #GtdTask
   * is removed from the list.
   */
  signals[TASK_REMOVED] = g_signal_new ("task-removed",
                                        GTD_TYPE_TASK_LIST,
                                        G_SIGNAL_RUN_LAST,
                                        G_STRUCT_OFFSET (GtdTaskListClass, task_removed),
                                        NULL,
                                        NULL,
                                        NULL,
                                        G_TYPE_NONE,
                                        1,
                                        GTD_TYPE_TASK);

  /**
   * GtdTaskList::task-updated:
   * @list: a #GtdTaskList
   * @task: a #GtdTask
   *
   * The ::task-updated signal is emmited after a #GtdTask
   * in the list is updated.
   */
  signals[TASK_UPDATED] = g_signal_new ("task-updated",
                                      GTD_TYPE_TASK_LIST,
                                      G_SIGNAL_RUN_LAST,
                                      G_STRUCT_OFFSET (GtdTaskListClass, task_updated),
                                      NULL,
                                      NULL,
                                      NULL,
                                      G_TYPE_NONE,
                                      1,
                                      GTD_TYPE_TASK);
}

static void
gtd_task_list_init (GtdTaskList *self)
{
  GtdTaskListPrivate *priv = gtd_task_list_get_instance_private (self);

  priv->task_to_uid = g_hash_table_new (g_str_hash, g_str_equal);
  priv->tasks = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  priv->sorted_tasks = g_sequence_new (g_object_unref);
}

/**
 * gtd_task_list_new:
 * @provider: (nullable): a #GtdProvider
 *
 * Creates a new list.
 *
 * Returns: (transfer full): the new #GtdTaskList
 */
GtdTaskList *
gtd_task_list_new (GtdProvider *provider)
{
  return g_object_new (GTD_TYPE_TASK_LIST,
                       "provider", provider,
                       NULL);
}

/**
 * gtd_task_list_get_color:
 * @list: a #GtdTaskList
 *
 * Retrieves the color of %list. It is guarantee that it always returns a
 * color, given a valid #GtdTaskList.
 *
 * Returns: (transfer full): the color of %list. Free with %gdk_rgba_free after use.
 */
GdkRGBA*
gtd_task_list_get_color (GtdTaskList *list)
{
  GtdTaskListPrivate *priv;
  GdkRGBA rgba;

  g_return_val_if_fail (GTD_IS_TASK_LIST (list), NULL);

  priv = gtd_task_list_get_instance_private (list);

  if (!priv->color)
    {
      gdk_rgba_parse (&rgba, "#ffffff");
      priv->color = gdk_rgba_copy (&rgba);
    }

  return gdk_rgba_copy (priv->color);
}

/**
 * gtd_task_list_set_color:
 * @list: a #GtdTaskList
 * #color: a #GdkRGBA
 *
 * sets the color of @list.
 */
void
gtd_task_list_set_color (GtdTaskList   *list,
                         const GdkRGBA *color)
{
  GtdTaskListPrivate *priv;
  GdkRGBA *current_color;

  g_return_if_fail (GTD_IS_TASK_LIST (list));

  priv = gtd_task_list_get_instance_private (list);
  current_color = gtd_task_list_get_color (list);

  if (!gdk_rgba_equal (current_color, color))
    {
      g_clear_pointer (&priv->color, gdk_rgba_free);
      priv->color = gdk_rgba_copy (color);

      g_object_notify (G_OBJECT (list), "color");
    }

  gdk_rgba_free (current_color);
}

/**
 * gtd_task_list_get_name:
 * @list: a #GtdTaskList
 *
 * Retrieves the user-visible name of @list, or %NULL.
 *
 * Returns: (transfer none): the internal name of @list. Do not free
 * after use.
 */
const gchar*
gtd_task_list_get_name (GtdTaskList *list)
{
  GtdTaskListPrivate *priv;

  g_return_val_if_fail (GTD_IS_TASK_LIST (list), NULL);

  priv = gtd_task_list_get_instance_private (list);

  return priv->name;
}

/**
 * gtd_task_list_set_name:
 * @list: a #GtdTaskList
 * @name: (nullable): the name of @list
 *
 * Sets the @list name to @name.
 */
void
gtd_task_list_set_name (GtdTaskList *list,
                        const gchar *name)
{
  GtdTaskListPrivate *priv;

  g_assert (GTD_IS_TASK_LIST (list));

  priv = gtd_task_list_get_instance_private (list);

  if (g_strcmp0 (priv->name, name) != 0)
    {
      g_free (priv->name);
      priv->name = g_strdup (name);

      g_object_notify (G_OBJECT (list), "name");
    }
}

/**
 * gtd_task_list_get_provider:
 * @list: a #GtdTaskList
 *
 * Retrieves the #GtdProvider who owns this list.
 *
 * Returns: (transfer none): a #GtdProvider
 */
GtdProvider*
gtd_task_list_get_provider (GtdTaskList *list)
{
  GtdTaskListPrivate *priv;

  g_return_val_if_fail (GTD_IS_TASK_LIST (list), NULL);

  priv = gtd_task_list_get_instance_private (list);

  return priv->provider;
}

/**
 * gtd_task_list_set_provider:
 * @self: a #GtdTaskList
 * @provider: (nullable): a #GtdProvider, or %NULL
 *
 * Sets the provider of this tasklist.
 */
void
gtd_task_list_set_provider (GtdTaskList *self,
                            GtdProvider *provider)
{
  GtdTaskListPrivate *priv;

  g_assert (GTD_IS_TASK_LIST (self));

  priv = gtd_task_list_get_instance_private (self);

  if (g_set_object (&priv->provider, provider))
    g_object_notify (G_OBJECT (self), "provider");
}

/**
 * gtd_task_list_add_task:
 * @list: a #GtdTaskList
 * @task: a #GtdTask
 *
 * Adds @task to @list.
 */
void
gtd_task_list_add_task (GtdTaskList *self,
                        GtdTask     *task)
{
  guint position;

  g_assert (GTD_IS_TASK_LIST (self));
  g_assert (GTD_IS_TASK (task));
  g_assert (!gtd_task_list_contains (self, task));

  position = add_task (self, task);
  g_list_model_items_changed (G_LIST_MODEL (self), position, 0, 1);
}

/**
 * gtd_task_list_update_task:
 * @list: a #GtdTaskList
 * @task: a #GtdTask
 *
 * Updates @task at @list.
 */
void
gtd_task_list_update_task (GtdTaskList *self,
                           GtdTask     *task)
{
  GtdTaskListPrivate *priv;
  GSequenceIter *iter;

  g_return_if_fail (GTD_IS_TASK_LIST (self));
  g_return_if_fail (GTD_IS_TASK (task));

  priv = gtd_task_list_get_instance_private (self);

  g_return_if_fail (gtd_task_list_contains (self, task));

  iter = g_hash_table_lookup (priv->tasks, gtd_object_get_uid (GTD_OBJECT (task)));

  g_list_model_items_changed (G_LIST_MODEL (self),
                              g_sequence_iter_get_position (iter),
                              1,
                              1);

  g_signal_emit (self, signals[TASK_UPDATED], 0, task);
}

/**
 * gtd_task_list_remove_task:
 * @list: a #GtdTaskList
 * @task: a #GtdTask
 *
 * Removes @task from @list if it's inside the list.
 */
void
gtd_task_list_remove_task (GtdTaskList *list,
                           GtdTask     *task)
{
  guint position;

  g_assert (GTD_IS_TASK_LIST (list));
  g_assert (GTD_IS_TASK (task));
  g_assert (gtd_task_list_contains (list, task));

  position = remove_task (list, task);
  g_list_model_items_changed (G_LIST_MODEL (list), position, 1, 0);
}

/**
 * gtd_task_list_contains:
 * @list: a #GtdTaskList
 * @task: a #GtdTask
 *
 * Checks if @task is inside @list.
 *
 * Returns: %TRUE if @list contains @task, %FALSE otherwise
 */
gboolean
gtd_task_list_contains (GtdTaskList *list,
                        GtdTask     *task)
{
  GtdTaskListPrivate *priv;

  g_assert (GTD_IS_TASK_LIST (list));
  g_assert (GTD_IS_TASK (task));

  priv = gtd_task_list_get_instance_private (list);

  return g_hash_table_contains (priv->tasks, gtd_object_get_uid (GTD_OBJECT (task)));
}

/**
 * gtd_task_list_get_is_removable:
 * @list: a #GtdTaskList
 *
 * Retrieves whether @list can be removed or not.
 *
 * Returns: %TRUE if the @list can be removed, %FALSE otherwise
 */
gboolean
gtd_task_list_is_removable (GtdTaskList *list)
{
  GtdTaskListPrivate *priv;

  g_return_val_if_fail (GTD_IS_TASK_LIST (list), FALSE);

  priv = gtd_task_list_get_instance_private (list);

  return priv->removable;
}

/**
 * gtd_task_list_set_is_removable:
 * @list: a #GtdTaskList
 * @is_removable: %TRUE if @list can be deleted, %FALSE otherwise
 *
 * Sets whether @list can be deleted or not.
 */
void
gtd_task_list_set_is_removable (GtdTaskList *list,
                                gboolean     is_removable)
{
  GtdTaskListPrivate *priv;

  g_return_if_fail (GTD_IS_TASK_LIST (list));

  priv = gtd_task_list_get_instance_private (list);

  if (priv->removable != is_removable)
    {
      priv->removable = is_removable;

      g_object_notify (G_OBJECT (list), "is-removable");
    }
}

/**
 * gtd_task_list_get_task_by_id:
 * @list: a #GtdTaskList
 * @id: the id of the task
 *
 * Retrieves a task from @self with the given @id.
 *
 * Returns: (transfer none)(nullable): a #GtdTask, or %NULL
 */
GtdTask*
gtd_task_list_get_task_by_id (GtdTaskList *self,
                              const gchar *id)
{
  GtdTaskListPrivate *priv;
  GSequenceIter *iter;

  g_return_val_if_fail (GTD_IS_TASK_LIST (self), NULL);

  priv = gtd_task_list_get_instance_private (self);
  iter = g_hash_table_lookup (priv->tasks, id);

  if (!iter)
    return NULL;

  return g_sequence_get (iter);
}

/**
 * gtd_task_list_move_task_to_position:
 * @self: a #GtdTaskList
 * @task: a #GtdTask
 * @new_position: the new position of @task inside @self
 *
 * Moves @task to @new_position, and repositions the elements
 * in between as well.
 *
 * @task must belog to @self.
 */
void
gtd_task_list_move_task_to_position (GtdTaskList *self,
                                     GtdTask     *task,
                                     guint        new_position)
{

  GtdTaskListPrivate *priv = gtd_task_list_get_instance_private (self);
  GSequenceIter *new_position_iter;
  GSequenceIter *iter;
  guint old_position;
  guint length;
  guint start;
  guint i;


  g_return_if_fail (GTD_IS_TASK_LIST (self));
  g_return_if_fail (GTD_IS_TASK (task));
  g_return_if_fail (gtd_task_list_contains (self, task));
  g_return_if_fail (g_list_model_get_n_items (G_LIST_MODEL (self)) >= new_position);

  iter = g_hash_table_lookup (priv->tasks, gtd_object_get_uid (GTD_OBJECT (task)));
  old_position = g_sequence_iter_get_position (iter);

  if (old_position == new_position)
    return;

  GTD_TRACE_MSG ("Moving task '%s' (%s) from %u to %u",
                 gtd_task_get_title (task),
                 gtd_object_get_uid (GTD_OBJECT (task)),
                 old_position,
                 new_position);

  /* Update the GSequence */
  new_position_iter = new_position < old_position ?
                      g_sequence_get_iter_at_pos (priv->sorted_tasks, new_position) :
                      g_sequence_get_iter_at_pos (priv->sorted_tasks, new_position + 1);
  g_sequence_move (iter, new_position_iter);

  /* Update the 'position' property of all tasks in between */
  priv->freeze_counter++;

  length = ABS ((gint) new_position - (gint64) old_position) + 1;
  start = MIN (old_position, new_position);
  iter = g_sequence_get_iter_at_pos (priv->sorted_tasks, start);

  for (i = 0; i < length; i++)
    {
      GtdTask *aux = g_sequence_get (iter);

      g_signal_handlers_block_by_func (aux, task_changed_cb, self);
      gtd_task_set_position (aux, start + i);
      g_signal_handlers_unblock_by_func (aux, task_changed_cb, self);

      gtd_provider_update_task (priv->provider, aux, NULL, on_task_updated_cb, self);

      iter = g_sequence_iter_next (iter);
    }

  g_list_model_items_changed (G_LIST_MODEL (self), old_position, 1, 0);
  g_list_model_items_changed (G_LIST_MODEL (self), new_position, 0, 1);

  priv->freeze_counter--;
}

/**
 * gtd_task_list_get_archived:
 * @self: a #GtdTaskList
 *
 * Retrieves whether @self is archived or not. Archived task lists
 * are hidden by default, and new tasks cannot be added.
 *
 * Returns: %TRUE if @self is archived, %FALSE otherwise.
 */
gboolean
gtd_task_list_get_archived (GtdTaskList *self)
{
  g_return_val_if_fail (GTD_IS_TASK_LIST (self), FALSE);

  return GTD_TASK_LIST_GET_CLASS (self)->get_archived (self);
}

/**
 * gtd_task_list_set_archived:
 * @self: a #GtdTaskList
 * @archived: whether @self is archived or not
 *
 * Sets the "archive" property of @self to @archived.
 */
void
gtd_task_list_set_archived (GtdTaskList *self,
                            gboolean     archived)
{
  gboolean was_archived;

  g_return_if_fail (GTD_IS_TASK_LIST (self));

  was_archived = gtd_task_list_get_archived (self);

  if (archived == was_archived)
    return;

  GTD_TASK_LIST_GET_CLASS (self)->set_archived (self, archived);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ARCHIVED]);
}

/**
 * gtd_task_list_is_inbox:
 * @self: a #GtdTaskList
 *
 * Retrieves whether @self is the inbox task list of its provider.
 *
 * Returns: %TRUE if @self is the inbox of it's provider, %FALSE otherwise.
 */
gboolean
gtd_task_list_is_inbox (GtdTaskList *self)
{
  GtdTaskListPrivate *priv;

  g_return_val_if_fail (GTD_IS_TASK_LIST (self), FALSE);

  priv = gtd_task_list_get_instance_private (self);

  return self == gtd_provider_get_inbox (priv->provider);
}
