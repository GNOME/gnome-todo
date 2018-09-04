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
#include "notification/gtd-notification.h"
#include "gtd-debug.h"
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

struct _GtdManager
{
  GtdObject           parent;

  GSettings          *settings;
  GtdPluginManager   *plugin_manager;

  GList              *tasklists;
  GList              *providers;
  GList              *panels;
  GtdProvider        *default_provider;
  GtdTimer           *timer;

  GCancellable       *cancellable;
};

G_DEFINE_TYPE (GtdManager, gtd_manager, GTD_TYPE_OBJECT)

/* Singleton instance */
GtdManager *gtd_manager_instance = NULL;

enum
{
  LIST_ADDED,
  LIST_CHANGED,
  LIST_REMOVED,
  SHOW_ERROR_MESSAGE,
  SHOW_NOTIFICATION,
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


/*
 * Auxiliary methods
 */

static void
check_provider_is_default (GtdManager  *self,
                           GtdProvider *provider)
{
  g_autofree gchar *default_provider = NULL;

  default_provider = g_settings_get_string (self->settings, "default-provider");

  if (g_strcmp0 (default_provider, gtd_provider_get_id (provider)) == 0)
    gtd_manager_set_default_provider (self, provider);
}


/*
 * Callbacks
 */

static void
on_default_list_changed_cb (GtdProvider *provider,
                            GParamSpec  *pspec,
                            GtdManager  *self)
{
  g_object_notify (G_OBJECT (self), "default-task-list");
}

static void
on_task_list_modified_cb (GtdTaskList *list,
                          GtdTask     *task,
                          GtdManager  *self)
{
  g_signal_emit (self, signals[LIST_CHANGED], 0, list);
}

static void
on_panel_added_cb (GtdPluginManager *plugin_manager,
                   GtdPanel         *panel,
                   GtdManager       *self)
{
  self->panels = g_list_append (self->panels, panel);

  g_signal_emit (self, signals[PANEL_ADDED], 0, panel);
}

static void
on_panel_removed_cb (GtdPluginManager *plugin_manager,
                     GtdPanel         *panel,
                     GtdManager       *self)
{
  self->panels = g_list_remove (self->panels, panel);

  g_signal_emit (self, signals[PANEL_REMOVED], 0, panel);
}

static void
on_list_added_cb (GtdProvider *provider,
                  GtdTaskList *list,
                  GtdManager  *self)
{
  self->tasklists = g_list_append (self->tasklists, list);

  g_signal_connect (list,
                    "task-added",
                    G_CALLBACK (on_task_list_modified_cb),
                    self);

  g_signal_connect (list,
                    "task-updated",
                    G_CALLBACK (on_task_list_modified_cb),
                    self);

  g_signal_connect (list,
                    "task-removed",
                    G_CALLBACK (on_task_list_modified_cb),
                    self);

  g_signal_emit (self, signals[LIST_ADDED], 0, list);
}

static void
on_list_changed_cb (GtdProvider *provider,
                    GtdTaskList *list,
                    GtdManager  *self)
{
  g_signal_emit (self, signals[LIST_CHANGED], 0, list);
}

static void
on_list_removed_cb (GtdProvider *provider,
                    GtdTaskList *list,
                    GtdManager  *self)
{
  if (!list)
      return;

  self->tasklists = g_list_remove (self->tasklists, list);

  g_signal_handlers_disconnect_by_func (list,
                                        on_task_list_modified_cb,
                                        self);

  g_signal_emit (self, signals[LIST_REMOVED], 0, list);
}

static void
on_provider_added_cb (GtdPluginManager *plugin_manager,
                      GtdProvider      *provider,
                      GtdManager       *self)
{
  g_autoptr (GList) lists = NULL;
  GList *l;

  self->providers = g_list_append (self->providers, provider);

  /* Add lists */
  lists = gtd_provider_get_task_lists (provider);

  for (l = lists; l != NULL; l = l->next)
    on_list_added_cb (provider, l->data, self);

  g_signal_connect (provider,
                    "list-added",
                    G_CALLBACK (on_list_added_cb),
                    self);

  g_signal_connect (provider,
                    "list-changed",
                    G_CALLBACK (on_list_changed_cb),
                    self);

  g_signal_connect (provider,
                    "list-removed",
                    G_CALLBACK (on_list_removed_cb),
                    self);

  /* If we just added the default provider, update the property */
  check_provider_is_default (self, provider);

  g_signal_emit (self, signals[PROVIDER_ADDED], 0, provider);
}

static void
on_provider_removed_cb (GtdPluginManager *plugin_manager,
                        GtdProvider      *provider,
                        GtdManager       *self)
{
  g_autoptr (GList) lists = NULL;
  GList *l;

  self->providers = g_list_remove (self->providers, provider);

  /* Remove lists */
  lists = gtd_provider_get_task_lists (provider);

  for (l = lists; l != NULL; l = l->next)
    on_list_removed_cb (provider, l->data, self);

  g_signal_handlers_disconnect_by_func (provider, on_default_list_changed_cb, self);
  g_signal_handlers_disconnect_by_func (provider, on_list_added_cb, self);
  g_signal_handlers_disconnect_by_func (provider, on_list_changed_cb, self);
  g_signal_handlers_disconnect_by_func (provider, on_list_removed_cb, self);

  g_signal_emit (self, signals[PROVIDER_REMOVED], 0, provider);
}


/*
 * GObject overrides
 */

static void
gtd_manager_finalize (GObject *object)
{
  GtdManager *self = (GtdManager *)object;

  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);
  g_clear_object (&self->plugin_manager);
  g_clear_object (&self->settings);
  g_clear_object (&self->timer);

  G_OBJECT_CLASS (gtd_manager_parent_class)->finalize (object);
}

static void
gtd_manager_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  GtdManager *self = (GtdManager *) object;

  switch (prop_id)
    {
    case PROP_DEFAULT_PROVIDER:
      g_value_set_object (value, self->default_provider);
      break;

    case PROP_DEFAULT_TASKLIST:
      g_value_set_object (value, gtd_provider_get_default_task_list (self->default_provider));
      break;

    case PROP_TIMER:
      g_value_set_object (value, self->timer);
      break;

    case PROP_PLUGIN_MANAGER:
      g_value_set_object (value, self->plugin_manager);
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
  GtdManager *self = (GtdManager *) object;

  switch (prop_id)
    {
    case PROP_DEFAULT_PROVIDER:
      if (g_set_object (&self->default_provider, g_value_get_object (value)))
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
   * GtdManager::show-notification:
   * @manager: a #GtdManager
   * @notification: the #GtdNotification
   *
   * Sends a notification.
   */
  signals[SHOW_NOTIFICATION] = g_signal_new ("show-notification",
                                             GTD_TYPE_MANAGER,
                                             G_SIGNAL_RUN_LAST,
                                             0,
                                             NULL,
                                             NULL,
                                             NULL,
                                             G_TYPE_NONE,
                                             1,
                                             GTD_TYPE_NOTIFICATION);

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
gtd_manager_init (GtdManager *self)
{
  self->settings = g_settings_new ("org.gnome.todo");
  self->plugin_manager = gtd_plugin_manager_new ();
  self->timer = gtd_timer_new ();
  self->cancellable = g_cancellable_new ();
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
 * gtd_manager_get_task_lists:
 * @manager: a #GtdManager
 *
 * Retrieves the list of #GtdTaskList already loaded.
 *
 * Returns: (transfer container) (element-type Gtd.TaskList): a newly allocated list of #GtdTaskList, or %NULL if none.
 */
GList*
gtd_manager_get_task_lists (GtdManager *self)
{
  g_return_val_if_fail (GTD_IS_MANAGER (self), NULL);

  return g_list_copy (self->tasklists);
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
gtd_manager_get_providers (GtdManager *self)
{
  g_return_val_if_fail (GTD_IS_MANAGER (self), NULL);

  return g_list_copy (self->providers);
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
gtd_manager_get_panels (GtdManager *self)
{
  g_return_val_if_fail (GTD_IS_MANAGER (self), NULL);

  return g_list_copy (self->panels);
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
gtd_manager_get_default_provider (GtdManager *self)
{
  g_return_val_if_fail (GTD_IS_MANAGER (self), NULL);

  return self->default_provider;
}

/**
 * gtd_manager_set_default_provider:
 * @manager: a #GtdManager
 * @provider: (nullable): the default provider.
 *
 * Sets the provider.
 */
void
gtd_manager_set_default_provider (GtdManager  *self,
                                  GtdProvider *provider)
{
  GtdProvider *previous;

  g_return_if_fail (GTD_IS_MANAGER (self));

  previous = self->default_provider;

  if (g_set_object (&self->default_provider, provider))
    {
      g_settings_set_string (self->settings,
                             "default-provider",
                             provider ? gtd_provider_get_id (provider) : "local");

      /* Disconnect the previous provider... */
      if (previous)
        {
          g_signal_handlers_disconnect_by_func (previous,
                                                on_default_list_changed_cb,
                                                self);
        }

      /* ... and connect the current one */
      if (provider)
        {
          g_signal_connect (provider,
                            "notify::default-task-list",
                            G_CALLBACK (on_default_list_changed_cb),
                            self);
        }

      g_object_notify (G_OBJECT (self), "default-provider");
      g_object_notify (G_OBJECT (self), "default-task-list");
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
  g_return_val_if_fail (GTD_IS_MANAGER (self), NULL);

  if (!self->default_provider)
    return NULL;

  return gtd_provider_get_default_task_list (self->default_provider);
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
gtd_manager_get_settings (GtdManager *self)
{
  g_return_val_if_fail (GTD_IS_MANAGER (self), NULL);

  return self->settings;
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
gtd_manager_get_is_first_run (GtdManager *self)
{
  g_return_val_if_fail (GTD_IS_MANAGER (self), FALSE);

  return g_settings_get_boolean (self->settings, "first-run");
}

/**
 * gtd_manager_set_is_first_run:
 * @manager: a #GtdManager
 * @is_first_run: %TRUE to make it first run, %FALSE otherwise.
 *
 * Sets the 'first-run' setting.
 */
void
gtd_manager_set_is_first_run (GtdManager *self,
                              gboolean    is_first_run)
{
  g_return_if_fail (GTD_IS_MANAGER (self));

  g_settings_set_boolean (self->settings, "first-run", is_first_run);
}

/**
 * gtd_manager_emit_error_message:
 * @manager: a #GtdManager
 * @title: (nullable): the title of the error
 * @description: (nullable): detailed description of the error
 * @function: (nullable): function to be called when the notification is dismissed
 * @user_data: user data
 *
 * Reports an error.
 */
void
gtd_manager_emit_error_message (GtdManager         *manager,
                                const gchar        *title,
                                const gchar        *description,
                                GtdErrorActionFunc  function,
                                gpointer            user_data)
{
  g_return_if_fail (GTD_IS_MANAGER (manager));

  g_signal_emit (manager,
                 signals[SHOW_ERROR_MESSAGE],
                 0,
                 title,
                 description,
                 function,
                 user_data);
}

/**
 * gtd_manager_send_notification:
 * @self: a #GtdManager
 * @notification: a #GtdNotification
 *
 * Sends a notification to the notification system.
 */
void
gtd_manager_send_notification (GtdManager      *self,
                               GtdNotification *notification)
{
  g_return_if_fail (GTD_IS_MANAGER (self));

  g_signal_emit (self, signals[SHOW_NOTIFICATION], 0, notification);
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

  g_return_val_if_fail (GTD_IS_MANAGER (self), NULL);

  return self->timer;
}

void
gtd_manager_load_plugins (GtdManager *self)
{
  GTD_ENTRY;

  g_signal_connect (self->plugin_manager,
                    "panel-registered",
                    G_CALLBACK (on_panel_added_cb),
                    self);

  g_signal_connect (self->plugin_manager,
                    "panel-unregistered",
                    G_CALLBACK (on_panel_removed_cb),
                    self);

  g_signal_connect (self->plugin_manager,
                    "provider-registered",
                    G_CALLBACK (on_provider_added_cb),
                    self);

  g_signal_connect (self->plugin_manager,
                    "provider-unregistered",
                    G_CALLBACK (on_provider_removed_cb),
                    self);

  gtd_plugin_manager_load_plugins (self->plugin_manager);

  GTD_EXIT;
}

GtdPluginManager*
gtd_manager_get_plugin_manager (GtdManager *self)
{
  g_return_val_if_fail (GTD_IS_MANAGER (self), NULL);

  return self->plugin_manager;
}
