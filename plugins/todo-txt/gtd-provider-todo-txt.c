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

#include <string.h>
#include <stdlib.h>
#include <glib/gi18n.h>


struct _GtdProviderTodoTxt
{
  GtdObject           parent;

  GIcon              *icon;

  GHashTable         *lists;
  GHashTable         *tasks;

  GFileMonitor       *monitor;
  GFile              *source_file;

  GList              *task_lists;
  GPtrArray          *cache;

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


/*
 * Auxiliary methods
 */

static void
print_task (GString *output,
            GtdTask *task)
{
  GtdTaskList *list;
  GDateTime *dt;
  const gchar *description;
  gint priority;
  gboolean is_complete;

  is_complete = gtd_task_get_complete (task);
  priority = gtd_task_get_priority (task);
  dt = gtd_task_get_due_date (task);
  list = gtd_task_get_list (task);
  description = gtd_task_get_description (task);

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

  g_string_append (output, "\n");
}

static void
update_source (GtdProviderTodoTxt *self)
{
  g_autofree gchar *output_path = NULL;
  g_autoptr (GString) contents = NULL;
  g_autoptr (GError) error = NULL;
  GtdTaskList *list;
  guint i;

  GTD_ENTRY;

  contents = g_string_new ("");

  /* Save the tasks first */
  for (i = 0; i < self->cache->len; i++)
    {
      g_autoptr (GList) tasks = NULL;
      g_autoptr (GList) l = NULL;

      list = g_ptr_array_index (self->cache, i);
      tasks = gtd_task_list_get_tasks (list);

      /* And now each task */
      for (l = tasks; l; l = l->next)
        print_task (contents, l->data);
    }

  /* Then the task lists */
  for (i = 0; i < self->cache->len; i++)
    {
      g_autofree gchar *color_str = NULL;
      g_autoptr (GdkRGBA) color = NULL;

      list = g_ptr_array_index (self->cache, i);

      /* Print the list as the first line */
      color = gtd_task_list_get_color (list);
      color_str = gdk_rgba_to_string (color);

      g_string_append_printf (contents,
                              "h:1 @%s color:%s\n",
                              gtd_task_list_get_name (list),
                              color_str);
    }

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

static void
parse_task_list (GtdProviderTodoTxt *self,
                 const gchar        *line)
{
  g_autoptr (GtdTaskList) list = NULL;

  list = gtd_todo_txt_parser_parse_task_list (GTD_PROVIDER (self), line);

  if (!list)
    return;

  add_task_list (self, g_steal_pointer (&list));
}

static void
parse_task (GtdProviderTodoTxt *self,
            const gchar        *line)
{
  g_autofree gchar *list_name = NULL;
  GtdTaskList *list;
  GtdTask *task;

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

  gtd_task_set_list (task, list);
  gtd_task_list_save_task (list, task);

  g_hash_table_insert (self->tasks, (gpointer) gtd_object_get_uid (GTD_OBJECT (task)), task);
  self->task_counter++;
}

static void
reload_tasks (GtdProviderTodoTxt *self)
{
  g_autofree gchar *input_path = NULL;
  g_autofree gchar *file_contents = NULL;
  g_autoptr (GError) error = NULL;
  g_auto (GStrv) lines = NULL;
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

  lines = g_strsplit (file_contents, "\n", -1);

  for (i = 0; lines && lines[i]; i++)
    {
      GtdTodoTxtLineType line_type;
      gchar *line;

      line = lines[i];

      /* Last element of the array is NULL */
      if (!line || line[0] == '\0')
        break;

      g_strstrip (line);

      GTD_TRACE_MSG ("Parsing line %d: %s", i, line);

      line_type = gtd_todo_txt_parser_get_line_type (line, &error);

      if (error)
        {
          g_warning ("Error parsing line %d: %s", i + 1, error->message);
          g_clear_error (&error);
          continue;
        }

      switch (line_type)
        {
        case GTD_TODO_TXT_LINE_TYPE_TASKLIST:
          parse_task_list (self, line);
          break;

        case GTD_TODO_TXT_LINE_TYPE_TASK:
          parse_task (self, line);
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
  g_clear_pointer (&self->tasks, g_hash_table_destroy);
  g_ptr_array_free (self->cache, TRUE);

  for (l = self->task_lists; l != NULL; l = l->next)
    g_signal_emit_by_name (self, "list-removed", l->data);

  g_list_free (self->task_lists);

  self->task_lists = NULL;
  self->lists = g_hash_table_new ((GHashFunc) g_str_hash, (GEqualFunc) g_str_equal);
  self->tasks = g_hash_table_new ((GHashFunc) g_str_hash, (GEqualFunc) g_str_equal);
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
  new_task = gtd_provider_todo_txt_generate_task (self);
  gtd_task_set_due_date (new_task, due_date);
  gtd_task_set_list (new_task, list);
  gtd_task_set_title (new_task, title);

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
  g_clear_pointer (&self->tasks, g_hash_table_destroy);
  g_ptr_array_free (self->cache, TRUE);
  g_clear_pointer (&self->task_lists, g_clear_object);
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
  self->lists = g_hash_table_new (g_str_hash, g_str_equal);
  self->tasks = g_hash_table_new (g_str_hash, g_str_equal);
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

GtdTask*
gtd_provider_todo_txt_generate_task (GtdProviderTodoTxt *self)
{
  g_autofree gchar *uid = NULL;

  g_return_val_if_fail (GTD_IS_PROVIDER_TODO_TXT (self), NULL);
  uid = g_uuid_string_random ();

  return g_object_new (GTD_TYPE_TASK, "uid", uid, NULL);
}
