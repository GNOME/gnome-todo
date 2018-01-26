/* gtd-plugin-dialog.c
 *
 * Copyright (C) 2016 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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

#define G_LOG_DOMAIN "GtdPluginDialog"

#include "interfaces/gtd-activatable.h"
#include "gtd-manager.h"
#include "gtd-manager-protected.h"
#include "gtd-plugin-manager.h"
#include "gtd-plugin-dialog.h"
#include "gtd-plugin-dialog-row.h"

#include <libpeas/peas.h>

struct _GtdPluginDialog
{
  GtkDialog           parent;

  GtkWidget          *back_button;
  GtkWidget          *extension_list_placeholder;
  GtkWidget          *frame;
  GtkWidget          *listbox;
  GtkWidget          *stack;
};

G_DEFINE_TYPE (GtdPluginDialog, gtd_plugin_dialog, GTK_TYPE_DIALOG)

static void
back_button_clicked (GtkWidget       *button,
                     GtdPluginDialog *self)
{
  gtk_stack_set_transition_type (GTK_STACK (self->stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
  gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "list");
  gtk_widget_hide (button);
}

static gboolean
dialog_deleted (GtdPluginDialog *self,
                GdkEvent        *event,
                gpointer         data)
{
  gtk_stack_set_transition_type (GTK_STACK (self->stack), GTK_STACK_TRANSITION_TYPE_NONE);
  gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "list");
  gtk_widget_hide (self->back_button);
  gtk_widget_hide (GTK_WIDGET (self));

  return GDK_EVENT_STOP;
}

static void
show_preferences_cb (GtdPluginDialogRow *row,
                     PeasPluginInfo     *info,
                     GtdActivatable     *plugin,
                     GtdPluginDialog    *self)
{
  GtdActivatable *activatable;
  GtkWidget *old_panel;
  GtkWidget *panel;

  /* First, remove the old panel */
  old_panel = gtk_bin_get_child (GTK_BIN (self->frame));

  if (old_panel)
    {
      g_object_ref (old_panel);
      gtk_container_remove (GTK_CONTAINER (self->frame), old_panel);
    }

  /* Second, setup the new panel */
  activatable = gtd_plugin_dialog_row_get_plugin (row);
  panel = gtd_activatable_get_preferences_panel (activatable);

  if (panel)
    {
      gtk_container_add (GTK_CONTAINER (self->frame), panel);
      gtk_widget_show (panel);
    }

  /* Last, go to the preferences page */
  gtk_stack_set_transition_type (GTK_STACK (self->stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
  gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "config");
  gtk_widget_show (self->back_button);
}

static void
add_plugin (GtdPluginDialog *dialog,
            PeasPluginInfo *info,
            GtdActivatable *activatable)
{
  GtkWidget *row;

  if (peas_plugin_info_is_hidden (info))
    return;

  /* Create a row for the plugin */
  row = gtd_plugin_dialog_row_new (info, activatable);

  g_signal_connect (row,
                    "show-preferences",
                    G_CALLBACK (show_preferences_cb),
                    dialog);

  gtk_container_add (GTK_CONTAINER (dialog->listbox), row);
}

static void
plugin_loaded (GtdPluginManager *manager,
               PeasPluginInfo   *info,
               GtdActivatable   *activatable,
               GtdPluginDialog  *self)
{
  gboolean contains_plugin;
  GList *children;
  GList *l;

  contains_plugin = FALSE;
  children = gtk_container_get_children (GTK_CONTAINER (self->listbox));

  for (l = children; l != NULL; l = l->next)
    {
      GtdPluginDialogRow *row = l->data;

      if (!GTD_IS_PLUGIN_DIALOG_ROW (row) || gtd_plugin_dialog_row_get_info (row) != info)
        continue;

      gtd_plugin_dialog_row_set_plugin (l->data, activatable);
      contains_plugin = TRUE;
      break;
    }

  g_list_free (children);

  /* If we just loaded a plugin that is not yet added
   * to the plugin list, we shall do it now.
   */
  if (!contains_plugin)
    {
      add_plugin (self,
                  info,
                  activatable);
    }
}

static void
plugin_unloaded (GtdPluginManager *manager,
                 PeasPluginInfo   *info,
                 GtdActivatable   *activatable,
                 GtdPluginDialog  *self)
{
  GList *children;
  GList *l;

  children = gtk_container_get_children (GTK_CONTAINER (self->listbox));

  for (l = children; l != NULL; l = l->next)
    {
      GtdPluginDialogRow *row = l->data;

      if (!GTD_IS_PLUGIN_DIALOG_ROW (row) || gtd_plugin_dialog_row_get_info (row) != info)
        continue;

      gtd_plugin_dialog_row_set_plugin (l->data, NULL);
      break;
    }

  g_list_free (children);
}

static gint
sort_extensions (GtdPluginDialogRow *row1,
                 GtdPluginDialogRow *row2,
                 gpointer            user_data)
{
  PeasPluginInfo *info1, *info2;

  info1 = gtd_plugin_dialog_row_get_info (row1);
  info2 = gtd_plugin_dialog_row_get_info (row2);

  return g_strcmp0 (peas_plugin_info_get_name (info1), peas_plugin_info_get_name (info2));
}

static void
gtd_plugin_dialog_class_init (GtdPluginDialogClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/todo/ui/plugin-dialog.ui");

  gtk_widget_class_bind_template_child (widget_class, GtdPluginDialog, back_button);
  gtk_widget_class_bind_template_child (widget_class, GtdPluginDialog, extension_list_placeholder);
  gtk_widget_class_bind_template_child (widget_class, GtdPluginDialog, frame);
  gtk_widget_class_bind_template_child (widget_class, GtdPluginDialog, listbox);
  gtk_widget_class_bind_template_child (widget_class, GtdPluginDialog, stack);

  gtk_widget_class_bind_template_callback (widget_class, back_button_clicked);
  gtk_widget_class_bind_template_callback (widget_class, dialog_deleted);
}

static void
gtd_plugin_dialog_init (GtdPluginDialog *self)
{
  GtdPluginManager *plugin_manager;
  GtdManager *manager;
  PeasEngine *engine;
  const GList *plugin;

  engine = peas_engine_get_default ();
  manager = gtd_manager_get_default ();
  plugin_manager = gtd_manager_get_plugin_manager (manager);

  gtk_widget_init_template (GTK_WIDGET (self));

  /* Add discovered plugins to the list */
  for (plugin = peas_engine_get_plugin_list (engine);
       plugin != NULL;
       plugin = plugin->next)
    {
      GtdActivatable *activatable;

      activatable = gtd_plugin_manager_get_plugin (plugin_manager, plugin->data);

      add_plugin (self,
                  plugin->data,
                  activatable);
    }

  /* Connect GtdPluginManager signals */
  g_signal_connect (plugin_manager,
                    "plugin-loaded",
                    G_CALLBACK (plugin_loaded),
                    self);

  g_signal_connect (plugin_manager,
                    "plugin-unloaded",
                    G_CALLBACK (plugin_unloaded),
                    self);

  /* Sort extensions by their display name */
  gtk_list_box_set_sort_func (GTK_LIST_BOX (self->listbox),
                              (GtkListBoxSortFunc) sort_extensions,
                              NULL,
                              NULL);

  gtk_list_box_set_placeholder (GTK_LIST_BOX (self->listbox), self->extension_list_placeholder);
}

GtkWidget*
gtd_plugin_dialog_new (void)
{
  return g_object_new (GTD_TYPE_PLUGIN_DIALOG,
                       "use-header-bar", 1,
                       NULL);
}
