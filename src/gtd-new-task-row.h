/* gtd-new-task-row.h
 *
 * Copyright (C) 2017 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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

#ifndef GTD_NEW_TASK_ROW_H
#define GTD_NEW_TASK_ROW_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GTD_TYPE_NEW_TASK_ROW (gtd_new_task_row_get_type())

G_DECLARE_FINAL_TYPE (GtdNewTaskRow, gtd_new_task_row, GTD, NEW_TASK_ROW, GtkBin)

GtkWidget*           gtd_new_task_row_new                        (void);

gboolean             gtd_new_task_row_get_active                 (GtdNewTaskRow      *self);

void                 gtd_new_task_row_set_active                 (GtdNewTaskRow      *self,
                                                                  gboolean            active);

void                 gtd_new_task_row_set_show_list_selector     (GtdNewTaskRow      *self,
                                                                  gboolean            show_list_selector);

G_END_DECLS

#endif /* GTD_NEW_TASK_ROW_H */

