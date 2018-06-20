/* gtd-todo-txt-parser.h
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

#ifndef GTD_TODO_TXT_PARSE_H
#define GTD_TODO_TXT_PARSE_H

#include "gnome-todo.h"
#include "gtd-task-todo-txt.h"

#include <glib.h>

G_BEGIN_DECLS

typedef enum
{
  GTD_TODO_TXT_PARSER_INVALID_DUE_DATE,
  GTD_TODO_TXT_PARSER_INVALID_COLOR_HEX,
  GTD_TODO_TXT_PARSER_INVALID_INDENT,
  GTD_TODO_TXT_PARSER_INVALID_LINE,
  GTD_TODO_TXT_PARSER_UNSUPPORTED_TOKEN,
  GTD_TODO_TXT_PARSER_WRONG_LINE_TYPE,
} GtdTodoTxtParserError;

typedef enum
{
  GTD_TODO_TXT_LINE_TYPE_TASKLIST,
  GTD_TODO_TXT_LINE_TYPE_TASK,
  GTD_TODO_TXT_LINE_TYPE_LIST_COLORS,
  GTD_TODO_TXT_LINE_TYPE_UNKNOWN
} GtdTodoTxtLineType;

#define GTD_TODO_TXT_PARSER_ERROR (gtd_todo_txt_parser_error_quark ())

GQuark               gtd_todo_txt_parser_error_quark             (void);

GtdTodoTxtLineType   gtd_todo_txt_parser_get_line_type           (const gchar       *line,
                                                                  GError           **error);

GPtrArray*           gtd_todo_txt_parser_parse_task_lists        (GtdProvider       *provider,
                                                                  const gchar       *line,
                                                                  GError           **error);

GtdTask*             gtd_todo_txt_parser_parse_task              (GtdProvider       *provider,
                                                                  const gchar       *line,
                                                                  gchar            **out_list_name,
                                                                  GError           **error);

gboolean             gtd_todo_txt_parser_parse_task_list_color   (GHashTable        *name_to_tasklist,
                                                                  const gchar       *line,
                                                                  GError           **error);

G_END_DECLS

#endif /* GTD_TODO_TXT_PARSER_H */
