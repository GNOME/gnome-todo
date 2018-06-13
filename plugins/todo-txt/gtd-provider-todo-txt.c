/* gtd-provider-todo-txt.c
 *
 * Copyright © 2016 Rohit Kaushik <kaushikrohit325@gmail.com>
 *             2018 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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

#define G_LOG_DOMAIN "GtdProviderTodoTxt"

#include "gtd-debug.h"
#include "gtd-provider-todo-txt.h"
#include "gtd-plugin-todo-txt.h"
#include "gtd-todo-txt-parser.h"
#include "gtd-task-todo-txt.h"

#include <string.h>
#include <stdlib.h>
#include <glib/gi18n.h>


struct _GtdProviderTodoTxt
{
  GtdObject           parent;

  GIcon              *icon;

  GHashTable         *lists;

  GFileMonitor       *monitor;
  GFile              *source_file;

  GList              *task_lists;
  GPtrArray          *cache;

  GRand              *random_gen;

  guint64             task_counter;
  gboolean            should_reload;
};

static void          on_file_monitor_changed_cb                  (GFileMonitor       *monitor,
                                                                  GFile              *first,
                                                                  GFile              *second,
                                                                  GFileMonitorEvent   event,
                                                                  GtdProviderTodoTxt *self);

static void          gtd_provider_iface_init                     (GtdProviderInterface *iface);

G_DEFINE_TYPE_WITH_CODE (GtdProviderTodoTxt, gtd_provider_todo_txt, GTD_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GTD_TYPE_PROVIDER, gtd_provider_iface_init))

enum
{
  PROP_0,
  PROP_DEFAULT_TASKLIST,
  PROP_DESCRIPTION,
  PROP_ENABLED,
  PROP_ICON,
  PROP_ID,
  PROP_NAME,
  PROP_PROVIDER_TYPE,
  PROP_SOURCE,
  LAST_PROP
};

typedef gboolean (*GtdLineParserFunc) (GtdProviderTodoTxt *self,
                                       const gchar        *line,
                                       GError            **error);

/*
 * Auxiliary methods
 */

static void
print_task (GString *output,
            GtdTask *task)
{
  GtdTaskList *list;
  GDateTime *dt;
  GDateTime *completion_dt;
  GDateTime *creation_dt;
  const gchar *description;
  const gchar *uid;
  gint priority;
  gboolean is_complete;

  is_complete = gtd_task_get_complete (task);
  priority = gtd_task_get_priority (task);
  dt = gtd_task_get_due_date (task);
  list = gtd_task_get_list (task);
  description = gtd_task_get_description (task);
  creation_dt = gtd_task_get_creation_date (task);
  completion_dt = gtd_task_get_completion_date (task);
  uid = gtd_object_get_uid (GTD_OBJECT (task));

  if (is_complete)
    g_string_append (output, "x ");

  if (priority)
    {
      if (priority == 1)
        g_string_append (output, "(C) ");
      else if (priority == 2)
        g_string_append (output, "(B) ");
      else if (priority == 3)
        g_string_append (output, "(A) ");
    }

  if (is_complete && completion_dt)
    {
      g_autofree gchar *formatted_time = g_date_time_format (completion_dt, "%F");
      g_string_append_printf (output, "%s ", formatted_time);
    }

  /*
   * Creation date is only specified if completion date is specified
   * and it comes just after completion date as per todo.txt format
   */
  if (is_complete && completion_dt && creation_dt)
    {
      g_autofree gchar *formatted_time = g_date_time_format (creation_dt, "%F");
      g_string_append_printf (output, "%s ", formatted_time);
    }

  g_string_append_printf (output,
                          "%s @%s",
                          gtd_task_get_title (task),
                          gtd_task_list_get_name (list));

  if (dt)
    {
      g_autofree gchar *formatted_time = g_date_time_format (dt, "%F");
      g_string_append_printf (output, " due:%s", formatted_time);
    }

  if (description && g_strcmp0 (description, "") != 0)
    {
      g_autofree gchar *new_description = g_strescape (description, NULL);
      g_string_append_printf (output, " note:\"%s\"", new_description);
    }

  if (uid && uid[0] != '\0')
    g_string_append_printf (output, " task-uid:%s", uid);

  g_string_append (output, "\n");
}

static gchar*
rgba_to_hex (GdkRGBA *color)
{
  g_autofree gchar *color_str = NULL;
  gint r;
  gint g;
  gint b;

  color_str = gdk_rgba_to_string (color);
  sscanf (color_str, "rgb(%d,%d,%d)", &r, &g, &b);

  return g_strdup_printf ("#%02x%02x%02x", r, g, b);
}

static void
assign_task_uid (GtdProviderTodoTxt *self,
                 GtdTask            *task)
{
  guint NUMBER_OF_LETTER_IN_UID = 5;
  const gchar *title;
  gchar first_five_char[NUMBER_OF_LETTER_IN_UID + 1];
  GString *uid;
  guint i;

  uid = g_string_new ("");
  title = gtd_task_get_title (task);

  /*
   * If the title length is less than 5 then use
   * complete title as first part of uid
   */
  if (strlen (title) <= NUMBER_OF_LETTER_IN_UID)
    {
      g_string_append_printf (uid, "%s%d", title, g_rand_int (self->random_gen));
    }
  else
    {
      for (i = 0; i < NUMBER_OF_LETTER_IN_UID; i++)
        first_five_char[i] = title[i];

      first_five_char[NUMBER_OF_LETTER_IN_UID] = '\0';

      g_string_append_printf (uid, "%s%d", first_five_char, g_rand_int (self->random_gen));
    }

  gtd_object_set_uid (GTD_OBJECT (task), uid->str);
}

static void
update_source (GtdProviderTodoTxt *self)
{
  g_autofree gchar *output_path = NULL;
  g_autoptr (GString) contents = NULL;
  g_autoptr (GString) color_line = NULL;
  g_autoptr (GString) list_line = NULL;
  g_autoptr (GError) error = NULL;
  GtdTaskList *list;
  guint i;

  GTD_ENTRY;

  contents = g_string_new ("");
  color_line = g_string_new ("");
  list_line = g_string_new ("");

  /* Save the tasks first */
  for (i = 0; i < self->cache->len; i++)
    {
      g_autoptr (GList) tasks = NULL;
      g_autoptr (GList) l = NULL;

      list = g_ptr_array_index (self->cache, i);
      tasks = gtd_task_list_get_tasks (list);

      /* And now each task */
      for (l = tasks; l; l = l->next)
        {
          const gchar *uid;

          uid = gtd_object_get_uid (l->data);

          /* First set uid if not set already */
          if (!uid || uid[0] == '\0')
            assign_task_uid (self, l->data);

          print_task (contents, l->data);
        }
    }

  /* Initialize lists & colors custom lines */
  g_string_append_printf (list_line, "h:1 Lists ");
  g_string_append_printf (color_line, "h:1 Colors ");

  /* Then the task lists and color */
  for (i = 0; i < self->cache->len; i++)
    {
      g_autofree gchar *color_str = NULL;
      g_autoptr (GdkRGBA) color = NULL;
      const gchar *list_name;

      list = g_ptr_array_index (self->cache, i);
      list_name = gtd_task_list_get_name (list);
      color = gtd_task_list_get_color (list);
      color_str = rgba_to_hex (color);

      g_string_append_printf (list_line, "@%s", list_name);
      g_string_append_printf (color_line, "%s:%s", list_name, color_str);

      if (i < self->cache->len - 1)
        {
          g_string_append (list_line, " ");
          g_string_append (color_line, " ");
        }
    }

  /* Append newline to end of custom lines */
  g_string_append (list_line, "\n");
  g_string_append (color_line, "\n");

  /* Finally add custom lines to todo.txt content */
  g_string_append (contents, list_line->str);
  g_string_append (contents, color_line->str);

  output_path = g_file_get_path (self->source_file);
  g_file_set_contents (output_path, contents->str, contents->len, &error);

  if (error)
    {
      g_warning ("Error saving Todo.txt file: %s", error->message);
      return;
    }

  self->should_reload = FALSE;

  GTD_EXIT;
}

static void
add_task_list (GtdProviderTodoTxt *self,
               GtdTaskList        *list)
{
  if (g_hash_table_contains (self->lists, gtd_task_list_get_name (list)))
    return;

  g_ptr_array_add (self->cache, list);
  g_hash_table_insert (self->lists, g_strdup (gtd_task_list_get_name (list)), list);

  self->task_lists = g_list_append (self->task_lists, list);
}

static gboolean
parse_lists_line (GtdProviderTodoTxt *self,
                  const gchar        *line,
                  GError            **error)
{
  g_autoptr (GPtrArray) lists = NULL;
  guint i;

  lists = gtd_todo_txt_parser_parse_task_lists (GTD_PROVIDER (self), line, error);

  if (!lists)
    return FALSE;

  for (i = 0; i < lists->len; i++)
    add_task_list (self, g_ptr_array_index (lists, i));

  return TRUE;
}

static gboolean
parse_list_colors_line (GtdProviderTodoTxt *self,
                        const gchar        *line,
                        GError            **error)
{
  return gtd_todo_txt_parser_parse_task_list_color (self->lists, line, error);
}

static void
parse_task (GtdProviderTodoTxt *self,
            const gchar        *line)
{
  g_autofree gchar *list_name = NULL;
  GtdTaskList *list;
  GtdTask *task;
  const gchar *uid;

  task = gtd_todo_txt_parser_parse_task (GTD_PROVIDER (self), line, &list_name);

  /*
   * Create the list if it doesn't exist yet; this might happen with todo.txt files
   * that are not saved from GNOME To Do.
   */
  if (!g_hash_table_contains (self->lists, list_name))
    {
      GTD_TRACE_MSG ("Creating new list with name '%s'", list_name);

      list = g_object_new (GTD_TYPE_TASK_LIST,
                           "provider", self,
                           "name", list_name,
                           "is-removable", TRUE,
                           NULL);

      add_task_list (self, list);
    }
  else
    {
      GTD_TRACE_MSG ("List with name '%s' already exists, reusing", list_name);

      list = g_hash_table_lookup (self->lists, list_name);
    }

  uid = gtd_object_get_uid (GTD_OBJECT (task));

  /* If task-uid is null, generate a new uid */
  if (!uid || uid[0] == '\0')
    assign_task_uid (self, task);

  gtd_task_set_list (task, list);
  gtd_task_list_save_task (list, task);

  self->task_counter++;
}

struct
{
  GtdTodoTxtLineType  type;
  const gchar        *identifier;
  GtdLineParserFunc   parse;
} custom_lines_vtable[] =
{
  { GTD_TODO_TXT_LINE_TYPE_TASKLIST,    "Lists",  parse_lists_line },
  { GTD_TODO_TXT_LINE_TYPE_LIST_COLORS, "Colors", parse_list_colors_line }
};

static GPtrArray*
remove_empty_lines (GStrv lines)
{
  g_autoptr (GPtrArray) valid_lines = NULL;
  guint n_lines;
  guint i;

  n_lines = g_strv_length (lines);
  valid_lines = g_ptr_array_sized_new (n_lines);

  for (i = 0; i < n_lines; i++)
    {
      gchar *line;

      line = g_strstrip (lines[i]);

      if (!line || line[0] == '\n' || line[0] == '\0')
        continue;

      g_ptr_array_add (valid_lines, lines[i]);
    }

  return g_steal_pointer (&valid_lines);
}

static void
reload_tasks (GtdProviderTodoTxt *self)
{
  g_autofree gchar *input_path = NULL;
  g_autofree gchar *file_contents = NULL;
  g_autoptr (GPtrArray) valid_lines = NULL;
  g_autoptr (GError) error = NULL;
  g_auto (GStrv) lines = NULL;
  guint vtable_len;
  guint n_lines;
  guint i;

  GTD_ENTRY;

  input_path = g_file_get_path (self->source_file);

  g_debug ("Reading the contents of %s", input_path);

  g_file_get_contents (input_path, &file_contents, NULL, &error);

  if (error)
    {
      g_warning ("Error reading Todo.txt file: %s", error->message);
      return;
    }

  self->task_counter = 0;

  if (g_str_equal (file_contents, ""))
    return;

  lines = g_strsplit (file_contents, "\n", -1);
  valid_lines = remove_empty_lines (lines);
  n_lines = valid_lines->len;
  vtable_len = G_N_ELEMENTS (custom_lines_vtable);

  /* First parse the custom lines at the end of the Todo.txt file, if possible */
  for (i = 0; n_lines >= vtable_len && i < vtable_len; i++)
    {
      g_autoptr (GError) line_error = NULL;
      GtdTodoTxtLineType line_type;
      const gchar *line;
      guint line_number;

      line_number = n_lines - vtable_len + i;
      line = g_ptr_array_index (valid_lines, line_number);

      line_type = gtd_todo_txt_parser_get_line_type (line, &line_error);

      if (line_error)
        {
          g_warning ("Error parsing custom line %d: %s", line_number, line_error->message);
          continue;
        }

      if (custom_lines_vtable[i].type == line_type)
        custom_lines_vtable[i].parse (self, line, &line_error);

      if (line_error)
        {
          g_warning ("Error parsing custom line %d: %s", line_number, line_error->message);
          continue;
        }
    }

  /* Then regular task lines */
  for (i = 0; i < n_lines - vtable_len; i++)
    {
      g_autoptr (GError) line_error = NULL;
      GtdTodoTxtLineType line_type;
      gchar *line;

      line = g_ptr_array_index (valid_lines, i);

      /* Last element of the array is NULL */
      if (!line || line[0] == '\0')
        break;

      GTD_TRACE_MSG ("Parsing line %d: %s", i, line);

      line_type = gtd_todo_txt_parser_get_line_type (line, &line_error);

      if (line_error)
        {
          g_warning ("Error parsing line %d: %s", i + 1, line_error->message);
          continue;
        }

      switch (line_type)
        {
        case GTD_TODO_TXT_LINE_TYPE_TASKLIST:
          break;

        case GTD_TODO_TXT_LINE_TYPE_TASK:
          parse_task (self, line);
          break;

        case GTD_TODO_TXT_LINE_TYPE_LIST_COLORS:
          break;

        case GTD_TODO_TXT_LINE_TYPE_UNKNOWN:
          break;

        default:
          break;
        }
    }

  GTD_EXIT;
}

static void
setup_file_monitor (GtdProviderTodoTxt *self)
{
  g_autoptr (GError) error = NULL;

  self->monitor = g_file_monitor_file (self->source_file,
                                       G_FILE_MONITOR_WATCH_MOVES,
                                       NULL,
                                       &error);

  if (error)
    {
      gtd_manager_emit_error_message (gtd_manager_get_default (),
                                      _("Error while opening the file monitor. Todo.txt will not be monitored"),
                                      error->message,
                                      NULL,
                                      NULL);
      return;
    }

  g_signal_connect (self->monitor, "changed", G_CALLBACK (on_file_monitor_changed_cb), self);
}


/*
 * Callbacks
 */

static void
on_file_monitor_changed_cb (GFileMonitor       *monitor,
                            GFile              *first,
                            GFile              *second,
                            GFileMonitorEvent   event,
                            GtdProviderTodoTxt *self)
{
  GList *l = NULL;
  guint i;

  if (!self->should_reload)
    {
      self->should_reload = TRUE;
      return;
    }

  g_clear_pointer (&self->lists, g_hash_table_destroy);
  g_ptr_array_free (self->cache, TRUE);

  for (l = self->task_lists; l != NULL; l = l->next)
    g_signal_emit_by_name (self, "list-removed", l->data);

  g_list_free (self->task_lists);

  self->task_lists = NULL;
  self->lists = g_hash_table_new ((GHashFunc) g_str_hash, (GEqualFunc) g_str_equal);
  self->cache = g_ptr_array_new ();

  reload_tasks (self);

  for (i = 0; i < self->cache->len; i++)
    {
      GtdTaskList *list;
      list = g_ptr_array_index (self->cache, i);
      g_signal_emit_by_name (self, "list-added", list);
    }
}


/*
 * GtdProviderInterface implementation
 */

static const gchar*
gtd_provider_todo_txt_get_id (GtdProvider *provider)
{
  return "todo-txt";
}

static const gchar*
gtd_provider_todo_txt_get_name (GtdProvider *provider)
{
  return _("Todo.txt");
}

static const gchar*
gtd_provider_todo_txt_get_provider_type (GtdProvider *provider)
{
  return "todo-txt";
}

static const gchar*
gtd_provider_todo_txt_get_description (GtdProvider *provider)
{
  return _("On the Todo.txt file");
}

static gboolean
gtd_provider_todo_txt_get_enabled (GtdProvider *provider)
{
  return TRUE;
}

static GIcon*
gtd_provider_todo_txt_get_icon (GtdProvider *provider)
{
  GtdProviderTodoTxt *self = GTD_PROVIDER_TODO_TXT (provider);

  return self->icon;
}

static void
gtd_provider_todo_txt_create_task (GtdProvider *provider,
                                   GtdTaskList *list,
                                   const gchar *title,
                                   GDateTime   *due_date)
{
  GtdProviderTodoTxt *self;
  g_autoptr (GtdTask) new_task = NULL;

  self = GTD_PROVIDER_TODO_TXT (provider);

  /* Create the new task */
  new_task = GTD_TASK (gtd_provider_todo_txt_generate_task (self));
  gtd_task_set_due_date (new_task, due_date);
  gtd_task_set_list (new_task, list);
  gtd_task_set_title (new_task, title);
  gtd_task_set_creation_date (new_task, g_date_time_new_now_local ());
  assign_task_uid (self, new_task);

  gtd_task_list_save_task (list, new_task);

  update_source (GTD_PROVIDER_TODO_TXT (provider));
}

static void
gtd_provider_todo_txt_update_task (GtdProvider *provider,
                                   GtdTask     *task)
{
  update_source (GTD_PROVIDER_TODO_TXT (provider));
}

static void
gtd_provider_todo_txt_remove_task (GtdProvider *provider,
                                   GtdTask     *task)
{
  gtd_task_list_remove_task (gtd_task_get_list (task), task);

  update_source (GTD_PROVIDER_TODO_TXT (provider));
}

static void
gtd_provider_todo_txt_create_task_list (GtdProvider *provider,
                                        const gchar *name)
{
  GtdProviderTodoTxt *self;
  GtdTaskList *new_list;

  self = GTD_PROVIDER_TODO_TXT (provider);

  new_list = gtd_task_list_new (provider);
  gtd_task_list_set_name (new_list, name);
  gtd_task_list_set_is_removable (new_list, TRUE);

  self->task_lists = g_list_append (self->task_lists, new_list);
  g_ptr_array_add (self->cache, new_list);
  g_hash_table_insert (self->lists, g_strdup (name), new_list);

  update_source (self);

  g_signal_emit_by_name (provider, "list-added", new_list);
}

static void
gtd_provider_todo_txt_update_task_list (GtdProvider *provider,
                                        GtdTaskList *list)
{
  GtdProviderTodoTxt *self;

  self = GTD_PROVIDER_TODO_TXT (provider);

  g_return_if_fail (GTD_IS_TASK_LIST (list));

  update_source (self);

  g_signal_emit_by_name (provider, "list-changed", list);
}

static void
gtd_provider_todo_txt_remove_task_list (GtdProvider *provider,
                                        GtdTaskList *list)
{
  GtdProviderTodoTxt *self = GTD_PROVIDER_TODO_TXT (provider);

  g_ptr_array_remove (self->cache, list);
  self->task_lists = g_list_remove (self->task_lists, list);

  update_source (self);

  g_signal_emit_by_name (provider, "list-removed", list);
}

static GList*
gtd_provider_todo_txt_get_task_lists (GtdProvider *provider)
{
  GtdProviderTodoTxt *self = GTD_PROVIDER_TODO_TXT (provider);

  return g_list_copy (self->task_lists);
}

static GtdTaskList*
gtd_provider_todo_txt_get_default_task_list (GtdProvider *provider)
{
  return NULL;
}

static void
gtd_provider_todo_txt_set_default_task_list (GtdProvider *provider,
                                             GtdTaskList *list)
{
  /* FIXME: implement me */
}

static void
gtd_provider_iface_init (GtdProviderInterface *iface)
{
  iface->get_id = gtd_provider_todo_txt_get_id;
  iface->get_name = gtd_provider_todo_txt_get_name;
  iface->get_provider_type = gtd_provider_todo_txt_get_provider_type;
  iface->get_description = gtd_provider_todo_txt_get_description;
  iface->get_enabled = gtd_provider_todo_txt_get_enabled;
  iface->get_icon = gtd_provider_todo_txt_get_icon;
  iface->create_task = gtd_provider_todo_txt_create_task;
  iface->update_task = gtd_provider_todo_txt_update_task;
  iface->remove_task = gtd_provider_todo_txt_remove_task;
  iface->create_task_list = gtd_provider_todo_txt_create_task_list;
  iface->update_task_list = gtd_provider_todo_txt_update_task_list;
  iface->remove_task_list = gtd_provider_todo_txt_remove_task_list;
  iface->get_task_lists = gtd_provider_todo_txt_get_task_lists;
  iface->get_default_task_list = gtd_provider_todo_txt_get_default_task_list;
  iface->set_default_task_list = gtd_provider_todo_txt_set_default_task_list;
}


/*
 * GObject overrides
 */

static void
gtd_provider_todo_txt_finalize (GObject *object)
{
  GtdProviderTodoTxt *self = (GtdProviderTodoTxt *)object;

  g_clear_pointer (&self->lists, g_hash_table_destroy);
  g_ptr_array_free (self->cache, TRUE);
  g_clear_pointer (&self->task_lists, g_clear_object);
  g_clear_pointer (&self->random_gen, g_clear_object);
  g_clear_object (&self->source_file);
  g_clear_object (&self->icon);

  G_OBJECT_CLASS (gtd_provider_todo_txt_parent_class)->finalize (object);
}

static void
gtd_provider_todo_txt_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  GtdProvider *provider = GTD_PROVIDER (object);

  switch (prop_id)
    {
    case PROP_DESCRIPTION:
      g_value_set_string (value, gtd_provider_todo_txt_get_description (provider));
      break;

    case PROP_ENABLED:
      g_value_set_boolean (value, gtd_provider_todo_txt_get_enabled (provider));
      break;

    case PROP_ICON:
      g_value_set_object (value, gtd_provider_todo_txt_get_icon (provider));
      break;

    case PROP_ID:
      g_value_set_string (value, gtd_provider_todo_txt_get_id (provider));
      break;

    case PROP_NAME:
      g_value_set_string (value, gtd_provider_todo_txt_get_name (provider));
      break;

    case PROP_PROVIDER_TYPE:
      g_value_set_string (value, gtd_provider_todo_txt_get_provider_type (provider));
      break;

    case PROP_SOURCE:
      g_value_set_object (value, GTD_PROVIDER_TODO_TXT (provider)->source_file);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_provider_todo_txt_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  GtdProviderTodoTxt *self = GTD_PROVIDER_TODO_TXT (object);
  switch (prop_id)
    {
    case PROP_SOURCE:
      self->source_file = g_value_dup_object (value);
      setup_file_monitor (self);
      reload_tasks (self);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_provider_todo_txt_class_init (GtdProviderTodoTxtClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gtd_provider_todo_txt_finalize;
  object_class->get_property = gtd_provider_todo_txt_get_property;
  object_class->set_property = gtd_provider_todo_txt_set_property;

  g_object_class_install_property (object_class,
                                   PROP_SOURCE,
                                   g_param_spec_object ("source",
                                                        "Source file",
                                                        "The Todo.txt source file",
                                                        G_TYPE_FILE,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

  g_object_class_override_property (object_class, PROP_DEFAULT_TASKLIST, "default-task-list");
  g_object_class_override_property (object_class, PROP_DESCRIPTION, "description");
  g_object_class_override_property (object_class, PROP_ENABLED, "enabled");
  g_object_class_override_property (object_class, PROP_ICON, "icon");
  g_object_class_override_property (object_class, PROP_ID, "id");
  g_object_class_override_property (object_class, PROP_NAME, "name");
  g_object_class_override_property (object_class, PROP_PROVIDER_TYPE, "provider-type");
}

static void
gtd_provider_todo_txt_init (GtdProviderTodoTxt *self)
{
  self->random_gen = g_rand_new_with_seed (42);
  self->lists = g_hash_table_new (g_str_hash, g_str_equal);
  self->cache = g_ptr_array_new ();
  self->should_reload = TRUE;
  self->icon = G_ICON (g_themed_icon_new_with_default_fallbacks ("computer-symbolic"));
}

GtdProviderTodoTxt*
gtd_provider_todo_txt_new (GFile *source_file)
{

  return g_object_new (GTD_TYPE_PROVIDER_TODO_TXT,
                       "source", source_file,
                       NULL);
}

GtdTaskTodoTxt*
gtd_provider_todo_txt_generate_task (GtdProviderTodoTxt *self)
{
  g_return_val_if_fail (GTD_IS_PROVIDER_TODO_TXT (self), NULL);

  return g_object_new (GTD_TYPE_TASK_TODO_TXT, NULL);
}
