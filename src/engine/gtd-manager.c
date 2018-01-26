/* gtd-manager.c
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

#define G_LOG_DOMAIN "GtdManager"

#include "interfaces/gtd-provider.h"
#include "interfaces/gtd-panel.h"
#include "gtd-manager.h"
#include "gtd-manager-protected.h"
#include "gtd-plugin-manager.h"
#include "gtd-task.h"
#include "gtd-task-list.h"
#include "gtd-timer.h"

#include <glib/gi18n.h>

/**
 * SECTION:gtd-manager
 * @short_description:bridge between plugins and GNOME To Do
 * @title:GtdManager
 * @stability:Unstable
 * @see_also:#GtdNotification,#GtdActivatable
 *
 * The #GtdManager object is a singleton object that exposes all the data
 * inside the plugin to GNOME To Do, and vice-versa. From here, plugins have
 * access to all the tasklists, tasks and panels of the other plugins.
 *
 * Objects can use gtd_manager_emit_error_message() to send errors to GNOME
 * To Do. This will create a #GtdNotification internally.
 */

typedef struct
{
  GSettings             *settings;
  GtdPluginManager      *plugin_manager;

  GList                 *tasklists;
  GList                 *providers;
  GList                 *panels;
  GtdProvider           *default_provider;
  GtdTimer              *timer;
} GtdManagerPrivate;

struct _GtdManager
{
   GtdObject           parent;

  /*< private >*/
  GtdManagerPrivate *priv;
};

G_DEFINE_TYPE_WITH_PRIVATE (GtdManager, gtd_manager, GTD_TYPE_OBJECT)

/* Singleton instance */
GtdManager *gtd_manager_instance = NULL;

enum
{
  LIST_ADDED,
  LIST_CHANGED,
  LIST_REMOVED,
  SHOW_ERROR_MESSAGE,
  PANEL_ADDED,
  PANEL_REMOVED,
  PROVIDER_ADDED,
  PROVIDER_REMOVED,
  NUM_SIGNALS
};

enum
{
  PROP_0,
  PROP_DEFAULT_PROVIDER,
  PROP_DEFAULT_TASKLIST,
  PROP_TIMER,
  PROP_PLUGIN_MANAGER,
  LAST_PROP
};

static guint signals[NUM_SIGNALS] = { 0, };

static void
check_provider_is_default (GtdManager  *manager,
                           GtdProvider *provider)
{
  GtdManagerPrivate *priv;
  gchar *default_provider;

  priv = manager->priv;
  default_provider = g_settings_get_string (priv->settings, "default-provider");

  if (g_strcmp0 (default_provider, gtd_provider_get_id (provider)) == 0)
    gtd_manager_set_default_provider (manager, provider);

  g_free (default_provider);
}

static void
emit_show_error_message (GtdManager                *manager,
                         const gchar               *primary_text,
                         const gchar               *secondary_text,
                         GtdNotificationActionFunc  action,
                         gpointer                   user_data)
{
  g_signal_emit (manager,
                 signals[SHOW_ERROR_MESSAGE],
                 0,
                 primary_text,
                 secondary_text,
                 action,
                 user_data);
}

static void
gtd_manager_finalize (GObject *object)
{
  GtdManager *self = (GtdManager *)object;

  g_clear_object (&self->priv->plugin_manager);
  g_clear_object (&self->priv->settings);
  g_clear_object (&self->priv->timer);

  G_OBJECT_CLASS (gtd_manager_parent_class)->finalize (object);
}

static void
gtd_manager_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  GtdManagerPrivate *priv = gtd_manager_get_instance_private (GTD_MANAGER (object));

  switch (prop_id)
    {
    case PROP_DEFAULT_PROVIDER:
      g_value_set_object (value, priv->default_provider);
      break;

    case PROP_DEFAULT_TASKLIST:
      g_value_set_object (value, gtd_provider_get_default_task_list (priv->default_provider));
      break;

    case PROP_TIMER:
      g_value_set_object (value, priv->timer);
      break;

    case PROP_PLUGIN_MANAGER:
      g_value_set_object (value, priv->plugin_manager);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_manager_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  GtdManagerPrivate *priv;
  GtdManager *self;

  self = GTD_MANAGER (object);
  priv = gtd_manager_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_DEFAULT_PROVIDER:
      if (g_set_object (&priv->default_provider, g_value_get_object (value)))
        g_object_notify (object, "default-provider");
      break;

    case PROP_DEFAULT_TASKLIST:
      gtd_manager_set_default_task_list (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_manager_class_init (GtdManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gtd_manager_finalize;
  object_class->get_property = gtd_manager_get_property;
  object_class->set_property = gtd_manager_set_property;

  /**
   * GtdManager::default-provider:
   *
   * The default provider.
   */
  g_object_class_install_property (
        object_class,
        PROP_DEFAULT_PROVIDER,
        g_param_spec_object ("default-provider",
                             "The default provider of the application",
                             "The default provider of the application",
                             GTD_TYPE_PROVIDER,
                             G_PARAM_READWRITE));

  /**
   * GtdManager::default-task-list:
   *
   * The default provider.
   */
  g_object_class_install_property (
        object_class,
        PROP_DEFAULT_TASKLIST,
        g_param_spec_object ("default-task-list",
                             "The default task list of the application",
                             "The default task list of the application",
                             GTD_TYPE_TASK_LIST,
                             G_PARAM_READWRITE));

  /**
   * GtdManager::timer:
   *
   * The underlying timer of GNOME To DO.
   */
  g_object_class_install_property (
        object_class,
        PROP_TIMER,
        g_param_spec_object ("timer",
                             "The timer",
                             "The timer of the application",
                             GTD_TYPE_TIMER,
                             G_PARAM_READABLE));

  /**
   * GtdManager::plugin-manager:
   *
   * The plugin manager.
   */
  g_object_class_install_property (
        object_class,
        PROP_PLUGIN_MANAGER,
        g_param_spec_object ("plugin-manager",
                             "The plugin manager",
                             "The plugin manager of the application",
                             GTD_TYPE_PLUGIN_MANAGER,
                             G_PARAM_READABLE | G_PARAM_PRIVATE));

  /**
   * GtdManager::list-added:
   * @manager: a #GtdManager
   * @list: a #GtdTaskList
   *
   * The ::list-added signal is emmited after a #GtdTaskList
   * is connected.
   */
  signals[LIST_ADDED] = g_signal_new ("list-added",
                                      GTD_TYPE_MANAGER,
                                      G_SIGNAL_RUN_LAST,
                                      0,
                                      NULL,
                                      NULL,
                                      NULL,
                                      G_TYPE_NONE,
                                      1,
                                      GTD_TYPE_TASK_LIST);

  /**
   * GtdManager::list-changed:
   * @manager: a #GtdManager
   * @list: a #GtdTaskList
   *
   * The ::list-changed signal is emmited after a #GtdTaskList
   * has any of it's properties changed.
   */
  signals[LIST_CHANGED] = g_signal_new ("list-changed",
                                        GTD_TYPE_MANAGER,
                                        G_SIGNAL_RUN_LAST,
                                        0,
                                        NULL,
                                        NULL,
                                        NULL,
                                        G_TYPE_NONE,
                                        1,
                                        GTD_TYPE_TASK_LIST);

  /**
   * GtdManager::list-removed:
   * @manager: a #GtdManager
   * @list: a #GtdTaskList
   *
   * The ::list-removed signal is emmited after a #GtdTaskList
   * is disconnected.
   */
  signals[LIST_REMOVED] = g_signal_new ("list-removed",
                                        GTD_TYPE_MANAGER,
                                        G_SIGNAL_RUN_LAST,
                                        0,
                                        NULL,
                                        NULL,
                                        NULL,
                                        G_TYPE_NONE,
                                        1,
                                        GTD_TYPE_TASK_LIST);

  /**
   * GtdManager::show-error-message:
   * @manager: a #GtdManager
   * @primary_text: the primary message
   * @secondary_text: the detailed explanation of the error or the text to the notification button.
   * @action : optionally action of type GtdNotificationActionFunc ignored if it's null.
   * @user_data : user data passed to the action.
   *
   * Notifies about errors, and sends the error message for widgets
   * to display.
   */
  signals[SHOW_ERROR_MESSAGE] = g_signal_new ("show-error-message",
                                              GTD_TYPE_MANAGER,
                                              G_SIGNAL_RUN_LAST,
                                              0,
                                              NULL,
                                              NULL,
                                              NULL,
                                              G_TYPE_NONE,
                                              4,
                                              G_TYPE_STRING,
                                              G_TYPE_STRING,
                                              G_TYPE_POINTER,
                                              G_TYPE_POINTER);

  /**
   * GtdManager::panel-added:
   * @manager: a #GtdManager
   * @panel: a #GtdPanel
   *
   * The ::panel-added signal is emmited after a #GtdPanel
   * is added.
   */
  signals[PANEL_ADDED] = g_signal_new ("panel-added",
                                        GTD_TYPE_MANAGER,
                                        G_SIGNAL_RUN_LAST,
                                        0,
                                        NULL,
                                        NULL,
                                        NULL,
                                        G_TYPE_NONE,
                                        1,
                                        GTD_TYPE_PANEL);

  /**
   * GtdManager::panel-removed:
   * @manager: a #GtdManager
   * @panel: a #GtdPanel
   *
   * The ::panel-removed signal is emmited after a #GtdPanel
   * is removed from the list.
   */
  signals[PANEL_REMOVED] = g_signal_new ("panel-removed",
                                         GTD_TYPE_MANAGER,
                                         G_SIGNAL_RUN_LAST,
                                         0,
                                         NULL,
                                         NULL,
                                         NULL,
                                         G_TYPE_NONE,
                                         1,
                                         GTD_TYPE_PANEL);

  /**
   * GtdManager::provider-added:
   * @manager: a #GtdManager
   * @provider: a #GtdProvider
   *
   * The ::provider-added signal is emmited after a #GtdProvider
   * is added.
   */
  signals[PROVIDER_ADDED] = g_signal_new ("provider-added",
                                          GTD_TYPE_MANAGER,
                                          G_SIGNAL_RUN_LAST,
                                          0,
                                          NULL,
                                          NULL,
                                          NULL,
                                          G_TYPE_NONE,
                                          1,
                                          GTD_TYPE_PROVIDER);

  /**
   * GtdManager::provider-removed:
   * @manager: a #GtdManager
   * @provider: a #GtdProvider
   *
   * The ::provider-removed signal is emmited after a #GtdProvider
   * is removed from the list.
   */
  signals[PROVIDER_REMOVED] = g_signal_new ("provider-removed",
                                            GTD_TYPE_MANAGER,
                                            G_SIGNAL_RUN_LAST,
                                            0,
                                            NULL,
                                            NULL,
                                            NULL,
                                            G_TYPE_NONE,
                                            1,
                                            GTD_TYPE_PROVIDER);
}

static void
gtd_manager__default_list_changed_cb (GtdProvider *provider,
                                      GParamSpec  *pspec,
                                      GtdManager  *self)
{
  g_object_notify (G_OBJECT (self), "default-task-list");
}

static void
gtd_manager__task_list_modified (GtdTaskList *list,
                                 GtdTask     *task,
                                 GtdManager  *self)
{
  g_signal_emit (self, signals[LIST_CHANGED], 0, list);
}

static void
gtd_manager__panel_added (GtdPluginManager *plugin_manager,
                          GtdPanel         *panel,
                          GtdManager       *self)
{
  GtdManagerPrivate *priv = gtd_manager_get_instance_private (self);

  priv->panels = g_list_append (priv->panels, panel);

  g_signal_emit (self, signals[PANEL_ADDED], 0, panel);
}

static void
gtd_manager__panel_removed (GtdPluginManager *plugin_manager,
                            GtdPanel         *panel,
                            GtdManager       *self)
{
  GtdManagerPrivate *priv = gtd_manager_get_instance_private (self);

  priv->panels = g_list_remove (priv->panels, panel);

  g_signal_emit (self, signals[PANEL_REMOVED], 0, panel);
}

static void
gtd_manager__list_added (GtdProvider *provider,
                         GtdTaskList *list,
                         GtdManager  *self)
{
  GtdManagerPrivate *priv = gtd_manager_get_instance_private (self);

  priv->tasklists = g_list_append (priv->tasklists, list);

  g_signal_connect (list,
                    "task-added",
                    G_CALLBACK (gtd_manager__task_list_modified),
                    self);

  g_signal_connect (list,
                    "task-updated",
                    G_CALLBACK (gtd_manager__task_list_modified),
                    self);

  g_signal_connect (list,
                    "task-removed",
                    G_CALLBACK (gtd_manager__task_list_modified),
                    self);

  g_signal_emit (self, signals[LIST_ADDED], 0, list);
}

static void
gtd_manager__list_changed (GtdProvider *provider,
                           GtdTaskList *list,
                           GtdManager  *self)
{
  g_signal_emit (self, signals[LIST_CHANGED], 0, list);
}

static void
gtd_manager__list_removed (GtdProvider *provider,
                           GtdTaskList *list,
                           GtdManager  *self)
{
  GtdManagerPrivate *priv = gtd_manager_get_instance_private (self);

  if (!list)
      return;

  priv->tasklists = g_list_remove (priv->tasklists, list);

  g_signal_handlers_disconnect_by_func (list,
                                        gtd_manager__task_list_modified,
                                        self);

  g_signal_emit (self, signals[LIST_REMOVED], 0, list);
}

static void
gtd_manager__provider_added (GtdPluginManager *plugin_manager,
                             GtdProvider      *provider,
                             GtdManager       *self)
{
  GtdManagerPrivate *priv = gtd_manager_get_instance_private (self);
  GList *lists;
  GList *l;

  priv->providers = g_list_append (priv->providers, provider);

  /* Add lists */
  lists = gtd_provider_get_task_lists (provider);

  for (l = lists; l != NULL; l = l->next)
    gtd_manager__list_added (provider, l->data, self);

  g_signal_connect (provider,
                    "list-added",
                    G_CALLBACK (gtd_manager__list_added),
                    self);

  g_signal_connect (provider,
                    "list-changed",
                    G_CALLBACK (gtd_manager__list_changed),
                    self);

  g_signal_connect (provider,
                    "list-removed",
                    G_CALLBACK (gtd_manager__list_removed),
                    self);

  /* If we just added the default provider, update the property */
  check_provider_is_default (self, provider);

  g_signal_emit (self, signals[PROVIDER_ADDED], 0, provider);
}

static void
gtd_manager__provider_removed (GtdPluginManager *plugin_manager,
                               GtdProvider      *provider,
                               GtdManager       *self)
{
  GtdManagerPrivate *priv = gtd_manager_get_instance_private (self);
  GList *lists;
  GList *l;

  priv->providers = g_list_remove (priv->providers, provider);

  /* Remove lists */
  lists = gtd_provider_get_task_lists (provider);

  for (l = lists; l != NULL; l = l->next)
    gtd_manager__list_removed (provider, l->data, self);

  /* Disconnect old signals */
  
  g_signal_handlers_disconnect_by_func (provider,
                                        gtd_manager__default_list_changed_cb,
                                        self);

  g_signal_handlers_disconnect_by_func (provider,
                                        gtd_manager__list_added,
                                        self);

  g_signal_handlers_disconnect_by_func (provider,
                                        gtd_manager__list_changed,
                                        self);

  g_signal_handlers_disconnect_by_func (provider,
                                        gtd_manager__list_removed,
                                        self);

  g_signal_emit (self, signals[PROVIDER_REMOVED], 0, provider);
}

static void
gtd_manager_init (GtdManager *self)
{
  self->priv = gtd_manager_get_instance_private (self);
  self->priv->settings = g_settings_new ("org.gnome.todo");
  self->priv->plugin_manager = gtd_plugin_manager_new ();
  self->priv->timer = gtd_timer_new ();
}

/**
 * gtd_manager_get_default:
 *
 * Retrieves the singleton #GtdManager instance. You should always
 * use this function instead of @gtd_manager_new.
 *
 * Returns: (transfer none): the singleton #GtdManager instance.
 */
GtdManager*
gtd_manager_get_default (void)
{
  if (!gtd_manager_instance)
    gtd_manager_instance = gtd_manager_new ();

  return gtd_manager_instance;
}

GtdManager*
gtd_manager_new (void)
{
  return g_object_new (GTD_TYPE_MANAGER, NULL);
}

/**
 * gtd_manager_create_task:
 * @manager: a #GtdManager
 * @task: a #GtdTask
 *
 * Ask for @task's parent list source to create @task.
 */
void
gtd_manager_create_task (GtdManager *manager,
                         GtdTask    *task)
{
  GtdTaskList *list;
  GtdProvider *provider;

  g_return_if_fail (GTD_IS_MANAGER (manager));
  g_return_if_fail (GTD_IS_TASK (task));

  list = gtd_task_get_list (task);
  provider = gtd_task_list_get_provider (list);

  gtd_provider_create_task (provider, task);
}

/**
 * gtd_manager_remove_task:
 * @manager: a #GtdManager
 * @task: a #GtdTask
 *
 * Ask for @task's parent list source to remove @task.
 */
void
gtd_manager_remove_task (GtdManager *manager,
                         GtdTask    *task)
{
  GtdTaskList *list;
  GtdProvider *provider;

  g_return_if_fail (GTD_IS_MANAGER (manager));
  g_return_if_fail (GTD_IS_TASK (task));

  list = gtd_task_get_list (task);
  provider = gtd_task_list_get_provider (list);

  gtd_provider_remove_task (provider, task);
}

/**
 * gtd_manager_update_task:
 * @manager: a #GtdManager
 * @task: a #GtdTask
 *
 * Ask for @task's parent list source to update @task.
 */
void
gtd_manager_update_task (GtdManager *manager,
                         GtdTask    *task)
{
  GtdTaskList *list;
  GtdProvider *provider;

  g_return_if_fail (GTD_IS_MANAGER (manager));
  g_return_if_fail (GTD_IS_TASK (task));

  list = gtd_task_get_list (task);

  /* Task does not have parent list, nothing we can do */
  if (!list)
      return;

  provider = gtd_task_list_get_provider (list);

  gtd_provider_update_task (provider, task);
}

/**
 * gtd_manager_create_task_list:
 * @manager: a #GtdManager
 * @list: a #GtdTaskList
 *
 * Creates a new task list at the given source.
 */
void
gtd_manager_create_task_list (GtdManager  *manager,
                              GtdTaskList *list)
{
  GtdProvider *provider;

  g_return_if_fail (GTD_IS_MANAGER (manager));
  g_return_if_fail (GTD_IS_TASK_LIST (list));

  provider = gtd_task_list_get_provider (list);

  gtd_provider_create_task_list (provider, list);
}

/**
 * gtd_manager_remove_task_list:
 * @manager: a #GtdManager
 * @list: a #GtdTaskList
 *
 * Deletes @list from the registry.
 */
void
gtd_manager_remove_task_list (GtdManager  *manager,
                              GtdTaskList *list)
{
  GtdProvider *provider;

  g_return_if_fail (GTD_IS_MANAGER (manager));
  g_return_if_fail (GTD_IS_TASK_LIST (list));

  provider = gtd_task_list_get_provider (list);

  gtd_provider_remove_task_list (provider, list);

  g_signal_emit (manager,
                 signals[LIST_REMOVED],
                 0,
                 list);
}

/**
 * gtd_manager_save_task_list:
 * @manager: a #GtdManager
 * @list: a #GtdTaskList
 *
 * Save or create @list.
 */
void
gtd_manager_save_task_list (GtdManager  *manager,
                            GtdTaskList *list)
{
  GtdProvider *provider;

  g_return_if_fail (GTD_IS_MANAGER (manager));
  g_return_if_fail (GTD_IS_TASK_LIST (list));

  provider = gtd_task_list_get_provider (list);

  gtd_provider_update_task_list (provider, list);
}

/**
 * gtd_manager_get_task_lists:
 * @manager: a #GtdManager
 *
 * Retrieves the list of #GtdTaskList already loaded.
 *
 * Returns: (transfer container) (element-type Gtd.TaskList): a newly allocated list of #GtdTaskList, or %NULL if none.
 */
GList*
gtd_manager_get_task_lists (GtdManager *manager)
{
  g_return_val_if_fail (GTD_IS_MANAGER (manager), NULL);

  return g_list_copy (manager->priv->tasklists);
}

/**
 * gtd_manager_get_providers:
 * @manager: a #GtdManager
 *
 * Retrieves the list of available #GtdProvider.
 *
 * Returns: (transfer container) (element-type Gtd.Provider): a newly allocated #GList of
 * #GtdStorage. Free with @g_list_free after use.
 */
GList*
gtd_manager_get_providers (GtdManager *manager)
{
  g_return_val_if_fail (GTD_IS_MANAGER (manager), NULL);

  return g_list_copy (manager->priv->providers);
}

/**
 * gtd_manager_get_panels:
 * @manager: a #GtdManager
 *
 * Retrieves the list of currently loaded #GtdPanel
 * instances.
 *
 * Returns: (transfer container) (element-type Gtd.Panel): a #GList of #GtdPanel
 */
GList*
gtd_manager_get_panels (GtdManager *manager)
{
  g_return_val_if_fail (GTD_IS_MANAGER (manager), NULL);

  return g_list_copy (manager->priv->panels);
}

/**
 * gtd_manager_get_default_provider:
 * @manager: a #GtdManager
 *
 * Retrieves the default provider location. Default is "local".
 *
 * Returns: (transfer none): the default provider.
 */
GtdProvider*
gtd_manager_get_default_provider (GtdManager *manager)
{
  g_return_val_if_fail (GTD_IS_MANAGER (manager), NULL);

  return manager->priv->default_provider;
}

/**
 * gtd_manager_set_default_provider:
 * @manager: a #GtdManager
 * @provider: (nullable): the default provider.
 *
 * Sets the provider.
 */
void
gtd_manager_set_default_provider (GtdManager  *manager,
                                  GtdProvider *provider)
{
  GtdManagerPrivate *priv;
  GtdProvider *previous;

  g_return_if_fail (GTD_IS_MANAGER (manager));

  priv = manager->priv;
  previous = priv->default_provider;

  if (g_set_object (&priv->default_provider, provider))
    {
      g_settings_set_string (priv->settings,
                             "default-provider",
                             provider ? gtd_provider_get_id (provider) : "local");

      /* Disconnect the previous provider... */
      if (previous)
        {
          g_signal_handlers_disconnect_by_func (previous,
                                                gtd_manager__default_list_changed_cb,
                                                manager);
        }

      /* ... and connect the current one */
      if (provider)
        {
          g_signal_connect (provider,
                            "notify::default-task-list",
                            G_CALLBACK (gtd_manager__default_list_changed_cb),
                            manager);
        }

      g_object_notify (G_OBJECT (manager), "default-provider");
      g_object_notify (G_OBJECT (manager), "default-task-list");
    }
}

/**
 * gtd_manager_get_default_task_list:
 * @self: a #GtdManager
 *
 * Retrieves the default tasklist of the default provider.
 *
 * Returns: (transfer none)(nullable): a #GtdTaskList
 */
GtdTaskList*
gtd_manager_get_default_task_list (GtdManager *self)
{
  GtdManagerPrivate *priv;

  g_return_val_if_fail (GTD_IS_MANAGER (self), NULL);

  priv = gtd_manager_get_instance_private (self);

  if (!priv->default_provider)
    return NULL;

  return gtd_provider_get_default_task_list (priv->default_provider);
}

/**
 * gtd_manager_set_default_task_list:
 * @self: a #GtdManager
 * @list: (nullable): a #GtdTaskList, or %NULL
 *
 * Sets the default task list of the application.
 */
void
gtd_manager_set_default_task_list (GtdManager  *self,
                                   GtdTaskList *list)
{
  g_return_if_fail (GTD_IS_MANAGER (self));
  g_return_if_fail (GTD_IS_TASK_LIST (list));

  if (list)
    {
      GtdProvider *provider;

      provider = gtd_task_list_get_provider (list);

      gtd_manager_set_default_provider (self, provider);
      gtd_provider_set_default_task_list (provider, list);
    }

  g_object_notify (G_OBJECT (self), "default-task-list");
}

/**
 * gtd_manager_get_settings:
 * @manager: a #GtdManager
 *
 * Retrieves the internal #GSettings from @manager.
 *
 * Returns: (transfer none): the internal #GSettings of @manager
 */
GSettings*
gtd_manager_get_settings (GtdManager *manager)
{
  g_return_val_if_fail (GTD_IS_MANAGER (manager), NULL);

  return manager->priv->settings;
}

/**
 * gtd_manager_get_is_first_run:
 * @manager: a #GtdManager
 *
 * Retrieves the 'first-run' setting.
 *
 * Returns: %TRUE if GNOME To Do was never run before, %FALSE otherwise.
 */
gboolean
gtd_manager_get_is_first_run (GtdManager *manager)
{
  g_return_val_if_fail (GTD_IS_MANAGER (manager), FALSE);

  return g_settings_get_boolean (manager->priv->settings, "first-run");
}

/**
 * gtd_manager_set_is_first_run:
 * @manager: a #GtdManager
 * @is_first_run: %TRUE to make it first run, %FALSE otherwise.
 *
 * Sets the 'first-run' setting.
 */
void
gtd_manager_set_is_first_run (GtdManager *manager,
                              gboolean    is_first_run)
{
  g_return_if_fail (GTD_IS_MANAGER (manager));

  g_settings_set_boolean (manager->priv->settings,
                          "first-run",
                          is_first_run);
}

void
gtd_manager_emit_error_message (GtdManager                *manager,
                                const gchar               *primary_message,
                                const gchar               *secondary_message,
                                GtdNotificationActionFunc  function,
                                gpointer                   user_data)
{
  g_return_if_fail (GTD_IS_MANAGER (manager));

  emit_show_error_message (manager,
                           primary_message,
                           secondary_message,
                           function,
                           user_data);
}

/**
 * gtd_manager_get_timer:
 * @self: a #GtdManager
 *
 * Retrieves the #GtdTimer from @self. You can use the
 * timer to know when your code should be updated.
 *
 * Returns: (transfer none): a #GtdTimer
 */
GtdTimer*
gtd_manager_get_timer (GtdManager *self)
{
  GtdManagerPrivate *priv;

  g_return_val_if_fail (GTD_IS_MANAGER (self), NULL);

  priv = gtd_manager_get_instance_private (self);

  return priv->timer;
}

void
gtd_manager_load_plugins (GtdManager *manager)
{
  GtdManagerPrivate *priv = gtd_manager_get_instance_private (manager);

  g_signal_connect (priv->plugin_manager,
                    "panel-registered",
                    G_CALLBACK (gtd_manager__panel_added),
                    manager);

  g_signal_connect (priv->plugin_manager,
                    "panel-unregistered",
                    G_CALLBACK (gtd_manager__panel_removed),
                    manager);

  g_signal_connect (priv->plugin_manager,
                    "provider-registered",
                    G_CALLBACK (gtd_manager__provider_added),
                    manager);

  g_signal_connect (priv->plugin_manager,
                    "provider-unregistered",
                    G_CALLBACK (gtd_manager__provider_removed),
                    manager);

  gtd_plugin_manager_load_plugins (priv->plugin_manager);
}

GtdPluginManager*
gtd_manager_get_plugin_manager (GtdManager *manager)
{
  g_return_val_if_fail (GTD_IS_MANAGER (manager), NULL);

  return manager->priv->plugin_manager;
}
