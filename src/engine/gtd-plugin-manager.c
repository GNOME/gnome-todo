/* gtd-plugin-manager.c
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

#define G_LOG_DOMAIN "GtdPluginManager"

#include "interfaces/gtd-activatable.h"
#include "interfaces/gtd-panel.h"
#include "interfaces/gtd-provider.h"
#include "gtd-manager.h"
#include "gtd-plugin-manager.h"

#include <libpeas/peas.h>

struct _GtdPluginManager
{
  GtdObject           parent;

  GHashTable         *info_to_extension;
};

G_DEFINE_TYPE (GtdPluginManager, gtd_plugin_manager, GTD_TYPE_OBJECT)

enum
{
  PROP_0,
  LAST_PROP
};

enum
{
  PANEL_REGISTERED,
  PANEL_UNREGISTERED,
  PLUGIN_LOADED,
  PLUGIN_UNLOADED,
  PROVIDER_REGISTERED,
  PROVIDER_UNREGISTERED,
  NUM_SIGNALS
};

static guint signals[NUM_SIGNALS] = { 0, };

static void
gtd_plugin_manager_finalize (GObject *object)
{
  GtdPluginManager *self = (GtdPluginManager *)object;

  g_clear_pointer (&self->info_to_extension, g_hash_table_destroy);

  G_OBJECT_CLASS (gtd_plugin_manager_parent_class)->finalize (object);
}

static void
gtd_plugin_manager_class_init (GtdPluginManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gtd_plugin_manager_finalize;

  signals[PANEL_REGISTERED] = g_signal_new ("panel-registered",
                                             GTD_TYPE_PLUGIN_MANAGER,
                                             G_SIGNAL_RUN_FIRST,
                                             0,
                                             NULL,
                                             NULL,
                                             NULL,
                                             G_TYPE_NONE,
                                             1,
                                             GTD_TYPE_PANEL);

  signals[PANEL_UNREGISTERED] = g_signal_new ("panel-unregistered",
                                              GTD_TYPE_PLUGIN_MANAGER,
                                              G_SIGNAL_RUN_FIRST,
                                              0,
                                              NULL,
                                              NULL,
                                              NULL,
                                              G_TYPE_NONE,
                                              1,
                                              GTD_TYPE_PANEL);

  signals[PLUGIN_LOADED] = g_signal_new ("plugin-loaded",
                                         GTD_TYPE_PLUGIN_MANAGER,
                                         G_SIGNAL_RUN_FIRST,
                                         0,
                                         NULL,
                                         NULL,
                                         NULL,
                                         G_TYPE_NONE,
                                         2,
                                         PEAS_TYPE_PLUGIN_INFO,
                                         GTD_TYPE_ACTIVATABLE);

  signals[PLUGIN_UNLOADED] = g_signal_new ("plugin-unloaded",
                                           GTD_TYPE_PLUGIN_MANAGER,
                                           G_SIGNAL_RUN_FIRST,
                                           0,
                                           NULL,
                                           NULL,
                                           NULL,
                                           G_TYPE_NONE,
                                           2,
                                           PEAS_TYPE_PLUGIN_INFO,
                                           GTD_TYPE_ACTIVATABLE);

  signals[PROVIDER_REGISTERED] = g_signal_new ("provider-registered",
                                               GTD_TYPE_PLUGIN_MANAGER,
                                               G_SIGNAL_RUN_FIRST,
                                               0,
                                               NULL,
                                               NULL,
                                               NULL,
                                               G_TYPE_NONE,
                                               1,
                                               G_TYPE_POINTER);

  signals[PROVIDER_UNREGISTERED] = g_signal_new ("provider-unregistered",
                                                 GTD_TYPE_PLUGIN_MANAGER,
                                                 G_SIGNAL_RUN_FIRST,
                                                 0,
                                                 NULL,
                                                 NULL,
                                                 NULL,
                                                 G_TYPE_NONE,
                                                 1,
                                                 G_TYPE_POINTER);
}

static void
on_panel_added (GtdActivatable   *activatable,
                GtdPanel         *panel,
                GtdPluginManager *self)
{
  g_signal_emit_by_name (self, "panel-registered", panel);
}

static void
on_panel_removed (GtdActivatable   *activatable,
                  GtdPanel         *panel,
                  GtdPluginManager *self)
{
  g_signal_emit_by_name (self, "panel-unregistered", panel);
}

static void
on_provider_added (GtdActivatable   *activatable,
                   GtdProvider      *provider,
                   GtdPluginManager *self)
{
  g_signal_emit_by_name (self, "provider-registered", provider);
}

static void
on_provider_removed (GtdActivatable   *activatable,
                     GtdProvider      *provider,
                     GtdPluginManager *self)
{
  g_signal_emit_by_name (self, "provider-unregistered", provider);
}

static void
on_plugin_unloaded (PeasEngine       *engine,
                    PeasPluginInfo   *info,
                    GtdPluginManager *self)
{
  GtdActivatable *activatable;
  GList *extension_providers;
  GList *extension_panels;
  GList *l;

  activatable = g_hash_table_lookup (self->info_to_extension, info);

  if (!activatable)
    return;

  /* Remove all panels */
  extension_panels = gtd_activatable_get_panels (activatable);

  for (l = extension_panels; l != NULL; l = l->next)
    on_panel_removed (activatable, l->data, self);

  /* Remove all registered providers */
  extension_providers = gtd_activatable_get_providers (activatable);

  for (l = extension_providers; l != NULL; l = l->next)
    on_provider_removed (activatable, l->data, self);

  /* Deactivates the extension */
  gtd_activatable_deactivate (activatable);

  /* Emit the signal */
  g_signal_emit (self, signals[PLUGIN_UNLOADED], 0, info, activatable);

  /* Disconnect old signals */
  g_signal_handlers_disconnect_by_func (activatable,
                                        on_panel_added,
                                        self);

  g_signal_handlers_disconnect_by_func (activatable,
                                        on_panel_removed,
                                        self);

  g_signal_handlers_disconnect_by_func (activatable,
                                        on_provider_added,
                                        self);

  g_signal_handlers_disconnect_by_func (activatable,
                                        on_provider_removed,
                                        self);

  g_hash_table_remove (self->info_to_extension, info);

  /* Destroy the extension */
  g_clear_object (&activatable);
}

static void
on_plugin_loaded (PeasEngine       *engine,
                  PeasPluginInfo   *info,
                  GtdPluginManager *self)
{
  if (peas_engine_provides_extension (engine, info, GTD_TYPE_ACTIVATABLE))
    {
      GtdActivatable *activatable;
      PeasExtension *extension;
      const GList *l;

      /*
       * Actually create the plugin object,
       * which should load all the providers.
       */
      extension = peas_engine_create_extension (engine,
                                                info,
                                                GTD_TYPE_ACTIVATABLE,
                                                NULL);

      /* All extensions shall be GtdActivatable impls */
      activatable = GTD_ACTIVATABLE (extension);

      g_hash_table_insert (self->info_to_extension,
                           info,
                           extension);

      /* Load all providers */
      for (l = gtd_activatable_get_providers (activatable); l != NULL; l = l->next)
        on_provider_added (activatable, l->data, self);

      /* Load all panels */
      for (l = gtd_activatable_get_panels (activatable); l != NULL; l = l->next)
        on_panel_added (activatable, l->data, self);

      g_signal_connect (activatable,
                        "provider-added",
                        G_CALLBACK (on_provider_added),
                        self);

      g_signal_connect (activatable,
                        "provider-removed",
                        G_CALLBACK (on_provider_removed),
                        self);

      g_signal_connect (activatable,
                        "panel-added",
                        G_CALLBACK (on_panel_added),
                        self);

      g_signal_connect (activatable,
                        "panel-removed",
                        G_CALLBACK (on_panel_removed),
                        self);

      /* Activate extension */
      gtd_activatable_activate (activatable);

      /* Emit the signal */
      g_signal_emit (self, signals[PLUGIN_LOADED], 0, info, extension);
    }
}

static void
setup_engine (GtdPluginManager *self)
{
  PeasEngine *engine;
  gchar *plugin_dir;

  engine = peas_engine_get_default ();

  /* Enable Python3 plugins */
  peas_engine_enable_loader (engine, "python3");

  /* Let Peas search for plugins in the specified directory */
  plugin_dir = g_build_filename (PACKAGE_LIB_DIR,
                                 "plugins",
                                 NULL);

  peas_engine_add_search_path (engine,
                               plugin_dir,
                               NULL);

  g_free (plugin_dir);

  /* User-installed plugins shall be detected too */
  plugin_dir = g_build_filename (g_get_home_dir (),
                                 ".local",
                                 "lib",
                                 "gnome-todo",
                                 "plugins",
                                 NULL);

  peas_engine_add_search_path (engine, plugin_dir, NULL);
  peas_engine_prepend_search_path (engine,
                                   "resource:///org/gnome/todo/plugins",
                                   "resource:///org/gnome/todo/plugins");

  g_free (plugin_dir);

  /* Hear about loaded plugins */
  g_signal_connect_after (engine,
                          "load-plugin",
                          G_CALLBACK (on_plugin_loaded),
                          self);

  g_signal_connect (engine,
                    "unload-plugin",
                    G_CALLBACK (on_plugin_unloaded),
                    self);
}

static void
gtd_plugin_manager_init (GtdPluginManager *self)
{
  self->info_to_extension = g_hash_table_new (g_direct_hash, g_direct_equal);

  gtd_object_set_ready (GTD_OBJECT (self), FALSE);

  setup_engine (self);

  gtd_object_set_ready (GTD_OBJECT (self), TRUE);
}

GtdPluginManager*
gtd_plugin_manager_new (void)
{
  return g_object_new (GTD_TYPE_PLUGIN_MANAGER, NULL);
}

void
gtd_plugin_manager_load_plugins (GtdPluginManager *self)
{
  PeasEngine *engine;
  GSettings *settings;

  engine = peas_engine_get_default ();
  settings = gtd_manager_get_settings (gtd_manager_get_default ());

  g_settings_bind (settings,
		   "active-extensions",
		   engine,
		   "loaded-plugins",
		   G_SETTINGS_BIND_DEFAULT);
}

GtdActivatable*
gtd_plugin_manager_get_plugin (GtdPluginManager *self,
                               PeasPluginInfo   *info)
{
  g_return_val_if_fail (GTD_IS_PLUGIN_MANAGER (self), NULL);

  return g_hash_table_lookup (self->info_to_extension, info);
}

GList*
gtd_plugin_manager_get_loaded_plugins (GtdPluginManager *self)
{
  g_return_val_if_fail (GTD_IS_PLUGIN_MANAGER (self), NULL);

  return g_hash_table_get_values (self->info_to_extension);
}
