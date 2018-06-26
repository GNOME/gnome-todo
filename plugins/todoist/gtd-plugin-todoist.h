/* gtd-plugin-todoist.h
 *
 * Copyright (C) 2017 Rohit Kaushik <kaushikrohit325@gmail.com>
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

#ifndef GTD_TODOIST_PLUGIN_H
#define GTD_TODOIST_PLUGIN_H

#include "gnome-todo.h"

#include <glib.h>

G_BEGIN_DECLS

typedef enum
{
  GTD_TODOIST_NETWORK_LOCAL_CONNECTIVITY,
  GTD_TODOIST_NETWORK_LIMITED_CONNECTIVITY,
  GTD_TODOIST_NETWORK_PORTAL_ERROR,
  GTD_TODOIST_NETWORK_SERVER_UNREACHABLE
} GtdTodoistNetworkError;

#define GTD_TYPE_PLUGIN_TODOIST (gtd_plugin_todoist_get_type())
#define GTD_TODOIST_NETWORK_ERROR (gtd_todoist_network_error_quark ())

GQuark               gtd_todo_txt_parser_error_quark             (void);

G_DECLARE_FINAL_TYPE (GtdPluginTodoist, gtd_plugin_todoist, GTD, PLUGIN_TODOIST, PeasExtensionBase)

G_MODULE_EXPORT void  gtd_plugin_todoist_register_types         (PeasObjectModule   *module);

G_END_DECLS

#endif /* GTD_TODOIST_PLUGIN_H */
