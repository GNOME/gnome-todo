/* gtd-list-selector-item.c
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

#define G_LOG_DOMAIN "GtdListSelectorItem"

#include "gtd-enum-types.h"
#include "gtd-task-list.h"
#include "gtd-list-selector-item.h"

G_DEFINE_INTERFACE (GtdListSelectorItem, gtd_list_selector_item, GTK_TYPE_WIDGET)

static void
gtd_list_selector_item_default_init (GtdListSelectorItemInterface *iface)
{
  /**
   * GtdListSelectorItem::list:
   *
   * The #GtdTaskList this item represents.
   */
  g_object_interface_install_property (iface,
                                       g_param_spec_object ("task-list",
                                                            "List of item",
                                                            "The list this item represents",
                                                            GTD_TYPE_TASK_LIST,
                                                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

  /**
   * GtdListSelectorItem::mode:
   *
   * The mode of the parent widget.
   */
  g_object_interface_install_property (iface,
                                       g_param_spec_enum ("mode",
                                                          "Mode of the parent",
                                                          "The mode of the parent",
                                                          GTD_TYPE_WINDOW_MODE,
                                                          GTD_WINDOW_MODE_NORMAL,
                                                          G_PARAM_READWRITE));

  /**
   * GtdListSelectorItem::selected:
   *
   * Whether the item is selected or not.
   */
  g_object_interface_install_property (iface,
                                       g_param_spec_boolean ("selected",
                                                             "Whether the item is selected",
                                                             "Whether the item is selected or not",
                                                             FALSE,
                                                             G_PARAM_READWRITE));

}

GtdTaskList*
gtd_list_selector_item_get_list (GtdListSelectorItem *item)
{
  g_return_val_if_fail (GTD_IS_LIST_SELECTOR_ITEM (item), NULL);
  g_return_val_if_fail (GTD_LIST_SELECTOR_ITEM_GET_IFACE (item)->get_list, NULL);

  return GTD_LIST_SELECTOR_ITEM_GET_IFACE (item)->get_list (item);
}

gboolean
gtd_list_selector_item_get_selected (GtdListSelectorItem *item)
{
  g_return_val_if_fail (GTD_IS_LIST_SELECTOR_ITEM (item), FALSE);
  g_return_val_if_fail (GTD_LIST_SELECTOR_ITEM_GET_IFACE (item)->get_selected, FALSE);

  return GTD_LIST_SELECTOR_ITEM_GET_IFACE (item)->get_selected (item);
}

void
gtd_list_selector_item_set_selected (GtdListSelectorItem *item,
                                     gboolean             selected)
{
  g_return_if_fail (GTD_IS_LIST_SELECTOR_ITEM (item));
  g_return_if_fail (GTD_LIST_SELECTOR_ITEM_GET_IFACE (item)->get_list);

  GTD_LIST_SELECTOR_ITEM_GET_IFACE (item)->set_selected (item, selected);
}
