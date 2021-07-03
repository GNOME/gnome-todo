/* gtd-task-list-panel.c
 *
 * Copyright 2018-2020 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "GtdTaskListPanel"

#include <glib/gi18n.h>

#include "gtd-color-button.h"
#include "gtd-debug.h"
#include "gtd-manager.h"
#include "gtd-panel.h"
#include "gtd-provider.h"
#include "gtd-task-list.h"
#include "gtd-task-list-panel.h"
#include "gtd-task-list-view.h"
#include "gtd-utils.h"

struct _GtdTaskListPanel
{
  GtkBox              parent;

  GtkButton          *archive_button;
  GtkFlowBox         *colors_flowbox;
  GtkPopover         *popover;
  GtkStack           *popover_stack;
  GtkWidget          *rename_button;
  GtkEditable        *rename_entry;
  GtdTaskListView    *task_list_view;

  GtkWidget          *previous_color_button;
};


static void          on_colors_flowbox_child_activated_cb        (GtkFlowBox         *colors_flowbox,
                                                                  GtkFlowBoxChild    *child,
                                                                  GtdTaskListPanel   *self);

static void          on_task_list_updated_cb                     (GObject           *source,
                                                                  GAsyncResult      *result,
                                                                  gpointer           user_data);

static void          gtd_panel_iface_init                        (GtdPanelInterface  *iface);


G_DEFINE_TYPE_WITH_CODE (GtdTaskListPanel, gtd_task_list_panel, GTK_TYPE_BOX,
                         G_IMPLEMENT_INTERFACE (GTD_TYPE_PANEL, gtd_panel_iface_init))

enum
{
  PROP_0,
  PROP_ICON,
  PROP_MENU,
  PROP_NAME,
  PROP_PRIORITY,
  PROP_SUBTITLE,
  PROP_TITLE,
  N_PROPS
};

enum
{
  LIST_DELETED,
  N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0, };

/*
 * Auxilary methods
 */

static const gchar * const colors[] =
{
  "#3584e4",
  "#33d17a",
  "#f6d32d",
  "#ff7800",
  "#e01b24",
  "#9141ac",
  "#986a44",
  "#3d3846",
  "#ffffff",
};

static void
populate_color_grid (GtdTaskListPanel *self)
{
  guint i;

  for (i = 0; i < G_N_ELEMENTS (colors); i++)
    {
      GtkWidget *button;
      GdkRGBA color;

      gdk_rgba_parse (&color, colors[i]);

      button = gtd_color_button_new (&color);
      gtk_widget_set_size_request (button, -1, 24);

      gtk_flow_box_insert (self->colors_flowbox, button, -1);
    }
}

static void
update_selected_color (GtdTaskListPanel *self)
{
  g_autoptr (GdkRGBA) color = NULL;
  GtdTaskList *list;
  GtkWidget *button;
  guint i;

  list = GTD_TASK_LIST (gtd_task_list_view_get_model (self->task_list_view));
  color = gtd_task_list_get_color (list);
  button = NULL;

  for (i = 0; i < G_N_ELEMENTS (colors); i++)
    {
      GdkRGBA c;

      gdk_rgba_parse (&c, colors[i]);

      if (gdk_rgba_equal (&c, color))
        {
          button = GTK_WIDGET (gtk_flow_box_get_child_at_index (self->colors_flowbox, i));
          break;
        }
    }

  if (button)
    {
      g_signal_handlers_block_by_func (button, on_colors_flowbox_child_activated_cb, self);
      g_signal_emit_by_name (button, "activate");
      g_signal_handlers_unblock_by_func (button, on_colors_flowbox_child_activated_cb, self);
    }
  else if (self->previous_color_button)
    {
      gtk_widget_unset_state_flags (self->previous_color_button, GTK_STATE_FLAG_SELECTED);
      self->previous_color_button = NULL;
    }
}

static void
rename_list (GtdTaskListPanel *self)
{
  g_autofree gchar *new_name = NULL;
  GtdTaskList *list;

  g_assert (gtk_widget_get_visible (GTK_WIDGET (self->popover)));
  g_assert (g_utf8_validate (gtk_editable_get_text (self->rename_entry), -1, NULL));

  list = GTD_TASK_LIST (gtd_task_list_view_get_model (self->task_list_view));
  g_assert (list != NULL);

  new_name = g_strdup (gtk_editable_get_text (self->rename_entry));
  new_name = g_strstrip (new_name);

  /*
   * Even though the Rename button is insensitive, we may still reach here
   * by activating the entry.
   */
  if (!new_name || new_name[0] == '\0')
    return;

  if (g_strcmp0 (new_name, gtd_task_list_get_name (list)) != 0)
    {
      gtd_task_list_set_name (list, new_name);
      gtd_provider_update_task_list (gtd_task_list_get_provider (list),
                                     list,
                                     NULL,
                                     on_task_list_updated_cb,
                                     self);
    }

  gtk_popover_popdown (self->popover);
  gtk_editable_set_text (self->rename_entry, "");
}

static void
update_archive_button (GtdTaskListPanel *self)
{
  GtdTaskList *list;
  gboolean archived;

  GTD_ENTRY;

  list = GTD_TASK_LIST (gtd_task_list_view_get_model (self->task_list_view));
  g_assert (list != NULL);

  archived = gtd_task_list_get_archived (list);
  g_object_set (self->archive_button,
                "label", archived ? _("Unarchive") : _("Archive"),
                NULL);

  GTD_EXIT;
}


/*
 * Callbacks
 */

static void
on_task_list_updated_cb (GObject      *source,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  g_autoptr (GError) error = NULL;

  gtd_provider_update_task_list_finish (GTD_PROVIDER (source), result, &error);

  if (error)
    {
      g_warning ("Error creating task: %s", error->message);

      gtd_manager_emit_error_message (gtd_manager_get_default (),
                                      _("An error occurred while updating a task"),
                                      error->message,
                                      NULL,
                                      NULL);
    }
}

static void
on_archive_button_clicked_cb (GtkButton        *button,
                              GtdTaskListPanel *self)
{
  GtdProvider *provider;
  GtdTaskList *list;
  gboolean archived;

  GTD_ENTRY;

  list = GTD_TASK_LIST (gtd_task_list_view_get_model (self->task_list_view));
  g_assert (list != NULL);

  archived = gtd_task_list_get_archived (list);
  gtd_task_list_set_archived (list, !archived);

  update_archive_button (self);

  provider = gtd_task_list_get_provider (list);
  gtd_provider_update_task_list (provider,
                                 list,
                                 NULL,
                                 on_task_list_updated_cb,
                                 self);

  GTD_EXIT;
}

static void
on_colors_flowbox_child_activated_cb (GtkFlowBox       *colors_flowbox,
                                      GtkFlowBoxChild  *child,
                                      GtdTaskListPanel *self)
{
  const GdkRGBA *color;
  GtdTaskList *list;
  GtkWidget *color_button;

  list = GTD_TASK_LIST (gtd_task_list_view_get_model (self->task_list_view));

  g_assert (list != NULL);

  color_button = gtk_flow_box_child_get_child (child);

  if (self->previous_color_button == color_button)
    return;

  gtk_widget_set_state_flags (color_button, GTK_STATE_FLAG_SELECTED, FALSE);

  if (self->previous_color_button)
    gtk_widget_unset_state_flags (self->previous_color_button, GTK_STATE_FLAG_SELECTED);

  g_debug ("Setting new color for task list '%s'", gtd_task_list_get_name (list));

  color = gtd_color_button_get_color (GTD_COLOR_BUTTON (color_button));
  gtd_task_list_set_color (list, color);

  gtd_provider_update_task_list (gtd_task_list_get_provider (list),
                                 list,
                                 NULL,
                                 on_task_list_updated_cb,
                                 self);

  self->previous_color_button = color_button;
}

static void
on_delete_button_clicked_cb (GtkButton        *button,
                             GtdTaskListPanel *self)
{
  GtdTaskList *list;

  list = GTD_TASK_LIST (gtd_task_list_view_get_model (self->task_list_view));
  g_assert (list != NULL);

  GTD_TRACE_MSG ("Emitting GtdTaskListPanel:list-deleted");

  g_signal_emit (self, signals[LIST_DELETED], 0, list);
}

static void
on_go_to_rename_page_button_clicked_cb (GtkButton        *button,
                                        GtdTaskListPanel *self)
{
  gtk_stack_set_visible_child_name (self->popover_stack, "rename");
}

static void
on_popover_hidden_cb (GtkPopover       *popover,
                      GtdTaskListPanel *self)
{
  gtk_editable_set_text (self->rename_entry, "");
  gtk_stack_set_visible_child_name (self->popover_stack, "main");
}

static void
on_rename_button_clicked_cb (GtkButton        *button,
                             GtdTaskListPanel *self)
{
  rename_list (self);
}

static void
on_rename_entry_activated_cb (GtkEntry         *entry,
                              GtdTaskListPanel *self)
{
  rename_list (self);
}

static void
on_rename_entry_text_changed_cb (GtkEditable      *entry,
                                 GParamSpec       *pspec,
                                 GtdTaskListPanel *self)
{
  g_autofree gchar *new_name = NULL;
  gboolean valid;

  new_name = g_strdup (gtk_editable_get_text (entry));
  new_name = g_strstrip (new_name);

  valid = new_name && new_name[0] != '\0';

  gtk_widget_set_sensitive (self->rename_button, valid);
}


/*
 * GtdPanel iface
 */

static const gchar*
gtd_task_list_panel_get_panel_name (GtdPanel *panel)
{
  return "task-list-panel";
}

static const gchar*
gtd_task_list_panel_get_panel_title (GtdPanel *panel)
{
  GtdTaskListPanel *self;
  GtdTaskList *list;

  self = GTD_TASK_LIST_PANEL (panel);
  list = (GtdTaskList *) gtd_task_list_view_get_model (self->task_list_view);

  return list ? gtd_task_list_get_name (list) : "";
}

static GList*
gtd_task_list_panel_get_header_widgets (GtdPanel *panel)
{
  return NULL;
}

static const GMenu*
gtd_task_list_panel_get_menu (GtdPanel *panel)
{
  return NULL;
}

static GIcon*
gtd_task_list_panel_get_icon (GtdPanel *panel)
{
  return NULL;
}

static GtkPopover*
gtd_task_list_panel_get_popover (GtdPanel *panel)
{
  GtdTaskListPanel *self = GTD_TASK_LIST_PANEL (panel);
  return self->popover;
}


static guint32
gtd_task_list_panel_get_priority (GtdPanel *panel)
{
  return 0;
}

static gchar*
gtd_task_list_panel_get_subtitle (GtdPanel *panel)
{
  return NULL;
}

static void
gtd_task_list_panel_activate (GtdPanel *panel,
                              GVariant *parameters)
{
  GtdTaskListPanel *self;
  GVariantDict dict;
  GtdTaskList *list;
  GListModel *model;
  const gchar *task_list_id;
  const gchar *provider_id;
  guint i;

  GTD_ENTRY;

  self = GTD_TASK_LIST_PANEL (panel);

  /*
   * The task list panel must receive an a{sv} and looks for:
   *
   *  * provider-id: the id of the provider
   *  * task-list-id: the id of the task list
   *
   * So it can find the task list from the GtdManager.
   */

  g_variant_dict_init (&dict, parameters);
  g_variant_dict_lookup (&dict, "provider-id", "&s", &provider_id);
  g_variant_dict_lookup (&dict, "task-list-id", "&s", &task_list_id);

  GTD_TRACE_MSG ("Activating %s with 'provider-id': %s and 'task-list-id': %s",
                 G_OBJECT_TYPE_NAME (self),
                 provider_id,
                 task_list_id);

  model = gtd_manager_get_task_lists_model (gtd_manager_get_default ());
  list = NULL;

  for (i = 0; i < g_list_model_get_n_items (model); i++)
    {
      g_autoptr (GtdTaskList) task_list = NULL;
      GtdProvider *provider;

      task_list = g_list_model_get_item (model, i);
      if (g_strcmp0 (gtd_object_get_uid (GTD_OBJECT (task_list)), task_list_id) != 0)
        continue;

      provider = gtd_task_list_get_provider (task_list);
      if (g_strcmp0 (gtd_provider_get_id (provider), provider_id) != 0)
        return;

      list = task_list;
      break;
    }

  g_assert (list != NULL);

  gtd_task_list_panel_set_task_list (self, list);

  GTD_EXIT;
}

static void
gtd_panel_iface_init (GtdPanelInterface *iface)
{
  iface->get_panel_name = gtd_task_list_panel_get_panel_name;
  iface->get_panel_title = gtd_task_list_panel_get_panel_title;
  iface->get_header_widgets = gtd_task_list_panel_get_header_widgets;
  iface->get_menu = gtd_task_list_panel_get_menu;
  iface->get_icon = gtd_task_list_panel_get_icon;
  iface->get_popover = gtd_task_list_panel_get_popover;
  iface->get_priority = gtd_task_list_panel_get_priority;
  iface->get_subtitle = gtd_task_list_panel_get_subtitle;
  iface->activate = gtd_task_list_panel_activate;
}


/*
 * GObject overrides
 */

static void
gtd_task_list_panel_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  switch (prop_id)
    {
    case PROP_ICON:
      g_value_set_object (value, NULL);
      break;

    case PROP_MENU:
      g_value_set_object (value, NULL);
      break;

    case PROP_NAME:
      g_value_set_string (value, "task-list-panel");
      break;

    case PROP_PRIORITY:
      g_value_set_uint (value, 0);
      break;

    case PROP_SUBTITLE:
      g_value_take_string (value, NULL);
      break;

    case PROP_TITLE:
      g_value_set_string (value, gtd_panel_get_panel_title (GTD_PANEL (object)));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_task_list_panel_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}


static void
gtd_task_list_panel_class_init (GtdTaskListPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->get_property = gtd_task_list_panel_get_property;
  object_class->set_property = gtd_task_list_panel_set_property;

  g_object_class_override_property (object_class, PROP_ICON, "icon");
  g_object_class_override_property (object_class, PROP_MENU, "menu");
  g_object_class_override_property (object_class, PROP_NAME, "name");
  g_object_class_override_property (object_class, PROP_PRIORITY, "priority");
  g_object_class_override_property (object_class, PROP_SUBTITLE, "subtitle");
  g_object_class_override_property (object_class, PROP_TITLE, "title");

  signals[LIST_DELETED] = g_signal_new ("list-deleted",
                                        GTD_TYPE_TASK_LIST_PANEL,
                                        G_SIGNAL_RUN_LAST,
                                        0,
                                        NULL,
                                        NULL,
                                        NULL,
                                        G_TYPE_NONE,
                                        1,
                                        GTD_TYPE_TASK_LIST);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/todo/plugins/task-lists-workspace/gtd-task-list-panel.ui");

  gtk_widget_class_bind_template_child (widget_class, GtdTaskListPanel, archive_button);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskListPanel, colors_flowbox);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskListPanel, popover);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskListPanel, popover_stack);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskListPanel, rename_button);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskListPanel, rename_entry);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskListPanel, rename_entry);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskListPanel, task_list_view);

  gtk_widget_class_bind_template_callback (widget_class, on_archive_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_colors_flowbox_child_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_delete_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_go_to_rename_page_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_popover_hidden_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_rename_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_rename_entry_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_rename_entry_text_changed_cb);
}

static void
gtd_task_list_panel_init (GtdTaskListPanel *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  populate_color_grid (self);
}

GtkWidget*
gtd_task_list_panel_new (void)
{
  return g_object_new (GTD_TYPE_TASK_LIST_PANEL, NULL);
}

GtdTaskList*
gtd_task_list_panel_get_task_list (GtdTaskListPanel *self)
{
  g_return_val_if_fail (GTD_IS_TASK_LIST_PANEL (self), NULL);

  return GTD_TASK_LIST (gtd_task_list_view_get_model (self->task_list_view));
}

void
gtd_task_list_panel_set_task_list (GtdTaskListPanel *self,
                                   GtdTaskList      *list)
{
  g_return_if_fail (GTD_IS_TASK_LIST_PANEL (self));
  g_return_if_fail (GTD_IS_TASK_LIST (list));

  gtd_task_list_view_set_model (self->task_list_view, G_LIST_MODEL (list));

  update_selected_color (self);
  update_archive_button (self);

  g_object_notify (G_OBJECT (self), "title");
}

