/* gtd-task-list-panel.c
 *
 * Copyright 2018 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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

#include "gtd-color-button.h"
#include "gtd-panel.h"
#include "gtd-provider.h"
#include "gtd-task-list.h"
#include "gtd-task-list-panel.h"
#include "gtd-task-list-view.h"
#include "gtd-utils.h"

struct _GtdTaskListPanel
{
  GtkBox              parent;

  GtkFlowBox         *colors_flowbox;
  GtkPopover         *popover;
  GtdTaskListView    *task_list_view;

  GtkWidget          *previous_color_button;
};


static void          on_colors_flowbox_child_activated_cb        (GtkFlowBox         *colors_flowbox,
                                                                  GtkFlowBoxChild    *child,
                                                                  GtdTaskListPanel   *self);

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

      gtk_container_add (GTK_CONTAINER (self->colors_flowbox), button);
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


/*
 * Callbacks
 */

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

  color_button = gtk_bin_get_child (GTK_BIN (child));

  if (self->previous_color_button == color_button)
    return;

  gtk_widget_set_state_flags (color_button, GTK_STATE_FLAG_SELECTED, FALSE);

  if (self->previous_color_button)
    gtk_widget_unset_state_flags (self->previous_color_button, GTK_STATE_FLAG_SELECTED);

  g_debug ("Setting new color for task list '%s'", gtd_task_list_get_name (list));

  color = gtd_color_button_get_color (GTD_COLOR_BUTTON (color_button));
  gtd_task_list_set_color (list, color);

  gtd_provider_update_task_list (gtd_task_list_get_provider (list), list);

  self->previous_color_button = color_button;
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
  return "";
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
}


/*
 * GObject overrides
 */

static void
gtd_task_list_panel_finalize (GObject *object)
{
  //GtdTaskListPanel *self = (GtdTaskListPanel *)object;

  G_OBJECT_CLASS (gtd_task_list_panel_parent_class)->finalize (object);
}

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
      g_value_set_string (value, NULL);
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

  object_class->finalize = gtd_task_list_panel_finalize;
  object_class->get_property = gtd_task_list_panel_get_property;
  object_class->set_property = gtd_task_list_panel_set_property;

  g_object_class_override_property (object_class, PROP_ICON, "icon");
  g_object_class_override_property (object_class, PROP_MENU, "menu");
  g_object_class_override_property (object_class, PROP_NAME, "name");
  g_object_class_override_property (object_class, PROP_PRIORITY, "priority");
  g_object_class_override_property (object_class, PROP_SUBTITLE, "subtitle");
  g_object_class_override_property (object_class, PROP_TITLE, "title");

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/todo/ui/task-list-panel.ui");

  gtk_widget_class_bind_template_child (widget_class, GtdTaskListPanel, colors_flowbox);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskListPanel, popover);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskListPanel, task_list_view);

  gtk_widget_class_bind_template_callback (widget_class, on_colors_flowbox_child_activated_cb);
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
}

