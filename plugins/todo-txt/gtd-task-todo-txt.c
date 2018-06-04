/* gtd-task-todo-txt.c
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

#define G_LOG_DOMAIN "GtdTaskTodoTxt"

#include "gtd-task-todo-txt.h"

struct _GtdTaskTodoTxt
{
  GtdTask             parent;
};

G_DEFINE_TYPE (GtdTaskTodoTxt, gtd_task_todo_txt, GTD_TYPE_TASK)

/*
 * GtdTask overrides
 */
static void
gtd_task_todo_txt_set_completion_date (GtdTask   *task,
									   GDateTime *dt)
{
  GTD_TASK_CLASS (gtd_task_todo_txt_parent_class)->set_completion_date (task, dt);
}

static void
gtd_task_todo_txt_class_init (GtdTaskTodoTxtClass *klass)
{
  GtdTaskClass *task_class = GTD_TASK_CLASS (klass);

  task_class->set_completion_date = gtd_task_todo_txt_set_completion_date;
}

static void
gtd_task_todo_txt_init (GtdTaskTodoTxt *self)
{
}

GtdTask*
gtd_task_todo_txt_new (void)
{
  return g_object_new (GTD_TYPE_TASK_TODO_TXT, NULL);
}
