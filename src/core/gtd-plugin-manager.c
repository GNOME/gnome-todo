/* gtd-plugin-manager.c
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

#define G_LOG_DOMAIN "GtdPluginManager"

#include "gtd-activatable.h"
#include "gtd-manager.h"
#include "gtd-panel.h"
#include "gtd-provider.h"
#include "gtd-plugin-manager.h"
#include "gtd-theme-manager.h"

#include <libpeas/peas.h>

struct _GtdPluginManager
{
  GtdObject           parent;

  GHashTable         *info_to_extension;
  GtdThemeManager    *theme_manager;
};

G_DEFINE_TYPE (GtdPluginManager, gtd_plugin_manager, GTD_TYPE_OBJECT)

enum
{
  PLUGIN_LOADED,
  PLUGIN_UNLOADED,
  NUM_SIGNALS
};

static guint signals[NUM_SIGNALS] = { 0, };

static const gchar * const default_plugins[] = {
  "all-tasks-panel",
  "eds",
  "welcome",
  "task-lists-workspace",
  "today-panel",
  "inbox-panel",
  "next-week-panel",
  "night-light",
  "peace",
};

static gboolean
gtd_str_equal0 (gconstpointer a,
                gconstpointer b)
{
  if (a == b)
    return TRUE;
  else if (!a || !b)
    return FALSE;
  else
    return g_str_equal (a, b);
}

static gchar**
get_loaded_extensions (const gchar **extensions)
{
  g_autoptr (GPtrArray) loaded_plugins = NULL;
  gsize i;

  loaded_plugins = g_ptr_array_new ();

  for (i = 0; extensions && extensions[i]; i++)
    g_ptr_array_add (loaded_plugins, g_strdup (extensions[i]));

  for (i = 0; i < G_N_ELEMENTS (default_plugins); i++)
    {
      if (g_ptr_array_find_with_equal_func (loaded_plugins,
                                            default_plugins[i],
                                            gtd_str_equal0,
                                            NULL))
        {
          continue;
        }

      g_ptr_array_add (loaded_plugins, g_strdup (default_plugins[i]));
    }

  g_ptr_array_add (loaded_plugins, NULL);

  return (gchar**) g_ptr_array_free (g_steal_pointer (&loaded_plugins), FALSE);
}

static gboolean
from_gsetting_to_property_func (GValue   *value,
                                GVariant *variant,
                                gpointer  user_data)
{
  g_autofree const gchar **extensions = NULL;
  g_autofree gchar **loaded_extensions = NULL;

  extensions = g_variant_get_strv (variant, NULL);
  loaded_extensions = get_loaded_extensions (extensions);

  g_value_take_boxed (value, g_steal_pointer (&loaded_extensions));

  return TRUE;
}

static GVariant*
from_property_to_gsetting_func (const GValue       *value,
                                const GVariantType *expected_type,
                                gpointer            user_data)
{
  g_autofree gchar **loaded_extensions = NULL;
  const gchar **extensions = NULL;

  extensions = g_value_get_boxed (value);
  loaded_extensions = get_loaded_extensions (extensions);

  return g_variant_new_strv ((const gchar * const *)loaded_extensions, -1);
}

static void
on_plugin_unloaded_cb (PeasEngine       *engine,
                       PeasPluginInfo   *info,
                       GtdPluginManager *self)
{
  GtdActivatable *activatable;
  const gchar *data_dir;

  data_dir = peas_plugin_info_get_data_dir (info);

  if (g_str_has_prefix (data_dir, "resource://"))
    gtd_theme_manager_remove_resources (self->theme_manager, data_dir);

  activatable = g_hash_table_lookup (self->info_to_extension, info);

  if (!activatable)
    return;

  /* Deactivates the extension */
  gtd_activatable_deactivate (activatable);

  /* Emit the signal */
  g_signal_emit (self, signals[PLUGIN_UNLOADED], 0, info, activatable);

  g_hash_table_remove (self->info_to_extension, info);

  /* Destroy the extension */
  g_clear_object (&activatable);
}

static void
on_plugin_loaded_cb (PeasEngine       *engine,
                     PeasPluginInfo   *info,
                     GtdPluginManager *self)
{
  const gchar *data_dir;

  data_dir = peas_plugin_info_get_data_dir (info);

  if (g_str_has_prefix (data_dir, "resource://"))
    gtd_theme_manager_add_resources (self->theme_manager, data_dir);

  if (peas_engine_provides_extension (engine, info, GTD_TYPE_ACTIVATABLE))
    {
      GtdActivatable *activatable;
      PeasExtension *extension;

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
  g_signal_connect_after (engine, "load-plugin", G_CALLBACK (on_plugin_loaded_cb), self);
  g_signal_connect (engine, "unload-plugin",G_CALLBACK (on_plugin_unloaded_cb), self);
}

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
}

static void
gtd_plugin_manager_init (GtdPluginManager *self)
{
  self->info_to_extension = g_hash_table_new (g_direct_hash, g_direct_equal);
  self->theme_manager = gtd_theme_manager_new ();

  gtd_object_push_loading (GTD_OBJECT (self));

  setup_engine (self);

  gtd_object_pop_loading (GTD_OBJECT (self));
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

  g_settings_bind_with_mapping (settings,
                                "active-extensions",
                                engine,
                                "loaded-plugins",
                                G_SETTINGS_BIND_DEFAULT,
                                from_gsetting_to_property_func,
                                from_property_to_gsetting_func,
                                self,
                                NULL);
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
