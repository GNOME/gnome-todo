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

#include <glib.h>

G_BEGIN_DECLS

#define GTD_TYPE_TODO_TXT_PARSER (gtd_todo_txt_parser_get_type())

typedef struct _TaskData TaskData;

G_DECLARE_FINAL_TYPE (GtdTodoTxtParser, gtd_todo_txt_parser, GTD, TODO_TXT_PARSER, GtdObject)

gint          gtd_todo_txt_parser_get_priority                    (gchar             *token);

GDateTime*    gtd_todo_txt_parser_get_date                        (gchar             *token);

gboolean      gtd_todo_txt_parser_is_date                         (gchar             *dt);

gboolean      gtd_todo_txt_parser_is_word                         (gchar             *token);

void          gtd_todo_txt_parser_parse_tokens                    (GtdTask           *task,
                                                                   GList             *tokens);

gboolean      gtd_todo_txt_parser_validate_token_format           (GList             *tokens);

GList*        gtd_todo_txt_parser_tokenize                        (const gchar       *line);

gchar*        gtd_todo_txt_parser_serialize_list                  (GtdTaskList       *list);

gchar*        gtd_todo_txt_parser_serialize_task                  (GtdTask           *task);

G_END_DECLS

#endif /* GTD_TODO_TXT_PARSER_H */
