/* gtd-plugin-todo-txt.c
 *
 * Copyright (C) 2016 Rohit Kaushik <kaushikrohit325@gmail.com>
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

#define G_LOG_DOMAIN "GtdPluginTodoTxt"

#include "gtd-plugin-todo-txt.h"
#include "gtd-provider-todo-txt.h"

#include <glib/gi18n.h>
#include <glib-object.h>

/**
 * The #GtdPluginTodoTxt is a class that loads Todo.txt
 * provider of GNOME To Do.
 */

struct _GtdPluginTodoTxt
{
  PeasExtensionBase   parent;

  GFile              *source_file;

  GSettings          *settings;

  GtkWidget          *preferences_box;
  GtkWidget          *preferences;

  /* Providers */
  GList              *providers;
};

enum
{
  PROP_0,
  PROP_PREFERENCES_PANEL,
  LAST_PROP
};

static void          gtd_activatable_iface_init                  (GtdActivatableInterface  *iface);

G_DEFINE_DYNAMIC_TYPE_EXTENDED (GtdPluginTodoTxt, gtd_plugin_todo_txt, PEAS_TYPE_EXTENSION_BASE,
                                0,
                                G_IMPLEMENT_INTERFACE_DYNAMIC (GTD_TYPE_ACTIVATABLE,
                                                               gtd_activatable_iface_init))

/*
 * GtdActivatable interface implementation
 */
static void
gtd_plugin_todo_txt_activate (GtdActivatable *activatable)
{
  ;
}

static void
gtd_plugin_todo_txt_deactivate (GtdActivatable *activatable)
{
  ;
}

static GList*
gtd_plugin_todo_txt_get_header_widgets (GtdActivatable *activatable)
{
  return NULL;
}

static GtkWidget*
gtd_plugin_todo_txt_get_preferences_panel (GtdActivatable *activatable)
{
  GtdPluginTodoTxt *plugin = GTD_PLUGIN_TODO_TXT (activatable);

  return plugin->preferences_box;

}

static GList*
gtd_plugin_todo_txt_get_panels (GtdActivatable *activatable)
{
  return NULL;
}

static GList*
gtd_plugin_todo_txt_get_providers (GtdActivatable *activatable)
{
  GtdPluginTodoTxt *plugin = GTD_PLUGIN_TODO_TXT (activatable);
  return plugin->providers;
}

static void
gtd_activatable_iface_init (GtdActivatableInterface *iface)
{
  iface->activate = gtd_plugin_todo_txt_activate;
  iface->deactivate = gtd_plugin_todo_txt_deactivate;
  iface->get_header_widgets = gtd_plugin_todo_txt_get_header_widgets;
  iface->get_preferences_panel = gtd_plugin_todo_txt_get_preferences_panel;
  iface->get_panels = gtd_plugin_todo_txt_get_panels;
  iface->get_providers = gtd_plugin_todo_txt_get_providers;
}

/*
 * Init
 */

static gboolean
gtd_plugin_todo_txt_set_default_source (GtdPluginTodoTxt *self)
{
  g_autofree gchar *default_file;
  GError *error;

  error = NULL;
  default_file = g_build_filename (g_get_user_special_dir (G_USER_DIRECTORY_DOCUMENTS),
                                   "todo.txt",
                                   NULL);
  self->source_file = g_file_new_for_path (default_file);

  if (g_file_query_exists (self->source_file, NULL))
    return TRUE;

  g_file_create (self->source_file,
                     G_FILE_CREATE_NONE,
                     NULL,
                     &error);

  if (error)
    {
      gtd_manager_emit_error_message (gtd_manager_get_default (),
                                      _("Cannot create Todo.txt file"),
                                      error->message,
                                      NULL,
                                      NULL);

      g_clear_error (&error);
      return FALSE;
    }

  return TRUE;
}

static gboolean
gtd_plugin_todo_txt_set_source (GtdPluginTodoTxt *self)
{
  GError *error;
  gchar  *source;

  error = NULL;
  source = g_settings_get_string (self->settings, "file");

  if (!source || source[0] == '\0')
    {
      if (!gtd_plugin_todo_txt_set_default_source (self))
        return FALSE;
    }
  else
    {
      self->source_file = g_file_new_for_uri (source);
    }

  if (!g_file_query_exists (self->source_file, NULL))
    {
      g_file_create (self->source_file,
                     G_FILE_CREATE_NONE,
                     NULL,
                     &error);

      if (error)
        {
          gtd_manager_emit_error_message (gtd_manager_get_default (),
                                          _("Cannot create Todo.txt file"),
                                          error->message,
                                          NULL,
                                          NULL);

          g_clear_error (&error);
          return FALSE;
        }
    }

  return TRUE;
}

static void
gtd_plugin_todo_txt_source_changed_finished_cb (GtdPluginTodoTxt *self)
{
  GtdProviderTodoTxt *provider;
  gboolean set;

  set = gtd_plugin_todo_txt_set_source (self);

  if (!set)
    return;

  provider = gtd_provider_todo_txt_new (self->source_file);
  self->providers = g_list_append (self->providers, provider);

  g_signal_emit_by_name (self, "provider-added", provider);
}

static void
gtd_plugin_todo_txt_source_changed_cb (GtkWidget *preference_panel,
                                       gpointer   user_data)
{
  GtdPluginTodoTxt *self;
  GtdProviderTodoTxt *provider;

  self = GTD_PLUGIN_TODO_TXT (user_data);

  g_clear_object (&self->source_file);

  g_settings_set_string (self->settings,
                        "file",
                         gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (self->preferences)));

  if (self->providers)
    {
      provider = self->providers->data;

      g_list_free_full (self->providers, g_object_unref);
      self->providers = NULL;

      g_signal_emit_by_name (self, "provider-removed", provider);
    }

  gtd_plugin_todo_txt_source_changed_finished_cb (self);
}

static void
gtd_plugin_todo_txt_finalize (GObject *object)
{
  GtdPluginTodoTxt *self = (GtdPluginTodoTxt *) object;

  g_list_free_full (self->providers, g_object_unref);
  self->providers = NULL;

  G_OBJECT_CLASS (gtd_plugin_todo_txt_parent_class)->finalize (object);
}

static void
gtd_plugin_todo_txt_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  GtdPluginTodoTxt *self = GTD_PLUGIN_TODO_TXT (object);
  switch (prop_id)
    {
    case PROP_PREFERENCES_PANEL:
      g_value_set_object (value, self->preferences);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_plugin_todo_txt_class_init (GtdPluginTodoTxtClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize     = gtd_plugin_todo_txt_finalize;
  object_class->get_property = gtd_plugin_todo_txt_get_property;

  g_object_class_override_property (object_class,
                                    PROP_PREFERENCES_PANEL,
                                    "preferences-panel");
}

static void
gtd_plugin_todo_txt_init (GtdPluginTodoTxt *self)
{
  GtdProviderTodoTxt *provider;
  GtkWidget *label;
  gboolean   set;

  self->settings = g_settings_new ("org.gnome.todo.plugins.todo-txt");
  set = gtd_plugin_todo_txt_set_source (self);
  self->providers = NULL;

  if (set)
    {
      provider = gtd_provider_todo_txt_new (self->source_file);
      self->providers = g_list_append (self->providers, provider);
    }

  /* Preferences */
  self->preferences_box = g_object_new (GTK_TYPE_BOX,
                                        "margin", 18,
                                        "spacing", 12,
                                        "expand", TRUE,
                                        "orientation", GTK_ORIENTATION_VERTICAL,
                                        NULL);
  label = gtk_label_new (_("Select a Todo.txt-formatted file:"));
  self->preferences = gtk_file_chooser_button_new (_("Select a file"), GTK_FILE_CHOOSER_ACTION_OPEN);

  gtk_widget_set_size_request (GTK_WIDGET (self->preferences_box), 300, 0);

  gtk_container_add (GTK_CONTAINER (self->preferences_box), label);
  gtk_container_add (GTK_CONTAINER (self->preferences_box), self->preferences);

  gtk_widget_set_halign (GTK_WIDGET (self->preferences_box), GTK_ALIGN_CENTER);
  gtk_widget_set_valign (GTK_WIDGET (self->preferences_box), GTK_ALIGN_CENTER);

  gtk_widget_show_all (self->preferences_box);

  g_signal_connect (self->preferences,
                    "file-set",
                    G_CALLBACK (gtd_plugin_todo_txt_source_changed_cb),
                    self);
}

/* Empty class_finalize method */
static void
gtd_plugin_todo_txt_class_finalize (GtdPluginTodoTxtClass *klass)
{
}

G_MODULE_EXPORT void
gtd_plugin_todo_txt_register_types (PeasObjectModule *module)
{
  gtd_plugin_todo_txt_register_type (G_TYPE_MODULE (module));

  peas_object_module_register_extension_type (module,
                                              GTD_TYPE_ACTIVATABLE,
                                              GTD_TYPE_PLUGIN_TODO_TXT);
}
