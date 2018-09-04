/* gtd-panel.c
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

#define G_LOG_DOMAIN "GtdPanel"

#include "gtd-panel.h"

/**
 * SECTION:gtd-panel
 * @short_description: interface for panels
 * @title:GtdPanel
 * @stability:Unstable
 *
 * The #GtdPanel interface must be implemented by plugins that want
 * a given widget to be shown as a panel in the main window. Examples
 * of panels are the "Today" and "Scheduled" panels.
 *
 * A panel must have a unique name (see #GtdPanel:name) and a title.
 * The title can change dynamically. Avoid long titles.
 *
 * The panel may also provide header widgets, which will be placed
 * in the headerbar according to the #GtkWidget:halign property. See
 * gtd_panel_get_header_widgets() for a detailed explanation.
 *
 * The #GtdPanel:icon and #GtdPanel:priority properties are used by
 * the sidebar. The former is used to display the icon, and the latter
 * is used to determine the position of the panel relative to the
 * others panels.
 *
 * At last, a #GtdPanel implementation may provide a #GMenu that will
 * be appended to the window menu. Alternatively, a #GtkPopover can
 * also be set. Popovers are used when both a menu and a popover are
 * provided.
 */

G_DEFINE_INTERFACE (GtdPanel, gtd_panel, GTK_TYPE_WIDGET)

static void
gtd_panel_default_init (GtdPanelInterface *iface)
{

  /**
   * GtdPanel::icon:
   *
   * The icon of the panel.
   */
  g_object_interface_install_property (iface,
                                       g_param_spec_object ("icon",
                                                            "Icon of the panel",
                                                            "The icon of the panel",
                                                            G_TYPE_ICON,
                                                            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * GtdPanel::name:
   *
   * The identifier name of the panel. It is used as the #GtkStack
   * name, so be sure to use a specific name that won't collide with
   * other plugins.
   */
  g_object_interface_install_property (iface,
                                       g_param_spec_string ("name",
                                                            "The name of the panel",
                                                            "The identifier name of the panel",
                                                            NULL,
                                                            G_PARAM_READABLE));

  /**
   * GtdPanel::priority:
   *
   * The priority of the panel.
   */
  g_object_interface_install_property (iface,
                                       g_param_spec_uint ("priority",
                                                          "Priority of the panel",
                                                          "The priority of the panel",
                                                          0,
                                                          G_MAXUINT32,
                                                          0,
                                                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * GtdPanel::subtitle:
   *
   * The subtitle of the panel. It usually is the counter of not
   * completed tasks.
   */
  g_object_interface_install_property (iface,
                                       g_param_spec_string ("subtitle",
                                                            "The subtitle of the panel",
                                                            "The user-visible subtitle of the panel",
                                                            NULL,
                                                            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * GtdPanel::title:
   *
   * The user-visible title of the panel. It is used as the #GtkStack
   * title.
   */
  g_object_interface_install_property (iface,
                                       g_param_spec_string ("title",
                                                            "The title of the panel",
                                                            "The user-visible title of the panel",
                                                            NULL,
                                                            G_PARAM_READABLE));

  /**
   * GtdPanel::menu:
   *
   * A #GMenu of entries of the window menu.
   */
  g_object_interface_install_property (iface,
                                       g_param_spec_object ("menu",
                                                            "The title of the panel",
                                                            "The user-visible title of the panel",
                                                            G_TYPE_MENU,
                                                            G_PARAM_READABLE));

}

/**
 * gtd_panel_get_panel_name:
 * @panel: a #GtdPanel
 *
 * Retrieves the name of @panel
 *
 * Returns: (transfer none): the name of @panel
 */
const gchar*
gtd_panel_get_panel_name (GtdPanel *panel)
{
  g_return_val_if_fail (GTD_IS_PANEL (panel), NULL);
  g_return_val_if_fail (GTD_PANEL_GET_IFACE (panel)->get_panel_name, NULL);

  return GTD_PANEL_GET_IFACE (panel)->get_panel_name (panel);
}

/**
 * gtd_panel_get_panel_title:
 * @panel: a #GtdPanel
 *
 * Retrieves the title of @panel
 *
 * Returns: (transfer none): the title of @panel
 */
const gchar*
gtd_panel_get_panel_title (GtdPanel *panel)
{
  g_return_val_if_fail (GTD_IS_PANEL (panel), NULL);
  g_return_val_if_fail (GTD_PANEL_GET_IFACE (panel)->get_panel_title, NULL);

  return GTD_PANEL_GET_IFACE (panel)->get_panel_title (panel);
}

/**
 * gtd_panel_get_header_widgets:
 * @panel: a #GtdPanel
 *
 * Retrieves the list of widgets to be placed at headerbar. The
 * position of the widget is determined by the #GtkWidget::halign
 * property.
 *
 * Widgets with @GTK_ALIGN_START halign will be packed into the
 * start of the headerbar, and @GTK_ALIGN_END at the end. Other
 * values are silently ignored.
 *
 * Returns: (transfer container) (element-type Gtk.Widget): the list of #GtkWidget
 */
GList*
gtd_panel_get_header_widgets (GtdPanel *panel)
{
  g_return_val_if_fail (GTD_IS_PANEL (panel), NULL);
  g_return_val_if_fail (GTD_PANEL_GET_IFACE (panel)->get_header_widgets, NULL);

  return GTD_PANEL_GET_IFACE (panel)->get_header_widgets (panel);
}

/**
 * gtd_panel_get_menu:
 * @panel: a #GtdPanel
 *
 * Retrieves the gear menu of @panel.
 *
 * Returns: (transfer none): a #GMenu
 */
const GMenu*
gtd_panel_get_menu (GtdPanel *panel)
{
  g_return_val_if_fail (GTD_IS_PANEL (panel), NULL);
  g_return_val_if_fail (GTD_PANEL_GET_IFACE (panel)->get_menu, NULL);

  return GTD_PANEL_GET_IFACE (panel)->get_menu (panel);
}

/**
 * gtd_panel_get_icon:
 * @self: a #GtdPanel
 *
 * Retrieves the icon of @self.
 *
 * Returns: (transfer full)(nullable): a #GIcon
 */
GIcon*
gtd_panel_get_icon (GtdPanel *self)
{
  g_return_val_if_fail (GTD_IS_PANEL (self), NULL);
  g_return_val_if_fail (GTD_PANEL_GET_IFACE (self)->get_icon, NULL);

  return GTD_PANEL_GET_IFACE (self)->get_icon (self);
}

/**
 * gtd_panel_get_popover:
 * @self: a #GtdPanel
 *
 * Retrieves the popover of @self. It is used as the
 * window menu when available.
 *
 * Returns: (nullable)(transfer none): a #GtkPopover
 */
GtkPopover*
gtd_panel_get_popover (GtdPanel *self)
{
  g_return_val_if_fail (GTD_IS_PANEL (self), 0);

  if (GTD_PANEL_GET_IFACE (self)->get_popover)
    return GTD_PANEL_GET_IFACE (self)->get_popover (self);

  return NULL;
}

/**
 * gtd_panel_get_priority:
 * @self: a #GtdPanel
 *
 * Retrieves the priority of @self. This value is used to
 * determine the position of the panel in the sidebar.
 *
 * Returns: the priority of @self
 */
guint32
gtd_panel_get_priority (GtdPanel *self)
{
  g_return_val_if_fail (GTD_IS_PANEL (self), 0);
  g_return_val_if_fail (GTD_PANEL_GET_IFACE (self)->get_priority, 0);

  return GTD_PANEL_GET_IFACE (self)->get_priority (self);
}

/**
 * gtd_panel_get_subtitle:
 * @self: a #GtdPanel
 *
 * Retrieves the subtitle of @panel
 *
 * Returns: (transfer full): the subtitle of @panel
 */
gchar*
gtd_panel_get_subtitle (GtdPanel *self)
{
  g_return_val_if_fail (GTD_IS_PANEL (self), NULL);
  g_return_val_if_fail (GTD_PANEL_GET_IFACE (self)->get_icon, NULL);

  return GTD_PANEL_GET_IFACE (self)->get_subtitle (self);
}
