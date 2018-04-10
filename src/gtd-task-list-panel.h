/* gtd-task-list-panel.h
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

#pragma once

#include "gtd-types.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GTD_TYPE_TASK_LIST_PANEL (gtd_task_list_panel_get_type())

G_DECLARE_FINAL_TYPE (GtdTaskListPanel, gtd_task_list_panel, GTD, TASK_LIST_PANEL, GtkBox)

GtkWidget*           gtd_task_list_panel_new                     (void);

GtdTaskList*         gtd_task_list_panel_get_task_list           (GtdTaskListPanel   *self);

void                 gtd_task_list_panel_set_task_list           (GtdTaskListPanel   *self,
                                                                  GtdTaskList        *list);

G_END_DECLS
