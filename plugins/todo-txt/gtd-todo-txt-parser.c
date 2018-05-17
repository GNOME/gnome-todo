/* gtd-task-list.c
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

#define G_LOG_DOMAIN "GtdTodoTxtParser"

#include "gtd-debug.h"
#include "gtd-utils.h"
#include "gtd-todo-txt-parser.h"
#include "gtd-provider-todo-txt.h"

#include <glib/gi18n.h>


G_DEFINE_QUARK (GtdTodoTxtParserError, gtd_todo_txt_parser_error)


typedef enum
{
  TOKEN_START,
  TOKEN_HIDDEN,
  TOKEN_COMPLETE,
  TOKEN_PRIORITY,
  TOKEN_DATE,
  TOKEN_TITLE,
  TOKEN_LIST_NAME,
  TOKEN_LIST_COLOR,
  TOKEN_DUE_DATE,
  TOKEN_IN_DESCRIPTION,
  TOKEN_NOTES
} Token;

static gint
parse_priority (const gchar *token)
{
  switch (token[1])
    {
    case 'A':
      return 3;

    case 'B':
      return 2;

    case 'C':
      return 1;

    default:
      return 0;
    }

  return 0;
}

static GDateTime*
parse_date (const gchar *token)
{
  GDateTime *dt;
  GDate date;
  gint year;
  gint month;
  gint day;

  dt = NULL;
  g_date_clear (&date, 1);
  g_date_set_parse (&date, token);

  if (!g_date_valid (&date))
    return NULL;

  year = g_date_get_year (&date);
  month = g_date_get_month (&date);
  day = g_date_get_day (&date);

  dt = g_date_time_new_utc (year, month, day, 0, 0, 0);

  return dt;
}

static gboolean
is_date (const gchar *dt)
{
  GDate date;

  g_date_clear (&date, 1);
  g_date_set_parse (&date, dt);

  return g_date_valid (&date);
}

static Token
parse_token_id (const gchar *token,
                gint         last_read)
{
  gint token_length;

  token_length = strlen (token);

  if (g_strcmp0 (token, "x") == 0)
    return TOKEN_COMPLETE;

  if (g_strcmp0 (token, "h:1") == 0)
    return TOKEN_HIDDEN;

  if (token_length == 3 && token[0] == '(' && token[2] == ')')
    return TOKEN_PRIORITY;

  if (!g_str_has_prefix (token , "due:") && is_date (token))
    return TOKEN_DATE;

  if (g_str_has_prefix (token , "color:"))
    return TOKEN_LIST_COLOR;

  if (token_length > 1 && token[0] == '@')
    return TOKEN_LIST_NAME;

  if (g_str_has_prefix (token , "due:"))
    return TOKEN_DUE_DATE;
  
  if (g_str_has_prefix (token , "notes:"))
    return TOKEN_NOTES;
  
  if (last_read == TOKEN_NOTES ||
      last_read == TOKEN_IN_DESCRIPTION)
    return TOKEN_IN_DESCRIPTION;
    
  if (last_read == TOKEN_START ||
      last_read == TOKEN_DATE ||
      last_read == TOKEN_PRIORITY ||
      last_read == TOKEN_COMPLETE||
      last_read == TOKEN_TITLE)
    {
      return TOKEN_TITLE;
    }
  else if (last_read == TOKEN_LIST_NAME)
    {
      return TOKEN_LIST_NAME;
    }

  return -1;
}

static GStrv
tokenize_line (const gchar *line)
{
  GStrv tokens = NULL;
  gsize i;

  tokens = g_strsplit (line, " ", -1);

  for (i = 0; tokens && tokens[i]; i++)
    g_strstrip (tokens[i]);

  return tokens;
}

GtdTask*
gtd_todo_txt_parser_parse_task (GtdProvider  *provider,
                                const gchar  *line,
                                gchar       **out_list_name)
{
  g_autoptr (GString) parent_task_name = NULL;
  g_autoptr (GString) list_name = NULL;
  g_autoptr (GString) title = NULL;
  g_autoptr (GString) notes = NULL;
  g_autoptr (GtdTask) task = NULL;
  g_auto (GStrv) tokens = NULL;
  GDateTime *dt;
  Token last_token;
  Token token_id;
  guint i;

  dt = NULL;
  title = g_string_new (NULL);
  list_name = g_string_new (NULL);
  notes = g_string_new (NULL);
  parent_task_name = g_string_new (NULL);
  last_token = TOKEN_START;

  task = gtd_provider_todo_txt_generate_task (GTD_PROVIDER_TODO_TXT (provider));
  tokens = tokenize_line (line);

  for (i = 0; tokens && tokens[i]; i++)
    {
      const gchar *token;
      gchar *esc_token;

      token = tokens[i];
      token_id = parse_token_id (token, last_token);

      switch (token_id)
        {
        case TOKEN_COMPLETE:
          gtd_task_set_complete (task, TRUE);
          break;

        case TOKEN_HIDDEN:
          return NULL;

        case TOKEN_PRIORITY:
          last_token = TOKEN_PRIORITY;
          gtd_task_set_priority (task, parse_priority (token));
          break;

        case TOKEN_DATE:
          dt = parse_date (token);
          break;

        case TOKEN_TITLE:
          g_string_append (title, token);
          g_string_append (title, " ");
          break;

        case TOKEN_LIST_NAME:
          g_string_append (list_name, token);
          g_string_append (list_name, " ");
          break;

        case TOKEN_DUE_DATE:
          dt = parse_date (token + strlen ("due:"));
          gtd_task_set_due_date (task, dt);
          break;
        
        case TOKEN_NOTES:
          /* remove the extra "\" added for escaping special character */
          esc_token = g_strcompress (token + strlen("notes:"));
          g_string_append (notes, esc_token);
          g_free (esc_token);
          break;
        
        case TOKEN_IN_DESCRIPTION:
          esc_token = g_strcompress (token);
          g_string_append (notes, " ");
          g_string_append (notes, esc_token);
          g_free(esc_token);
          break;
          
        case TOKEN_LIST_COLOR:
        case TOKEN_START:
        default:
          break;
        }

      last_token = token_id;
    }

  g_strstrip (parent_task_name->str);
  g_strstrip (list_name->str);
  g_strstrip (title->str);

  gtd_task_set_title (task, title->str);
  gtd_task_set_description (task, notes->str);

  if (out_list_name)
    *out_list_name = g_strdup (list_name->str + 1);

  return g_steal_pointer (&task);
}

/**
 * gtd_todo_txt_parser_parse_task_list:
 * provider: the @GtdProvider of the new tasklist
 * @line: the tasklist line to be parsed
 *
 * Parses a @GtdTaskList from @line. If there is a 'color:' token,
 * it is taken into account.
 *
 * Returns: (transfer full)(nullable): A @GtdTaskList
 */
GtdTaskList*
gtd_todo_txt_parser_parse_task_list (GtdProvider *provider,
                                     const gchar *line)
{
  g_autoptr (GtdTaskList) new_list = NULL;
  g_autoptr (GString) list_name = NULL;
  g_autofree gchar *color = NULL;
  g_auto (GStrv) tokens = NULL;
  guint i;

  tokens = tokenize_line (line);
  list_name = g_string_new (NULL);

  GTD_TRACE_MSG ("Parsing tasklist from line '%s'", line);

  for (i = 0; tokens && tokens[i]; i++)
    {
      const gchar *token = tokens[i];

      if (!token)
        break;

      /* Color */
      if (g_str_has_prefix (token, "color:"))
        {
          color = g_strdup (token + strlen ("color:"));
          continue;
        }

      /* Hidden token */
      if (g_str_has_prefix (token, "h:1"))
        continue;

      /* Title */
      g_string_append_printf (list_name, "%s ", token[0] == '@' ? token + 1 : token);
    }

  if (list_name->len == 0)
    return NULL;

  g_strstrip (list_name->str);

  new_list = g_object_new (GTD_TYPE_TASK_LIST,
                           "provider", provider,
                           "name", list_name->str,
                           "is-removable", TRUE,
                           NULL);

  if (color)
    {
      GdkRGBA rgba;

      gdk_rgba_parse (&rgba, color);

      gtd_task_list_set_color (new_list, &rgba);
    }

  return g_steal_pointer (&new_list);
}

/**
 * gtd_todo_txt_parser_get_line_type:
 * @line: the line to parse
 * @error: (nullable): return location for a #GError
 *
 * Validates the given line and returns the line type.
 *
 * Returns: the line type
 */
GtdTodoTxtLineType
gtd_todo_txt_parser_get_line_type (const gchar  *line,
                                   GError      **error)
{
  GtdTodoTxtLineType line_type;
  g_auto (GStrv) tokens;
  gboolean task_list_name_tk;
  Token last_read;
  Token token_id;
  gint i;

  GTD_ENTRY;

  tokens = tokenize_line (line);
  last_read = TOKEN_START;
  line_type = GTD_TODO_TXT_LINE_TYPE_TASKLIST;
  task_list_name_tk = FALSE;

  for (i = 0; tokens && tokens[i]; i++)
    {
      const gchar *token = tokens[i];

      token_id = parse_token_id (token, last_read);

      switch (token_id)
        {
        case TOKEN_COMPLETE:
          if (last_read == TOKEN_START)
            line_type = GTD_TODO_TXT_LINE_TYPE_TASK;
          break;

        case TOKEN_HIDDEN:
          return GTD_TODO_TXT_LINE_TYPE_TASKLIST;

        case TOKEN_PRIORITY:
          if (last_read <= TOKEN_COMPLETE)
            line_type = GTD_TODO_TXT_LINE_TYPE_TASK;
          break;

        case TOKEN_DATE:
          if (last_read <= TOKEN_PRIORITY)
            {
              line_type = GTD_TODO_TXT_LINE_TYPE_TASK;

              if (!is_date (token))
                {
                  g_set_error (error,
                               GTD_TODO_TXT_PARSER_ERROR,
                               GTD_TODO_TXT_PARSER_INVALID_DUE_DATE,
                               "Invalid date found");

                  GTD_RETURN (-1);
                }
            }
          break;

        case TOKEN_TITLE:
          line_type = GTD_TODO_TXT_LINE_TYPE_TASK;
          break;

        case TOKEN_LIST_COLOR:
          break;

        case TOKEN_LIST_NAME:
          task_list_name_tk = TRUE;
          break;

        case TOKEN_DUE_DATE:
          line_type = GTD_TODO_TXT_LINE_TYPE_TASK;

          if (!is_date (token + strlen ("due:")))
            {
              g_set_error (error,
                           GTD_TODO_TXT_PARSER_ERROR,
                           GTD_TODO_TXT_PARSER_INVALID_DUE_DATE,
                           "Invalid date found");

              GTD_RETURN (-1);
            }

          break;
        
        case TOKEN_NOTES:
          line_type = GTD_TODO_TXT_LINE_TYPE_TASK;
          break;
          
        case TOKEN_IN_DESCRIPTION:
          line_type = GTD_TODO_TXT_LINE_TYPE_TASK;
          break;
          
        case TOKEN_START:
          /* Nothing */
          break;
        }

      last_read = token_id;
    }

  if (!task_list_name_tk)
    {
      g_set_error (error,
                   GTD_TODO_TXT_PARSER_ERROR,
                   GTD_TODO_TXT_PARSER_INVALID_LINE,
                   "No task list found");

      GTD_RETURN (-1);
    }

  GTD_RETURN (line_type);
}
