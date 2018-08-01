/* gtd-task-list.c
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

#define G_LOG_DOMAIN "GtdTaskList"

#include "interfaces/gtd-provider.h"
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
 * A #GtdTaskList represents a task list, and contains a list of tasks, a color,
 * a name and the provider who generated it.
 *
 * It implements #GListModel, and can be used as the model for #GtkListBox.
 */

typedef struct
{
  GtdProvider         *provider;
  GdkRGBA             *color;

  GHashTable          *tasks;
  GSequence           *sorted_tasks;

  gchar               *name;
  gboolean             removable;
} GtdTaskListPrivate;


static gint          compare_tasks_cb                            (gconstpointer      a,
                                                                  gconstpointer      b,
                                                                  gpointer           user_data);

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
  GtdTaskListPrivate *priv = NULL;
  GHashTableIter iter;
  gpointer sequence_iter;
  gpointer uid;

  priv = gtd_task_list_get_instance_private (self);

  g_debug ("Updating uid of task '%s'", gtd_task_get_title (task));

  /* Iterate until we find the current task */
  g_hash_table_iter_init (&iter, priv->tasks);
  while (g_hash_table_iter_next (&iter, &uid, &sequence_iter))
    {
      if (g_sequence_get (sequence_iter) == task)
        break;
    }

  g_assert (g_sequence_get (sequence_iter) == task);

  g_hash_table_remove (priv->tasks, uid);
  g_hash_table_insert (priv->tasks, g_strdup (gtd_object_get_uid (GTD_OBJECT (task))), sequence_iter);
}


/*
 * Callbacks
 */

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
  if (g_strcmp0 (g_param_spec_get_name (pspec), "loading") == 0)
    return;

  if (g_strcmp0 (g_param_spec_get_name (pspec), "uid") == 0)
    {
      update_task_uid (self, task);
      return;
    }

  g_signal_emit (self, signals[TASK_UPDATED], 0, task);
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
  return g_hash_table_size (priv->tasks);
}

static gpointer
gtd_list_model_get_item (GListModel *model,
                         guint       i)
{
  GtdTaskListPrivate *priv = gtd_task_list_get_instance_private (GTD_TASK_LIST (model));
  return g_sequence_get (g_sequence_get_iter_at_pos (priv->sorted_tasks, i));
}

static void
g_list_model_iface_init (GListModelInterface *iface)
{
  iface->get_item_type = gtd_list_model_get_type;
  iface->get_n_items = gtd_list_model_get_n_items;
  iface->get_item = gtd_list_model_get_item;
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

  /**
   * GtdTaskList::color:
   *
   * The color of the list.
   */
  properties[PROP_COLOR] = g_param_spec_boxed ("color",
                                               "Color of the list",
                                               "The color of the list",
                                               GDK_TYPE_RGBA,
                                               G_PARAM_READWRITE);

  /**
   * GtdTaskList::is-removable:
   *
   * Whether the task list can be removed from the system.
   */
  properties[PROP_IS_REMOVABLE] = g_param_spec_boolean ("is-removable",
                                                        "Whether the task list is removable",
                                                        "Whether the task list can be removed from the system",
                                                        FALSE,
                                                        G_PARAM_READWRITE);

  /**
   * GtdTaskList::name:
   *
   * The display name of the list.
   */
  properties[PROP_NAME] = g_param_spec_string ("name",
                                               "Name of the list",
                                               "The name of the list",
                                               NULL,
                                               G_PARAM_READWRITE);

  /**
   * GtdTaskList::provider:
   *
   * The data provider of the list.
   */
  properties[PROP_PROVIDER] =  g_param_spec_object ("provider",
                                                    "Provider of the list",
                                                    "The provider that handles the list",
                                                    GTD_TYPE_PROVIDER,
                                                    G_PARAM_READWRITE);

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
 * gtd_task_list_get_tasks:
 * @list: a #GtdTaskList
 *
 * Returns the list's tasks.
 *
 * Returns: (element-type GtdTask) (transfer container): a newly-allocated list of the list's tasks.
 */
GList*
gtd_task_list_get_tasks (GtdTaskList *list)
{
  GtdTaskListPrivate *priv;
  GList *values = NULL;
  guint i;

  g_return_val_if_fail (GTD_IS_TASK_LIST (list), NULL);

  priv = gtd_task_list_get_instance_private (list);

  for (i = 0; i < g_hash_table_size (priv->tasks); i++)
    values = g_list_prepend (values, g_list_model_get_item (G_LIST_MODEL (list), i));

  return g_list_reverse (values);
}

/**
 * gtd_task_list_save_task:
 * @list: a #GtdTaskList
 * @task: a #GtdTask
 *
 * Adds or updates @task to @list if it's not already present.
 */
void
gtd_task_list_save_task (GtdTaskList *list,
                         GtdTask     *task)
{
  GtdTaskListPrivate *priv;

  g_assert (GTD_IS_TASK_LIST (list));
  g_assert (GTD_IS_TASK (task));

  priv = gtd_task_list_get_instance_private (list);

  if (gtd_task_list_contains (list, task))
    {
      g_signal_emit (list, signals[TASK_UPDATED], 0, task);
    }
  else
    {
      GSequenceIter *iter;
      const gchar *uid;

      uid = gtd_object_get_uid (GTD_OBJECT (task));
      iter = g_sequence_insert_sorted (priv->sorted_tasks,
                                       g_object_ref (task),
                                       compare_tasks_cb,
                                       NULL);

      g_hash_table_insert (priv->tasks, g_strdup (uid), iter);

      g_signal_connect (task, "notify", G_CALLBACK (task_changed_cb), list);

      g_signal_emit (list, signals[TASK_ADDED], 0, task);
    }
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
  GtdTaskListPrivate *priv;
  const gchar *uid;

  g_assert (GTD_IS_TASK_LIST (list));
  g_assert (GTD_IS_TASK (task));

  priv = gtd_task_list_get_instance_private (list);

  if (!gtd_task_list_contains (list, task))
    return;

  g_signal_handlers_disconnect_by_func (task, task_changed_cb, list);

  uid = gtd_object_get_uid (GTD_OBJECT (task));
  g_sequence_remove (g_hash_table_lookup (priv->tasks, uid));
  g_hash_table_remove (priv->tasks, uid);

  g_signal_emit (list, signals[TASK_REMOVED], 0, task);
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
