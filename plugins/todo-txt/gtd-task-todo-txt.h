/* gtd-task-todo-txt.h
 *
 * Copyright (C) 2018 Rohit Kaushik <kaushikrohit325@gmail.com>
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

#ifndef GTD_TASK_TODO_TXT_H
#define GTD_TASK_TODO_TXT_H

#include "gnome-todo.h"

G_BEGIN_DECLS

#define GTD_TYPE_TASK_TODO_TXT (gtd_task_todo_txt_get_type())

G_DECLARE_FINAL_TYPE (GtdTaskTodoTxt, gtd_task_todo_txt, GTD, TASK_TODO_TXT, GtdTask)

GtdTask*             gtd_task_todo_txt_new                            (void);

G_END_DECLS

#endif /* GTD_TASK_TODO_TXT_H */

