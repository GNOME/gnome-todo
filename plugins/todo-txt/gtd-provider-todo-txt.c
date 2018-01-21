/* gtd-provider-todo-txt.c
 *
 * Copyright (C) 2016 Rohit Kaushik <kaushikrohit325@gmail.com>
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

#include "gtd-provider-todo-txt.h"
#include "gtd-plugin-todo-txt.h"
#include "gtd-todo-txt-parser.h"

#include <string.h>
#include <stdlib.h>
#include <glib/gi18n.h>


struct _GtdProviderTodoTxt
{
  GtdObject          parent;

  GIcon              *icon;

  GHashTable         *lists;
  GHashTable         *tasks;

  GFileMonitor       *monitor;
  GFile              *source_file;

  GList              *task_lists;
  GPtrArray          *cache;
  gboolean            should_reload;
};

static void          gtd_provider_iface_init                     (GtdProviderInterface *iface);

G_DEFINE_TYPE_WITH_CODE (GtdProviderTodoTxt, gtd_provider_todo_txt, GTD_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GTD_TYPE_PROVIDER,
                                                gtd_provider_iface_init))

enum {
  PROP_0,
  PROP_DEFAULT_TASKLIST,
  PROP_DESCRIPTION,
  PROP_ENABLED,
  PROP_ICON,
  PROP_ID,
  PROP_NAME,
  PROP_SOURCE,
  LAST_PROP
};

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
  GtdProviderTodoTxt *self;

  self = GTD_PROVIDER_TODO_TXT (provider);

  return self->icon;
}

static void
emit_generic_error (GError *error)
{
  g_warning ("%s: %s: %s",
             G_STRFUNC,
             "Error while opening Todo.txt",
             error->message);

  gtd_manager_emit_error_message (gtd_manager_get_default (),
                                  _("Error while opening Todo.txt"),
                                  error->message,
                                  NULL,
                                  NULL);
}

static void
update_source (GtdProviderTodoTxt *self)
{

  GFileOutputStream *write_stream;
  GDataOutputStream *writer;
  GtdTaskList *list;
  GError *error;
  GList *tasks, *l;
  guint i;

  error = NULL;
  tasks = NULL;
  l = NULL;
  self->should_reload = FALSE;

  write_stream = g_file_replace (self->source_file,
                                 NULL,
                                 TRUE,
                                 G_FILE_CREATE_NONE,
                                 NULL,
                                 &error);
  if (error)
    {
      emit_generic_error (error);
      g_error_free (error);
      return;
    }

  writer = g_data_output_stream_new (G_OUTPUT_STREAM (write_stream));

  for (i = 0; i < self->cache->len; i++)
    {
      gchar *list_line;
      list = g_ptr_array_index (self->cache, i);

      tasks = gtd_task_list_get_tasks (list);
      tasks = g_list_sort (tasks, (GCompareFunc) gtd_task_compare);

      list_line = gtd_todo_txt_parser_serialize_list (list);

      g_data_output_stream_put_string (writer,
                                       list_line,
                                       NULL,
                                       NULL);

      for (l = tasks; l != NULL; l = l->next)
        {
          gchar *task_line;

          task_line = gtd_todo_txt_parser_serialize_task (l->data);

          g_data_output_stream_put_string (writer,
                                           task_line,
                                           NULL,
                                           NULL);

          g_free (task_line);
        }

      g_free (list_line);
    }

  g_output_stream_close (G_OUTPUT_STREAM (writer), NULL, NULL);
  g_output_stream_close (G_OUTPUT_STREAM (write_stream), NULL, NULL);
}

static GtdTaskList*
create_list (GtdProviderTodoTxt *self,
             gchar              *name)
{
  GtdTaskList *task_list;

  if (g_hash_table_contains (self->lists, name))
    return g_hash_table_lookup (self->lists, name);

  task_list = gtd_task_list_new (GTD_PROVIDER (self));
  gtd_task_list_set_is_removable (task_list, TRUE);
  g_ptr_array_add (self->cache, task_list);
  g_hash_table_insert (self->lists, g_strdup (name), task_list);
  gtd_task_list_set_name (task_list, name);
  self->task_lists = g_list_append (self->task_lists, task_list);

  return task_list;
}

static void
gtd_provider_todo_txt_load_tasks (GtdProviderTodoTxt *self)
{
  GFileInputStream *readstream;
  GDataInputStream *reader;
  GtdTaskList *list;
  GtdTask *parent_task;
  GtdTask *task;
  GError *error;
  GList *tokens;
  gboolean valid;
  gchar *line_read;
  gchar *list_name;
  gchar *root_task_name;

  g_return_if_fail (G_IS_FILE (self->source_file));

  tokens = NULL;
  error = NULL;
  readstream = g_file_read (self->source_file, NULL, &error);

  if (error)
    {
      emit_generic_error (error);
      g_error_free (error);
      return;
    }


  reader = g_data_input_stream_new (G_INPUT_STREAM (readstream));

  while (!error)
    {
      line_read = g_data_input_stream_read_line (reader, NULL, NULL, &error);

      if (error)
        {
          g_warning ("%s: %s: %s",
                     G_STRFUNC,
                     "Error while reading a line from Todo.txt",
                     error->message);

          gtd_manager_emit_error_message (gtd_manager_get_default (),
                                          _("Error while reading a line from Todo.txt"),
                                          error->message,
                                          NULL,
                                          NULL);
          g_error_free (error);

          continue;
        }

      if (!line_read)
        break;

      g_strstrip (line_read);
      tokens = gtd_todo_txt_parser_tokenize (line_read);
      valid = gtd_todo_txt_parser_validate_token_format (tokens);

      if (valid)
        {
          if (g_list_length (tokens) == 1)
            {
              list_name = &((gchar*)tokens->data)[0];
              list_name++;
              create_list (self, list_name);
              continue;
            }

          task = gtd_provider_generate_task (GTD_PROVIDER (self));
          gtd_todo_txt_parser_parse_tokens (task, tokens);

          g_hash_table_insert (self->tasks, g_strdup (gtd_task_get_title (task)), task);

          list = create_list (self, g_object_get_data (G_OBJECT (task), "list_name"));
          gtd_task_set_list (task, list);

          if (g_object_get_data (G_OBJECT (task), "root_task_name"))
            {
              root_task_name = g_object_get_data (G_OBJECT (task), "root_task_name");

              if (g_hash_table_contains (self->tasks, root_task_name))
                {
                  parent_task =  g_hash_table_lookup (self->tasks, root_task_name);
                }
              else
                {
                  parent_task = gtd_provider_generate_task (GTD_PROVIDER (self));
                  gtd_task_set_list (parent_task, list);
                  gtd_task_set_title (parent_task, g_object_get_data (G_OBJECT (task), "root_task_name"));

                  g_hash_table_insert (self->tasks, root_task_name, parent_task);
                }

              gtd_task_add_subtask (parent_task, task);
              gtd_task_list_save_task (list, parent_task);
            }

          gtd_task_list_save_task (list, task);
        }

      g_list_free_full (tokens, g_free);
      g_free (line_read);
    }

  g_input_stream_close (G_INPUT_STREAM (reader), NULL, NULL);
  g_input_stream_close (G_INPUT_STREAM (readstream), NULL, NULL);
}

static void
gtd_provider_todo_txt_reload (GFileMonitor       *monitor,
                              GFile              *first,
                              GFile              *second,
                              GFileMonitorEvent   event,
                              GtdProviderTodoTxt *self)
{
  GList *l;
  guint i;

  l = NULL;

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

  gtd_provider_todo_txt_load_tasks (self);

  for (i = 0; i < self->cache->len; i++)
    {
      GtdTaskList *list;
      list = g_ptr_array_index (self->cache, i);
      g_signal_emit_by_name (self, "list-added", list);
    }
}

static void
gtd_provider_todo_txt_load_source_monitor (GtdProviderTodoTxt *self)
{
  GError *error = NULL;

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
      g_clear_error (&error);
      return;
    }

  g_signal_connect (self->monitor, "changed", G_CALLBACK (gtd_provider_todo_txt_reload), self);
}

static void
gtd_provider_todo_txt_create_task (GtdProvider *provider,
                                   GtdTask     *task)
{
  GtdProviderTodoTxt *self;

  self = GTD_PROVIDER_TODO_TXT (provider);

  g_return_if_fail (GTD_IS_TASK (task));
  g_return_if_fail (GTD_IS_TASK_LIST (gtd_task_get_list (task)));

  update_source (self);
}

static void
gtd_provider_todo_txt_update_task (GtdProvider *provider,
                                   GtdTask     *task)
{
  GtdProviderTodoTxt *self;

  self = GTD_PROVIDER_TODO_TXT (provider);

  g_return_if_fail (GTD_IS_TASK (task));
  g_return_if_fail (GTD_IS_TASK_LIST (gtd_task_get_list (task)));
  g_return_if_fail (G_IS_FILE (self->source_file));

  update_source (self);
}

static void
gtd_provider_todo_txt_remove_task (GtdProvider *provider,
                                   GtdTask     *task)
{
  GtdProviderTodoTxt *self;

  self = GTD_PROVIDER_TODO_TXT (provider);

  g_return_if_fail (GTD_IS_TASK (task));
  g_return_if_fail (GTD_IS_TASK_LIST (gtd_task_get_list (task)));
  g_return_if_fail (G_IS_FILE (self->source_file));

  update_source (self);
}

static void
gtd_provider_todo_txt_create_task_list (GtdProvider *provider,
                                        GtdTaskList *list)
{
  GtdProviderTodoTxt *self;
  gchar *name;
  g_return_if_fail (GTD_IS_TASK_LIST (list));

  self = GTD_PROVIDER_TODO_TXT (provider);
  name = g_strdup (gtd_task_list_get_name (list));
  gtd_task_list_set_is_removable (list, TRUE);

  self->task_lists = g_list_append (self->task_lists, list);
  g_ptr_array_add (self->cache, list);
  g_hash_table_insert (self->lists, name, list);

  update_source (self);

  g_signal_emit_by_name (provider, "list-added", list);
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
  GtdProviderTodoTxt *self;

  self = GTD_PROVIDER_TODO_TXT (provider);

  g_return_if_fail (GTD_IS_TASK_LIST (list));

  g_ptr_array_remove (self->cache, list);
  self->task_lists = g_list_remove (self->task_lists, list);

  update_source (self);

  g_signal_emit_by_name (provider, "list-removed", list);
}

static GList*
gtd_provider_todo_txt_get_task_lists (GtdProvider *provider)
{
  GtdProviderTodoTxt *self;

  self = GTD_PROVIDER_TODO_TXT (provider);

  return self->task_lists;
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

GtdProviderTodoTxt*
gtd_provider_todo_txt_new (GFile *source_file)
{

  return g_object_new (GTD_TYPE_PROVIDER_TODO_TXT,
                       "source", source_file,
                       NULL);
}

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
      gtd_provider_todo_txt_load_source_monitor (self);
      gtd_provider_todo_txt_load_tasks (self);
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
                                                         G_TYPE_OBJECT,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

  g_object_class_override_property (object_class, PROP_DEFAULT_TASKLIST, "default-task-list");
  g_object_class_override_property (object_class, PROP_DESCRIPTION, "description");
  g_object_class_override_property (object_class, PROP_ENABLED, "enabled");
  g_object_class_override_property (object_class, PROP_ICON, "icon");
  g_object_class_override_property (object_class, PROP_ID, "id");
  g_object_class_override_property (object_class, PROP_NAME, "name");
}

static void
gtd_provider_todo_txt_init (GtdProviderTodoTxt *self)
{
  gtd_object_set_ready (GTD_OBJECT (self), TRUE);

  self->lists = g_hash_table_new ((GHashFunc) g_str_hash, (GEqualFunc) g_str_equal);
  self->tasks = g_hash_table_new ((GHashFunc) g_str_hash, (GEqualFunc) g_str_equal);
  self->cache = g_ptr_array_new ();
  self->should_reload = TRUE;

  /* icon */
  self->icon = G_ICON (g_themed_icon_new_with_default_fallbacks ("computer-symbolic"));
}
