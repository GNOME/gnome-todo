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

#include "gtd-debug.h"
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

static void          on_source_changed_cb                        (GtkWidget          *preference_panel,
                                                                  GtdPluginTodoTxt   *self);

static void          gtd_activatable_iface_init                  (GtdActivatableInterface  *iface);

G_DEFINE_DYNAMIC_TYPE_EXTENDED (GtdPluginTodoTxt, gtd_plugin_todo_txt, PEAS_TYPE_EXTENSION_BASE, 0,
                                G_IMPLEMENT_INTERFACE_DYNAMIC (GTD_TYPE_ACTIVATABLE, gtd_activatable_iface_init))


/*
 * Auxiliary methods
 */

static gboolean
set_default_source (GtdPluginTodoTxt *self)
{
  g_autofree gchar *default_file = NULL;
  g_autoptr (GError) error = NULL;

  GTD_ENTRY;

  default_file = g_build_filename (g_get_user_special_dir (G_USER_DIRECTORY_DOCUMENTS), "todo.txt", NULL);
  self->source_file = g_file_new_for_path (default_file);

  if (g_file_query_exists (self->source_file, NULL))
    GTD_RETURN (TRUE);

  g_file_create (self->source_file, G_FILE_CREATE_NONE, NULL, &error);

  if (error)
    {
      gtd_manager_emit_error_message (gtd_manager_get_default (),
                                      _("Cannot create Todo.txt file"),
                                      error->message,
                                      NULL,
                                      NULL);
      GTD_RETURN (FALSE);
    }

  GTD_RETURN (TRUE);
}

static gboolean
setup_source (GtdPluginTodoTxt *self)
{
  g_autoptr (GError) error = NULL;
  g_autofree gchar *source = NULL;

  GTD_ENTRY;

  source = g_settings_get_string (self->settings, "file");

  if (!source || source[0] == '\0')
    {
      if (!set_default_source (self))
        GTD_RETURN (FALSE);
    }
  else
    {
      self->source_file = g_file_new_for_path (source);
    }

  if (!g_file_query_exists (self->source_file, NULL))
    {
      g_file_create (self->source_file, G_FILE_CREATE_NONE, NULL, &error);

      if (error)
        {
          gtd_manager_emit_error_message (gtd_manager_get_default (),
                                          _("Cannot create Todo.txt file"),
                                          error->message,
                                          NULL,
                                          NULL);
          GTD_RETURN (FALSE);
        }
    }

  GTD_RETURN (TRUE);
}

static void
setup_preferences_panel (GtdPluginTodoTxt *self)
{
  GtdProviderTodoTxt *provider;
  g_autofree gchar *path = NULL;
  GtkWidget *label;
  GtkWidget *box;
  gboolean set;

  set = setup_source (self);
  self->providers = NULL;

  if (set)
    {
      provider = gtd_provider_todo_txt_new (self->source_file);
      self->providers = g_list_append (self->providers, provider);
    }

  /* Preferences */
  box = g_object_new (GTK_TYPE_BOX,
                      "margin", 18,
                      "spacing", 12,
                      "expand", TRUE,
                      "orientation", GTK_ORIENTATION_VERTICAL,
                      NULL);

  label = gtk_label_new (_("Select a Todo.txt-formatted file:"));
  gtk_container_add (GTK_CONTAINER (box), label);

  /* Filechooser */
  self->preferences = gtk_file_chooser_button_new (_("Select a file"), GTK_FILE_CHOOSER_ACTION_OPEN);
  gtk_widget_set_size_request (GTK_WIDGET (box), 300, 0);
  gtk_widget_set_halign (GTK_WIDGET (box), GTK_ALIGN_CENTER);
  gtk_widget_set_valign (GTK_WIDGET (box), GTK_ALIGN_CENTER);

  gtk_container_add (GTK_CONTAINER (box), self->preferences);

  /* If there's a file set, select it */
  path = g_settings_get_string (self->settings, "file");

  if (path && *path)
    {
      g_autoptr (GError) error = NULL;
      g_autoptr (GFile) file = NULL;

      g_debug ("Selecting Todo.txt file %s", path);

      file = g_file_new_for_path (path);

      gtk_file_chooser_set_file (GTK_FILE_CHOOSER (self->preferences), file, &error);

      if (error)
        {
          g_warning ("Error selecting Todo.txt file (%s): %s", path, error->message);

          gtd_manager_emit_error_message (gtd_manager_get_default (),
                                          _("Error opening Todo.txt file"),
                                          error->message,
                                          NULL,
                                          NULL);
        }
    }

  /* Big warning label reminding the user that this is experimental */
  label = gtk_label_new ("");
  gtk_label_set_markup (GTK_LABEL (label),
                        _("<b>Warning!</b> Todo.txt support is experimental and unstable. "
                          "You may experience instability, errors and eventually data loss. "
                          "It is not recommended to use Todo.txt integration on production systems."));
  gtk_label_set_max_width_chars (GTK_LABEL (label), 60);
  gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_label_set_lines (GTK_LABEL (label), 3);
  gtk_widget_set_margin_top (label, 18);

  gtk_container_add (GTK_CONTAINER (box), label);

  /* Store the box, and report it as the preferences panel itself */
  self->preferences_box = box;

  g_signal_connect (self->preferences, "file-set", G_CALLBACK (on_source_changed_cb), self);
}


/*
 * Callbacks
 */

static void
on_source_changed_finished_cb (GtdPluginTodoTxt *self)
{
  GtdProviderTodoTxt *provider;
  gboolean set;

  GTD_ENTRY;

  set = setup_source (self);

  if (!set)
    GTD_RETURN ();

  provider = gtd_provider_todo_txt_new (self->source_file);
  self->providers = g_list_append (self->providers, provider);

  g_signal_emit_by_name (self, "provider-added", provider);

  GTD_EXIT;
}

static void
on_source_changed_cb (GtkWidget        *preference_panel,
                      GtdPluginTodoTxt *self)
{
  GtdProviderTodoTxt *provider;

  GTD_ENTRY;

  g_clear_object (&self->source_file);

  g_settings_set_string (self->settings,
                        "file",
                         gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (self->preferences)));

  if (self->providers)
    {
      provider = self->providers->data;

      g_list_free_full (self->providers, g_object_unref);
      self->providers = NULL;

      g_signal_emit_by_name (self, "provider-removed", provider);
    }

  on_source_changed_finished_cb (self);

  GTD_EXIT;
}


/*
 * GtdActivatable implementation
 */

static void
gtd_plugin_todo_txt_activate (GtdActivatable *activatable)
{
}

static void
gtd_plugin_todo_txt_deactivate (GtdActivatable *activatable)
{
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
 * GObject overrides
 */

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

  object_class->finalize = gtd_plugin_todo_txt_finalize;
  object_class->get_property = gtd_plugin_todo_txt_get_property;

  g_object_class_override_property (object_class, PROP_PREFERENCES_PANEL, "preferences-panel");
}

static void
gtd_plugin_todo_txt_init (GtdPluginTodoTxt *self)
{
  self->settings = g_settings_new ("org.gnome.todo.plugins.todo-txt");

  setup_preferences_panel (self);
}

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
