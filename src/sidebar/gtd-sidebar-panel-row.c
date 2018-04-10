/* gtd-sidebar-panel-row.c
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

#include "gtd-panel.h"
#include "gtd-sidebar-panel-row.h"

struct _GtdSidebarPanelRow
{
  GtkListBoxRow       parent;

  GtkWidget          *panel_icon;
  GtkWidget          *subtitle_label;
  GtkWidget          *title_label;

  GtdPanel           *panel;
};

G_DEFINE_TYPE (GtdSidebarPanelRow, gtd_sidebar_panel_row, GTK_TYPE_LIST_BOX_ROW)

enum
{
  PROP_0,
  PROP_PANEL,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];


/*
 * Auxiliary methods
 */

static void
set_panel (GtdSidebarPanelRow *self,
           GtdPanel           *panel)
{
  g_assert (panel != NULL);
  g_assert (self->panel == NULL);

  self->panel = g_object_ref (panel);

  /* Bind panel properties to the row widgets */
  g_object_bind_property (self->panel,
                          "icon",
                          self->panel_icon,
                          "gicon",
                          G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

  g_object_bind_property (self->panel,
                          "title",
                          self->title_label,
                          "label",
                          G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

  g_object_bind_property (self->panel,
                          "subtitle",
                          self->subtitle_label,
                          "label",
                          G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PANEL]);
}


/*
 * GObject overrides
 */

static void
gtd_sidebar_panel_row_finalize (GObject *object)
{
  GtdSidebarPanelRow *self = (GtdSidebarPanelRow *)object;

  g_clear_object (&self->panel);

  G_OBJECT_CLASS (gtd_sidebar_panel_row_parent_class)->finalize (object);
}

static void
gtd_sidebar_panel_row_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  GtdSidebarPanelRow *self = GTD_SIDEBAR_PANEL_ROW (object);

  switch (prop_id)
    {
    case PROP_PANEL:
      g_value_set_object (value, self->panel);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_sidebar_panel_row_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  GtdSidebarPanelRow *self = GTD_SIDEBAR_PANEL_ROW (object);

  switch (prop_id)
    {
    case PROP_PANEL:
      set_panel (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_sidebar_panel_row_class_init (GtdSidebarPanelRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gtd_sidebar_panel_row_finalize;
  object_class->get_property = gtd_sidebar_panel_row_get_property;
  object_class->set_property = gtd_sidebar_panel_row_set_property;

  properties[PROP_PANEL] = g_param_spec_object ("panel",
                                                "Panel",
                                                "The panel this row represents",
                                                GTD_TYPE_PANEL,
                                                G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/todo/ui/sidebar-panel-row.ui");

  gtk_widget_class_bind_template_child (widget_class, GtdSidebarPanelRow, panel_icon);
  gtk_widget_class_bind_template_child (widget_class, GtdSidebarPanelRow, subtitle_label);
  gtk_widget_class_bind_template_child (widget_class, GtdSidebarPanelRow, title_label);
}

static void
gtd_sidebar_panel_row_init (GtdSidebarPanelRow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget*
gtd_sidebar_panel_row_new (GtdPanel *panel)
{
  return g_object_new (GTD_TYPE_SIDEBAR_PANEL_ROW,
                       "panel", panel,
                       NULL);
}

GtdPanel*
gtd_sidebar_panel_row_get_panel (GtdSidebarPanelRow *self)
{
  g_return_val_if_fail (GTD_IS_SIDEBAR_PANEL_ROW (self), NULL);

  return self->panel;
}
