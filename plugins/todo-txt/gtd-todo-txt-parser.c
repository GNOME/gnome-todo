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

#include "gtd-todo-txt-parser.h"
#include "gtd-provider-todo-txt.h"

#include <glib/gi18n.h>

struct _GtdTodoTxtParser
{
  GtdObject          parent;
};

enum
{
  NONE,
  TASK_COMPLETE,
  TASK_PRIORITY,
  TASK_DATE,
  TASK_TITLE,
  TASK_LIST_NAME,
  ROOT_TASK_NAME,
  TASK_DUE_DATE
};

G_DEFINE_TYPE (GtdTodoTxtParser, gtd_todo_txt_parser, GTD_TYPE_OBJECT);

gint
gtd_todo_txt_parser_get_priority (gchar *token)
{
  gunichar priority;

  priority = g_utf8_get_char (&token[1]);

  switch (priority)
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

GDateTime*
gtd_todo_txt_parser_get_date (gchar *token)
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

gboolean
gtd_todo_txt_parser_is_date (gchar *dt)
{
  GDate date;

  g_date_clear (&date, 1);
  g_date_set_parse (&date, dt);

  return g_date_valid (&date);
}

gboolean
gtd_todo_txt_parser_is_word (gchar *token)
{
  guint token_length;

  token_length = g_utf8_strlen (token, -1);

  return g_utf8_validate (token, -1, NULL);
}

static gint
get_token_id (gchar *token,
              gint last_read,
              gboolean title_read)
{
  gint token_length;

  token_length = strlen (token);

  if (!g_strcmp0 (token, "x") &&
      last_read == NONE)
    return TASK_COMPLETE;

  if (token_length == 3 && token[0] == '(' && token[2] == ')')
    return TASK_PRIORITY;

  if (token_length > 1 && g_str_has_prefix (token , "@"))
    return TASK_LIST_NAME;

  if (token_length > 1 && g_str_has_prefix (token , "+"))
    return ROOT_TASK_NAME;

  if (g_str_has_prefix (token , "due:") && gtd_todo_txt_parser_is_date (token))
    return TASK_DUE_DATE;

  if (gtd_todo_txt_parser_is_date (token))
    return TASK_DATE;

  if ((!title_read && gtd_todo_txt_parser_is_word (token)) ||
      title_read && last_read == TASK_TITLE &&
      gtd_todo_txt_parser_is_word (token))
    {
      return TASK_TITLE;
    }

  return -1;
}

void
gtd_todo_txt_parser_parse_tokens (GtdTask *task,
                                  GList   *tokens)
{
  GDateTime *dt;
  GString *list_name;
  GString *title;
  GString *root_task_name;
  GList *l;
  gboolean is_subtask;
  gint last_read_token;
  gboolean title_read;
  gint token_id;

  l = NULL;
  dt = NULL;
  is_subtask = FALSE;
  title_read = FALSE;
  title = g_string_new (NULL);
  list_name = g_string_new (NULL);
  root_task_name = g_string_new (NULL);

  last_read_token =  NONE;

  for (l = tokens; l != NULL; l = l->next)
    {

      gchar *str;
      gchar *utf_str;

      g_strstrip (l->data);
      str = l->data;

      utf_str =   g_utf8_make_valid (str, -1);

      token_id = get_token_id (utf_str, last_read_token, title_read);

      switch (token_id)
        {
        case TASK_COMPLETE:
          last_read_token = TASK_COMPLETE;
          gtd_task_set_complete (task, TRUE);
          break;

        case TASK_PRIORITY:
          last_read_token = TASK_PRIORITY;
          gtd_task_set_priority (task, gtd_todo_txt_parser_get_priority (l->data));
          break;

        case TASK_DATE:
          last_read_token = TASK_DATE;
          dt = gtd_todo_txt_parser_get_date (l->data);
          break;

        case TASK_TITLE:
          last_read_token = TASK_TITLE;
          title_read = TRUE;
          g_string_append (title, l->data);
          g_string_append (title, " ");
          break;

        case TASK_LIST_NAME:
          last_read_token = TASK_LIST_NAME;
          g_string_append (list_name, l->data);
          g_string_append (list_name, " ");
          break;

        case ROOT_TASK_NAME:
          last_read_token = ROOT_TASK_NAME;
          is_subtask = TRUE;
          g_string_append (root_task_name, l->data);
          g_string_append (root_task_name, " ");
          break;

        case TASK_DUE_DATE:
          last_read_token = TASK_DUE_DATE;
          dt = gtd_todo_txt_parser_get_date (&str[4]);
          gtd_task_set_due_date (task, dt);
          break;

        default:
          return;
        }
    }

  g_strstrip (title->str);
  g_strstrip (list_name->str);
  g_strstrip (root_task_name->str);
  gtd_task_set_title (task, title->str);
  g_object_set_data_full (G_OBJECT (task), "list_name", g_strdup (list_name->str + 1), g_free);

  if (is_subtask)
    g_object_set_data_full (G_OBJECT (task), "root_task_name", g_strdup (root_task_name->str + 1), g_free);

  g_string_free (root_task_name, TRUE);
  g_string_free (list_name, TRUE);
  g_string_free (title, TRUE);
}

gboolean
gtd_todo_txt_parser_validate_token_format (GList *tokens)
{
  GList *it = NULL;
  gint token_id;
  gint position = 0;

  gboolean complete_tk = FALSE;
  gboolean priority_tk = FALSE;
  gboolean task_list_name_tk = FALSE;
  gboolean title_read = FALSE;

  gint last_read = TASK_COMPLETE;

  for (it = tokens; it != NULL; it = it->next)
    {
      gchar *str;

      str = it->data;
      token_id = get_token_id (it->data, last_read, title_read);
      position++;

      switch (token_id)
        {
        case TASK_COMPLETE:
          last_read = TASK_COMPLETE;
          break;

        case TASK_PRIORITY:
          last_read = TASK_PRIORITY;
          priority_tk = TRUE;

          break;

        case TASK_DATE:
          last_read = TASK_DATE;

          if (!gtd_todo_txt_parser_is_date (it->data))
            {
              gtd_manager_emit_error_message (gtd_manager_get_default (),
                                             _("Incorrect date"),
                                             _("Please make sure the date in Todo.txt is valid."),
                                             NULL,
                                             NULL);
              return FALSE;
            }

          break;

        case TASK_TITLE:
          last_read = TASK_TITLE;
          title_read = TRUE;
          break;

        case TASK_LIST_NAME:
          task_list_name_tk = TRUE;
          last_read = TASK_LIST_NAME;
          break;

        case ROOT_TASK_NAME:
          last_read = ROOT_TASK_NAME;
          break;

        case TASK_DUE_DATE:
          last_read = TASK_DUE_DATE;

          if (!gtd_todo_txt_parser_is_date (&str[4]))
            return FALSE;

          break;

        default:
          gtd_manager_emit_error_message (gtd_manager_get_default (),
                                          _("Unrecognized token in a Todo.txt line"),
                                          _("To Do cannot recognize some tags in your Todo.txt file. Some tasks may not be loaded"),
                                          NULL,
                                          NULL);
          return FALSE;
          break;
        }
    }

  if (!task_list_name_tk)
    {
      gtd_manager_emit_error_message (gtd_manager_get_default (),
                                      _("No task list found for some tasks"),
                                      _("Some of the tasks in your Todo.txt file do not have a task list. To Do supports tasks with a task list. Please add a list to all your tasks"),
                                      NULL,
                                      NULL);
      return FALSE;
    }

  return TRUE;
}

GList*
gtd_todo_txt_parser_tokenize (const gchar *line)
{
  GList *tokens = NULL;
  g_autofree GStrv token = NULL;
  gsize i;

  token = g_strsplit (line, " ", -1);

  for (i = 0; token[i]; i++)
    {
      g_strstrip (token[i]);
      tokens = g_list_prepend (tokens, g_strdup (token[i]));
    }

  tokens = g_list_reverse (tokens);

  return tokens;
}

gchar*
gtd_todo_txt_parser_serialize_list (GtdTaskList *list)
{
  GString *description;
  const gchar   *list_name;

  description = g_string_new (NULL);
  list_name = gtd_task_list_get_name (list);

  g_string_append (description, "@");
  g_string_append (description, list_name);

  g_string_append (description, "\n");

  return g_string_free (description, FALSE);
}

gchar*
gtd_todo_txt_parser_serialize_task (GtdTask *task)
{
  GtdTaskList *list;
  GDateTime   *dt;
  GtdTask     *parent;
  GString     *description;
  const gchar *list_name;
  const gchar *title;
  gint priority;
  gboolean is_complete;

  description = g_string_new (NULL);

  is_complete = gtd_task_get_complete (task);
  title = gtd_task_get_title (task);
  priority = gtd_task_get_priority (task);
  dt = gtd_task_get_due_date (task);
  list = gtd_task_get_list (task);
  parent = gtd_task_get_parent (task);

  list_name = gtd_task_list_get_name (list);

  if (is_complete)
    g_string_append (description, "x ");

  if (priority)
    {
      if (priority == 1)
        g_string_append (description, "(C) ");
      else if (priority == 2)
        g_string_append (description, "(B) ");
      else if (priority == 3)
        g_string_append (description, "(A) ");
    }

  g_string_append (description, title);
  g_string_append (description, " @");
  g_string_append (description, list_name);

  if (parent)
    {
      g_string_append (description, " +");
      g_string_append (description, gtd_task_get_title (parent));
    }

  if (dt)
    {
      g_autofree gchar *formatted_time;

      formatted_time = g_date_time_format (dt, "%F");

      g_string_append (description, " due:");
      g_string_append (description, formatted_time);
    }

  g_string_append (description, "\n");

  return g_string_free (description, FALSE);
}

static void
gtd_todo_txt_parser_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
gtd_todo_txt_parser_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
gtd_todo_txt_parser_class_init (GtdTodoTxtParserClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = gtd_todo_txt_parser_get_property;
  object_class->set_property = gtd_todo_txt_parser_set_property;

}

static void
gtd_todo_txt_parser_init (GtdTodoTxtParser *self)
{
  ;
}
