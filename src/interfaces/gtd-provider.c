/* gtd-provider.c
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

#define G_LOG_DOMAIN "GtdProvider"

#include "gtd-provider.h"
#include "gtd-task.h"
#include "gtd-task-list.h"
#include "gtd-utils.h"

/**
 * SECTION:gtd-provider
 * @short_description:data sources for GNOME To do
 * @title: GtdProvider
 * @stability:Unstable
 *
 * The #GtdProvider is the interface that GNOME To Do uses to
 * connect to data sources. It must provide ways to create, update
 * and remove tasks and tasklists.
 *
 * A provider implementation must also expose which is the default
 * tasklist among the tasklists it manages.
 */

G_DEFINE_INTERFACE (GtdProvider, gtd_provider, GTD_TYPE_OBJECT)

enum
{
  LIST_ADDED,
  LIST_CHANGED,
  LIST_REMOVED,
  NUM_SIGNALS
};

static guint signals[NUM_SIGNALS] = { 0, };


static void
gtd_provider_default_init (GtdProviderInterface *iface)
{
  /**
   * GtdProvider::enabled:
   *
   * Whether the #GtdProvider is enabled.
   */
  g_object_interface_install_property (iface,
                                       g_param_spec_boolean ("enabled",
                                                             "Identifier of the provider",
                                                             "The identifier of the provider",
                                                             FALSE,
                                                             G_PARAM_READABLE));

  /**
   * GtdProvider::icon:
   *
   * The icon of the #GtdProvider, e.g. the account icon
   * of a GNOME Online Accounts' account.
   */
  g_object_interface_install_property (iface,
                                       g_param_spec_object ("icon",
                                                            "Icon of the provider",
                                                            "The icon of the provider",
                                                            G_TYPE_ICON,
                                                            G_PARAM_READABLE));

  /**
   * GtdProvider::id:
   *
   * The unique identifier of the #GtdProvider.
   */
  g_object_interface_install_property (iface,
                                       g_param_spec_string ("id",
                                                            "Identifier of the provider",
                                                            "The identifier of the provider",
                                                            NULL,
                                                            G_PARAM_READABLE));

  /**
   * GtdProvider::name:
   *
   * The user-visible name of the #GtdProvider.
   */
  g_object_interface_install_property (iface,
                                       g_param_spec_string ("name",
                                                            "Name of the provider",
                                                            "The user-visible name of the provider",
                                                            NULL,
                                                            G_PARAM_READABLE));

  /**
   * GtdProvider::provider-type:
   *
   * The type of the #GtdProvider.
   */
  g_object_interface_install_property (iface,
                                       g_param_spec_string ("provider-type",
                                                            "Type of the provider",
                                                            "The type of the provider",
                                                            NULL,
                                                            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * GtdProvider::description:
   *
   * The description of the #GtdProvider, e.g. the account user
   * of a GNOME Online Accounts' account.
   */
  g_object_interface_install_property (iface,
                                       g_param_spec_string ("description",
                                                            "Description of the provider",
                                                            "The description of the provider",
                                                            NULL,
                                                            G_PARAM_READABLE));

  /**
   * GtdProvider::default-task-list:
   *
   * The default #GtdTaskList of the provider.
   */
  g_object_interface_install_property (iface,
                                       g_param_spec_object ("default-task-list",
                                                            "Default tasklist of the provider",
                                                            "The default tasklist of the provider",
                                                            GTD_TYPE_TASK_LIST,
                                                            G_PARAM_READWRITE));

  /**
   * GtdProvider::list-added:
   * @provider: a #GtdProvider
   * @list: a #GtdTaskList
   *
   * The ::list-added signal is emmited after a #GtdTaskList
   * is connected.
   */
  signals[LIST_ADDED] = g_signal_new ("list-added",
                                      GTD_TYPE_PROVIDER,
                                      G_SIGNAL_RUN_LAST,
                                      0,
                                      NULL,
                                      NULL,
                                      NULL,
                                      G_TYPE_NONE,
                                      1,
                                      GTD_TYPE_TASK_LIST);

  /**
   * GtdProvider::list-changed:
   * @provider: a #GtdProvider
   * @list: a #GtdTaskList
   *
   * The ::list-changed signal is emmited after a #GtdTaskList
   * has any of it's properties changed.
   */
  signals[LIST_CHANGED] = g_signal_new ("list-changed",
                                        GTD_TYPE_PROVIDER,
                                        G_SIGNAL_RUN_LAST,
                                        0,
                                        NULL,
                                        NULL,
                                        NULL,
                                        G_TYPE_NONE,
                                        1,
                                        GTD_TYPE_TASK_LIST);

  /**
   * GtdProvider::list-removed:
   * @provider: a #GtdProvider
   * @list: a #GtdTaskList
   *
   * The ::list-removed signal is emmited after a #GtdTaskList
   * is disconnected.
   */
  signals[LIST_REMOVED] = g_signal_new ("list-removed",
                                        GTD_TYPE_PROVIDER,
                                        G_SIGNAL_RUN_LAST,
                                        0,
                                        NULL,
                                        NULL,
                                        NULL,
                                        G_TYPE_NONE,
                                        1,
                                        GTD_TYPE_TASK_LIST);
}

/**
 * gtd_provider_get_id:
 * @provider: a #GtdProvider
 *
 * Retrieves the identifier of @provider.
 *
 * Returns: (transfer none): the id of @provider
 */
const gchar*
gtd_provider_get_id (GtdProvider *provider)
{
  g_return_val_if_fail (GTD_IS_PROVIDER (provider), NULL);
  g_return_val_if_fail (GTD_PROVIDER_GET_IFACE (provider)->get_id, NULL);

  return GTD_PROVIDER_GET_IFACE (provider)->get_id (provider);
}

/**
 * gtd_provider_get_name:
 * @provider: a #GtdProvider
 *
 * Retrieves the user-visible name of @provider.
 *
 * Returns: (transfer none): the name of @provider
 */
const gchar*
gtd_provider_get_name (GtdProvider *provider)
{
  g_return_val_if_fail (GTD_IS_PROVIDER (provider), NULL);
  g_return_val_if_fail (GTD_PROVIDER_GET_IFACE (provider)->get_name, NULL);

  return GTD_PROVIDER_GET_IFACE (provider)->get_name (provider);
}

/**
 * gtd_provider_get_provider_type:
 * @provider: a #GtdProvider
 *
 * Retrieves the type of the @provider. This should return the
 * same value, regardless of the account name.
 *
 * For example: "todoist", "todo-txt" or "google"
 *
 * Returns: (transfer none): the type of the @provider
 */
const gchar*
gtd_provider_get_provider_type (GtdProvider *provider)
{
  g_return_val_if_fail (GTD_IS_PROVIDER (provider), NULL);
  g_return_val_if_fail (GTD_PROVIDER_GET_IFACE (provider)->get_name, NULL);

  return GTD_PROVIDER_GET_IFACE (provider)->get_provider_type (provider);
}

/**
 * gtd_provider_get_description:
 * @provider: a #GtdProvider
 *
 * Retrieves the description of @provider.
 *
 * Returns: (transfer none): the description of @provider
 */
const gchar*
gtd_provider_get_description (GtdProvider *provider)
{
  g_return_val_if_fail (GTD_IS_PROVIDER (provider), NULL);
  g_return_val_if_fail (GTD_PROVIDER_GET_IFACE (provider)->get_description, NULL);

  return GTD_PROVIDER_GET_IFACE (provider)->get_description (provider);
}

/**
 * gtd_provider_get_enabled:
 * @provider: a #GtdProvider
 *
 * Retrieves whether @provider is enabled or not. A disabled
 * provider cannot be selected to be default nor be selected
 * to add tasks to it.
 *
 * Returns: %TRUE if provider is enabled, %FALSE otherwise.
 */
gboolean
gtd_provider_get_enabled (GtdProvider *provider)
{
  g_return_val_if_fail (GTD_IS_PROVIDER (provider), FALSE);
  g_return_val_if_fail (GTD_PROVIDER_GET_IFACE (provider)->get_enabled, FALSE);

  return GTD_PROVIDER_GET_IFACE (provider)->get_enabled (provider);
}

/**
 * gtd_provider_get_icon:
 * @provider: a #GtdProvider
 *
 * The icon of @provider.
 *
 * Returns: (transfer none): a #GIcon
 */
GIcon*
gtd_provider_get_icon (GtdProvider *provider)
{
  g_return_val_if_fail (GTD_IS_PROVIDER (provider), NULL);
  g_return_val_if_fail (GTD_PROVIDER_GET_IFACE (provider)->get_icon, NULL);

  return GTD_PROVIDER_GET_IFACE (provider)->get_icon (provider);
}

/**
 * gtd_provider_create_task:
 * @provider: a #GtdProvider
 * @task: a #GtdTask
 *
 * Creates the given task in @provider.
 */
void
gtd_provider_create_task (GtdProvider *provider,
                          GtdTaskList *list,
                          const gchar *title,
                          GDateTime   *due_date)
{
  g_return_if_fail (GTD_IS_PROVIDER (provider));
  g_return_if_fail (GTD_PROVIDER_GET_IFACE (provider)->create_task);

  GTD_PROVIDER_GET_IFACE (provider)->create_task (provider, list, title, due_date);
}

/**
 * gtd_provider_update_task:
 * @provider: a #GtdProvider
 * @task: a #GtdTask
 *
 * Updates the given task in @provider.
 */
void
gtd_provider_update_task (GtdProvider *provider,
                          GtdTask     *task)
{
  g_return_if_fail (GTD_IS_PROVIDER (provider));
  g_return_if_fail (GTD_PROVIDER_GET_IFACE (provider)->update_task);

  GTD_PROVIDER_GET_IFACE (provider)->update_task (provider, task);
}

/**
 * gtd_provider_remove_task:
 * @provider: a #GtdProvider
 * @task: a #GtdTask
 *
 * Removes the given task from @provider.
 */
void
gtd_provider_remove_task (GtdProvider *provider,
                          GtdTask     *task)
{
  g_return_if_fail (GTD_IS_PROVIDER (provider));
  g_return_if_fail (GTD_PROVIDER_GET_IFACE (provider)->remove_task);

  GTD_PROVIDER_GET_IFACE (provider)->remove_task (provider, task);
}

/**
 * gtd_provider_create_task_list:
 * @provider: a #GtdProvider
 * @name: (nullable): the name of the new task list
 *
 * Creates the given list in @provider.
 */
void
gtd_provider_create_task_list (GtdProvider *provider,
                               const gchar *name)
{
  g_return_if_fail (GTD_IS_PROVIDER (provider));
  g_return_if_fail (GTD_PROVIDER_GET_IFACE (provider)->create_task_list);

  GTD_PROVIDER_GET_IFACE (provider)->create_task_list (provider, name);
}

/**
 * gtd_provider_update_task_list:
 * @provider: a #GtdProvider
 * @list: a #GtdTaskList
 *
 * Updates the given list in @provider.
 */
void
gtd_provider_update_task_list (GtdProvider *provider,
                               GtdTaskList *list)
{
  g_return_if_fail (GTD_IS_PROVIDER (provider));
  g_return_if_fail (GTD_PROVIDER_GET_IFACE (provider)->update_task_list);

  GTD_PROVIDER_GET_IFACE (provider)->update_task_list (provider, list);
}

/**
 * gtd_provider_remove_task_list:
 * @provider: a #GtdProvider
 * @list: a #GtdTaskList
 *
 * Removes the given list from @provider.
 */
void
gtd_provider_remove_task_list (GtdProvider *provider,
                               GtdTaskList *list)
{
  g_return_if_fail (GTD_IS_PROVIDER (provider));
  g_return_if_fail (GTD_PROVIDER_GET_IFACE (provider)->remove_task_list);

  GTD_PROVIDER_GET_IFACE (provider)->remove_task_list (provider, list);
}

/**
 * gtd_provider_get_task_lists:
 * @provider: a #GtdProvider
 *
 * Retrieves the tasklists that this provider contains.
 *
 * Returns: (transfer container) (element-type Gtd.TaskList): the list of tasks, or %NULL
 */
GList*
gtd_provider_get_task_lists (GtdProvider *provider)
{
  g_return_val_if_fail (GTD_IS_PROVIDER (provider), NULL);
  g_return_val_if_fail (GTD_PROVIDER_GET_IFACE (provider)->get_task_lists, NULL);

  return GTD_PROVIDER_GET_IFACE (provider)->get_task_lists (provider);
}

/**
 * gtd_provider_get_default_task_list:
 * @provider: a #GtdProvider
 *
 * Retrieves the default tasklist of @provider.
 *
 * Returns: (transfer none)(nullable): the default tasklist, or %NULL
 */
GtdTaskList*
gtd_provider_get_default_task_list (GtdProvider *provider)
{
  g_return_val_if_fail (GTD_IS_PROVIDER (provider), NULL);
  g_return_val_if_fail (GTD_PROVIDER_GET_IFACE (provider)->get_default_task_list, NULL);

  return GTD_PROVIDER_GET_IFACE (provider)->get_default_task_list (provider);
}

/**
 * gtd_provider_set_default_task_list:
 * @provider: a #GtdProvider
 * @list: a #GtdTaskList
 *
 * Sets the default tasklist of @provider.
 */
void
gtd_provider_set_default_task_list (GtdProvider *provider,
                                    GtdTaskList *list)
{
  g_return_if_fail (GTD_IS_PROVIDER (provider));
  g_return_if_fail (GTD_IS_TASK_LIST (provider));
  g_return_if_fail (gtd_task_list_get_provider (list) == provider);
  g_return_if_fail (GTD_PROVIDER_GET_IFACE (provider)->set_default_task_list);

  return GTD_PROVIDER_GET_IFACE (provider)->set_default_task_list (provider, list);
}

/**
 * gtd_provider_compare:
 * @a: a #GtdProvider
 * @b: a #GtdProvider
 *
 * Compares @a and @b. The sorting criteria is internal and
 * may change.
 *
 * Returns: -1 if @a comes before @b, 1 for the oposite, and
 * 0 if they're equal
 */
gint
gtd_provider_compare (GtdProvider *a,
                      GtdProvider *b)
{
  gint result;

  g_return_val_if_fail (GTD_IS_PROVIDER (a), 0);
  g_return_val_if_fail (GTD_IS_PROVIDER (b), 0);

  if (a == b)
    return 0;

  result = gtd_collate_compare_strings (gtd_provider_get_name (a), gtd_provider_get_name (b));

  if (result != 0)
    return result;

  return gtd_collate_compare_strings (gtd_provider_get_description (a), gtd_provider_get_description (b));
}
