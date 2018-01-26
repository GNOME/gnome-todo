/* gtd-list-selector.c
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

#define G_LOG_DOMAIN "GtdListSelector"

#include "gtd-enum-types.h"
#include "gtd-task-list.h"
#include "gtd-list-selector.h"

G_DEFINE_INTERFACE (GtdListSelector, gtd_list_selector, GTK_TYPE_WIDGET)

enum {
  LIST_SELECTED,
  NUM_SIGNALS
};

static guint signals[NUM_SIGNALS] = { 0, };

static void
gtd_list_selector_default_init (GtdListSelectorInterface *iface)
{
  /**
   * GtdListSelector::mode:
   *
   * The #GtdWindowMode of the list selector.
   */
  g_object_interface_install_property (iface,
                                       g_param_spec_enum ("mode",
                                                          "Mode of the list selector",
                                                          "The mode of the list selector",
                                                          GTD_TYPE_WINDOW_MODE,
                                                          GTD_WINDOW_MODE_NORMAL,
                                                          G_PARAM_READWRITE));

  /**
   * GtdListSelector::search-query:
   *
   * The search query of the list selector.
   */
  g_object_interface_install_property (iface,
                                       g_param_spec_string ("search-query",
                                                            "Search query of the list selector",
                                                            "The search query of the list selector",
                                                            NULL,
                                                            G_PARAM_READWRITE));

  /**
   * GtdListSelector::list-selected:
   *
   * The ::list-selected signals is emited whenever
   * a #GtdTaskList is selected.
   */
  signals[LIST_SELECTED] = g_signal_new ("list-selected",
                                         GTD_TYPE_LIST_SELECTOR,
                                         G_SIGNAL_RUN_LAST,
                                         0,
                                         NULL,
                                         NULL,
                                         NULL,
                                         G_TYPE_NONE,
                                         1,
                                         GTD_TYPE_TASK_LIST);
}

GtdWindowMode
gtd_list_selector_get_mode (GtdListSelector *selector)
{
  g_return_val_if_fail (GTD_IS_LIST_SELECTOR (selector), GTD_WINDOW_MODE_NORMAL);
  g_return_val_if_fail (GTD_LIST_SELECTOR_GET_IFACE (selector)->get_mode, GTD_WINDOW_MODE_NORMAL);

  return GTD_LIST_SELECTOR_GET_IFACE (selector)->get_mode (selector);
}

void
gtd_list_selector_set_mode (GtdListSelector *selector,
                            GtdWindowMode    mode)
{
  g_return_if_fail (GTD_IS_LIST_SELECTOR (selector));
  g_return_if_fail (GTD_LIST_SELECTOR_GET_IFACE (selector)->set_mode);

  GTD_LIST_SELECTOR_GET_IFACE (selector)->set_mode (selector, mode);
}

const gchar*
gtd_list_selector_get_search_query (GtdListSelector *selector)
{
  g_return_val_if_fail (GTD_IS_LIST_SELECTOR (selector), NULL);
  g_return_val_if_fail (GTD_LIST_SELECTOR_GET_IFACE (selector)->get_search_query, NULL);

  return GTD_LIST_SELECTOR_GET_IFACE (selector)->get_search_query (selector);
}

void
gtd_list_selector_set_search_query (GtdListSelector *selector,
                                    const gchar     *search_query)
{
  g_return_if_fail (GTD_IS_LIST_SELECTOR (selector));
  g_return_if_fail (GTD_LIST_SELECTOR_GET_IFACE (selector)->set_search_query);

  GTD_LIST_SELECTOR_GET_IFACE (selector)->set_search_query (selector, search_query);
}

GList*
gtd_list_selector_get_selected_lists (GtdListSelector *selector)
{
  g_return_val_if_fail (GTD_IS_LIST_SELECTOR (selector), NULL);
  g_return_val_if_fail (GTD_LIST_SELECTOR_GET_IFACE (selector)->get_selected_lists, NULL);

  return GTD_LIST_SELECTOR_GET_IFACE (selector)->get_selected_lists (selector);
}
