/* gtd-todoist-preferences-panel.h
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

#ifndef GTD_TODOIST_WEB_VIEW_H
#define GTD_TODOIST_WEB_VIEW_H

#include "gnome-todo.h"

#include <glib.h>
#include <webkit2/webkit2.h>

G_BEGIN_DECLS

#define GTD_TYPE_TODOIST_OAUTH2 (gtd_todoist_oauth2_get_type())

G_DECLARE_FINAL_TYPE (GtdTodoistOAuth2, gtd_todoist_oauth2, GTD, TODOIST_OAUTH2, GtkWindow)

GtdTodoistOAuth2*   gtd_todoist_oauth2_new           (void);

G_END_DECLS

#endif /* GTD_TODOIST_WEB_VIEW_H */
