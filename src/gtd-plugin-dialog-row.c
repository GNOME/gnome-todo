/* gtd-plugin-dialog-row.c
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

#include "gtd-activatable.h"
#include "gtd-plugin-dialog-row.h"

#include <glib/gi18n.h>

struct _GtdPluginDialogRow
{
  GtkListBoxRow       parent;

  GtkWidget          *description_label;
  GtkWidget          *error_image;
  GtkWidget          *icon_image;
  GtkWidget          *loaded_switch;
  GtkWidget          *name_label;
  GtkWidget          *preferences_button;

  PeasPluginInfo     *info;
  GtdActivatable     *plugin;
};

G_DEFINE_TYPE (GtdPluginDialogRow, gtd_plugin_dialog_row, GTK_TYPE_LIST_BOX_ROW)

enum
{
  SHOW_PREFERENCES,
  NUM_SIGNALS
};

enum
{
  PROP_0,
  PROP_INFO,
  PROP_PLUGIN,
  N_PROPS
};

static guint signals[NUM_SIGNALS] = { 0, };

static void
preferences_button_clicked (GtdPluginDialogRow *row)
{
  g_signal_emit (row,
                 signals[SHOW_PREFERENCES],
                 0,
                 row->info,
                 row->plugin);
}

static void
loaded_switch_changed (GtdPluginDialogRow *row)
{
  PeasEngine *engine;
  gboolean switch_active;
  gboolean success;

  engine = peas_engine_get_default ();
  switch_active = gtk_switch_get_active (GTK_SWITCH (row->loaded_switch));

  /* Load or unload the plugin */
  if (switch_active)
    success = peas_engine_load_plugin (engine, row->info);
  else
    success = peas_engine_unload_plugin (engine, row->info);

  /* If we failed to load or unload the plugin, the following things
   * must happen:
   * 1. the switch goes back to it's original state
   * 2. the row gets insensitive
   * 3. an icon is shown
   */
  gtk_widget_set_visible (row->error_image, !success);
  gtk_widget_set_sensitive (GTK_WIDGET (row), success);

  if (!success)
    {
      gtk_widget_set_tooltip_text (row->error_image,
                                   switch_active ? _("Error loading plugin") : _("Error unloading plugin"));

      /* Return the switch to it's original state, without
       * hearing the notify::active signal.
       */
      g_signal_handlers_block_by_func (row->loaded_switch,
                                       loaded_switch_changed,
                                       row);

      gtk_switch_set_active (GTK_SWITCH (row->loaded_switch), !switch_active);

      g_signal_handlers_unblock_by_func (row->loaded_switch,
                                         loaded_switch_changed,
                                         row);
    }
}

static void
gtd_plugin_dialog_row_finalize (GObject *object)
{
  GtdPluginDialogRow *self = (GtdPluginDialogRow *)object;

  g_clear_object (&self->plugin);

  G_OBJECT_CLASS (gtd_plugin_dialog_row_parent_class)->finalize (object);
}

static void
gtd_plugin_dialog_row_constructed (GObject *object)
{
  GtdPluginDialogRow *self = GTD_PLUGIN_DIALOG_ROW (object);

  G_OBJECT_CLASS (gtd_plugin_dialog_row_parent_class)->constructed (object);

  gtk_label_set_label (GTK_LABEL (self->name_label), peas_plugin_info_get_name (self->info));
  gtk_label_set_label (GTK_LABEL (self->description_label), peas_plugin_info_get_description (self->info));

  gtk_switch_set_active (GTK_SWITCH (self->loaded_switch), self->plugin != NULL);

  gtk_image_set_from_icon_name (GTK_IMAGE (self->icon_image),
                                peas_plugin_info_get_icon_name (self->info),
                                GTK_ICON_SIZE_DND);
}

static void
gtd_plugin_dialog_row_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  GtdPluginDialogRow *self = GTD_PLUGIN_DIALOG_ROW (object);

  switch (prop_id)
    {
    case PROP_INFO:
      g_value_set_boxed (value, self->info);
      break;

    case PROP_PLUGIN:
      g_value_set_object (value, self->plugin);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_plugin_dialog_row_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  GtdPluginDialogRow *self = GTD_PLUGIN_DIALOG_ROW (object);

  switch (prop_id)
    {
    case PROP_INFO:
      self->info = g_value_get_boxed (value);
      g_object_notify (object, "info");
      break;

    case PROP_PLUGIN:
      gtd_plugin_dialog_row_set_plugin (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_plugin_dialog_row_class_init (GtdPluginDialogRowClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gtd_plugin_dialog_row_finalize;
  object_class->constructed = gtd_plugin_dialog_row_constructed;
  object_class->get_property = gtd_plugin_dialog_row_get_property;
  object_class->set_property = gtd_plugin_dialog_row_set_property;

  g_object_class_install_property (object_class,
                                   PROP_INFO,
                                   g_param_spec_boxed ("info",
                                                       "Information about the plugin",
                                                       "The information about the plugin",
                                                       PEAS_TYPE_PLUGIN_INFO,
                                                       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

  g_object_class_install_property (object_class,
                                   PROP_PLUGIN,
                                   g_param_spec_object ("plugin",
                                                        "Plugin",
                                                        "The plugin this row implements",
                                                        GTD_TYPE_ACTIVATABLE,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  signals[SHOW_PREFERENCES] = g_signal_new ("show-preferences",
                                            GTD_TYPE_PLUGIN_DIALOG_ROW,
                                            G_SIGNAL_RUN_FIRST,
                                            0,
                                            NULL,
                                            NULL,
                                            NULL,
                                            G_TYPE_NONE,
                                            2,
                                            PEAS_TYPE_PLUGIN_INFO,
                                            GTD_TYPE_ACTIVATABLE);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/todo/ui/plugin-row.ui");

  gtk_widget_class_bind_template_child (widget_class, GtdPluginDialogRow, description_label);
  gtk_widget_class_bind_template_child (widget_class, GtdPluginDialogRow, error_image);
  gtk_widget_class_bind_template_child (widget_class, GtdPluginDialogRow, icon_image);
  gtk_widget_class_bind_template_child (widget_class, GtdPluginDialogRow, loaded_switch);
  gtk_widget_class_bind_template_child (widget_class, GtdPluginDialogRow, name_label);
  gtk_widget_class_bind_template_child (widget_class, GtdPluginDialogRow, preferences_button);

  gtk_widget_class_bind_template_callback (widget_class, loaded_switch_changed);
  gtk_widget_class_bind_template_callback (widget_class, preferences_button_clicked);
}

static void
gtd_plugin_dialog_row_init (GtdPluginDialogRow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget*
gtd_plugin_dialog_row_new (PeasPluginInfo *info,
                           GtdActivatable *activatable)
{
  return g_object_new (GTD_TYPE_PLUGIN_DIALOG_ROW,
                       "info", info,
                       "plugin", activatable,
                       NULL);
}

PeasPluginInfo*
gtd_plugin_dialog_row_get_info (GtdPluginDialogRow *row)
{
  g_return_val_if_fail (GTD_IS_PLUGIN_DIALOG_ROW (row), NULL);

  return row->info;
}

GtdActivatable*
gtd_plugin_dialog_row_get_plugin (GtdPluginDialogRow *row)
{
  g_return_val_if_fail (GTD_IS_PLUGIN_DIALOG_ROW (row), NULL);

  return row->plugin;
}

void
gtd_plugin_dialog_row_set_plugin (GtdPluginDialogRow *row,
                                  GtdActivatable     *activatable)
{
  g_return_if_fail (GTD_IS_PLUGIN_DIALOG_ROW (row));

  if (g_set_object (&row->plugin, activatable))
    {
      gboolean show_preferences;

      show_preferences = activatable != NULL &&
                         gtd_activatable_get_preferences_panel (activatable) != NULL;

      gtk_widget_set_visible (row->preferences_button, show_preferences);

      /* Setup the switch and make sure we don't fire notify::active */
      g_signal_handlers_block_by_func (row->loaded_switch,
                                       loaded_switch_changed,
                                       row);

      gtk_switch_set_active (GTK_SWITCH (row->loaded_switch), activatable != NULL);

      g_signal_handlers_unblock_by_func (row->loaded_switch,
                                         loaded_switch_changed,
                                         row);

      g_object_notify (G_OBJECT (row), "plugin");
    }
}
