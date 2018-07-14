/* gtd-provider-todoist.c
 *
 * Copyright (C) 2017 Rohit Kaushik <kaushikrohit325@gmail.com>
 *               2018 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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

#define G_LOG_DOMAIN "GtdProviderTodoist"

#define _XOPEN_SOURCE

#include "gtd-debug.h"
#include "gtd-provider-todoist.h"
#include "gtd-plugin-todoist.h"
#include "gtd-manager.h"
#include "gtd-utils.h"

#include <rest/oauth2-proxy.h>
#include <json-glib/json-glib.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <glib/gi18n.h>

#define GTD_PROVIDER_TODOIST_ERROR   (gtd_provider_todoist_error_quark ())
#define TODOIST_URL                  "https://todoist.com/API/v7/sync"
#define MAX_COMMANDS_PER_REQUEST     100
#define FOCUS_SYNCHRONIZE_INTERVAL   60 * 10
#define UNFOCUS_SYNCHRONIZE_INTERVAL 30 * 1

typedef enum
{
  REQUEST_OTHER,
  REQUEST_LIST_CREATE,
  REQUEST_LIST_REMOVE,
  REQUEST_LIST_UPDATE,
  REQUEST_TASK_CREATE,
  REQUEST_TASK_REMOVE,
  REQUEST_TASK_UPDATE,
} GtdTodoistRequest;

typedef enum
{
  GTD_PROVIDER_TODOIST_ERROR_BAD_GATEWAY,
  GTD_PROVIDER_TODOIST_ERROR_BAD_REQUEST,
  GTD_PROVIDER_TODOIST_ERROR_BAD_STATUS_CODE,
  GTD_PROVIDER_TODOIST_ERROR_INVALID_RESPONSE,
  GTD_PROVIDER_TODOIST_ERROR_LIMIT_REACHED,
  GTD_PROVIDER_TODOIST_ERROR_NOT_ALLOWED,
} GtdProviderTodoistError;

typedef struct
{
  GtdProviderTodoist *self;
  GtdObject          *object;
  gchar              *command;
  gchar              *command_uid;
  GtdTodoistRequest   request_type;
} PostCallbackData;

struct _GtdProviderTodoist
{
  GtdObject           parent;

  GoaObject          *account_object;

  gchar              *sync_token;
  gchar              *access_token;
  gchar              *description;
  GIcon              *icon;

  /* Map between id to list and task instance */
  GHashTable         *lists;
  GHashTable         *tasks;

  /* Queue to hold Request Data */
  GQueue             *queue;

  guint               timeout_id;
};


GQuark               gtd_provider_todoist_error_quark            (void);

static void          on_operation_completed_cb                   (RestProxyCall    *call,
                                                                  const GError     *error,
                                                                  GObject          *weak_object,
                                                                  gpointer          user_data);

static gboolean      on_schedule_wait_timeout_cb                 (gpointer            data);

static void          on_synchronize_completed_cb                 (RestProxyCall      *call,
                                                                  const GError       *error,
                                                                  GObject            *weak_object,
                                                                  gpointer            user_data);

static void          update_task_position                        (GtdProviderTodoist *self,
                                                                  GtdTask            *task,
                                                                  gboolean            schedule_requests);

static void          gtd_provider_iface_init                     (GtdProviderInterface *iface);


G_DEFINE_QUARK (GtdProviderTodoistError, gtd_provider_todoist_error)

G_DEFINE_TYPE_WITH_CODE (GtdProviderTodoist, gtd_provider_todoist, GTD_TYPE_OBJECT,
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
  PROP_GOA_OBJECT,
  LAST_PROP
};

static const gchar *colormap[] =
{
  "#95ef63",
  "#ff8581",
  "#ffc471",
  "#f9ec75",
  "#a8c8e4",
  "#d2b8a3",
  "#e2a8e4",
  "#cccccc",
  "#fb886e",
  "#ffcc00",
  "#74e8d3",
  "#3bd5fb",
  "#dc4fad",
  "#ac193d",
  "#d24726",
  "#82ba00",
  "#03b3b2",
  "#008299",
  "#5db2ff",
  "#0072c6",
  "#000000",
  "#777777"
};


/*
 * Auxiliary methods
 */

#define CHECK_ACCESS_TOKEN(self)  \
G_STMT_START {                    \
  if (!self->access_token) {      \
      emit_access_token_error (); \
      return;                     \
    }                             \
} G_STMT_END

static gchar*
escape_string_for_post (const gchar *string)
{
  g_autofree gchar *replaced_slash = gtd_str_replace (string, "\\", "\\\\");
  return gtd_str_replace (replaced_slash, "\"", "\\\"");
}

static gint
optimized_eucledian_color_distance (GdkRGBA *color1,
                                    GdkRGBA *color2)
{
  gdouble red_diff;
  gdouble green_diff;
  gdouble blue_diff;
  gdouble red_mean_level;

  red_mean_level = (color1->red + color2->red) / 2;
  red_diff = color1->red - color2->red;
  green_diff = color1->green - color2->green;
  blue_diff = color1->blue - color2->blue;

  return (red_diff * red_diff * (2 + red_mean_level) +
          green_diff * green_diff * 4 +
          blue_diff * blue_diff * ((1 - red_mean_level) + 2));
}

static GdkRGBA*
convert_color_code (gint index)
{
  GdkRGBA rgba;

  gdk_rgba_parse (&rgba, colormap [index]);

  return gdk_rgba_copy (&rgba);
}

static void
emit_access_token_error (void)
{
  gtd_manager_emit_error_message (gtd_manager_get_default (),
                                  _("Error fetching Todoist account key"),
                                  _("Please ensure that Todoist account is correctly configured."),
                                  NULL,
                                  NULL);
}

static gint
get_color_code_index (GdkRGBA *rgba)
{
  guint nearest_color_index;
  guint min_color_diff;
  guint i;

  nearest_color_index = 0;
  min_color_diff = G_MAXUINT;

  for (i = 0; i < G_N_ELEMENTS (colormap); i++)
    {
      GdkRGBA color;
      guint distance;

      gdk_rgba_parse (&color, colormap [i]);

      distance = optimized_eucledian_color_distance (rgba, &color);

      if (min_color_diff > distance)
        {
          min_color_diff = distance;
          nearest_color_index = i;
        }
    }

  return nearest_color_index;
}

static void
parse_task_lists (GtdProviderTodoist *self,
                  JsonArray          *projects)
{
  GList *lists;
  GList *l;

  lists = json_array_get_elements (projects);

  for (l = lists; l != NULL; l = l->next)
    {
      g_autofree gchar *uid = NULL;
      JsonObject *object;
      GtdTaskList *list;
      const gchar *name;
      guint32 id;
      guint color_index;
      gboolean new_list_added;
      gint is_deleted;

      object = json_node_get_object (l->data);
      new_list_added = FALSE;

      name = json_object_get_string_member (object, "name");
      color_index = json_object_get_int_member (object, "color");
      id = json_object_get_int_member (object, "id");
      is_deleted = json_object_get_int_member (object, "is_deleted");
      uid = g_strdup_printf ("%u", id);

      if (is_deleted)
        {
          if (g_hash_table_contains (self->lists, GUINT_TO_POINTER (id)))
            {
              g_debug ("%s is marked deleted. It will be removed", name);

              list = g_hash_table_lookup (self->lists, GUINT_TO_POINTER (id));
              g_hash_table_remove (self->lists, GUINT_TO_POINTER (id));

              g_signal_emit_by_name (self, "list-removed", list);
            }
          else
            {
              g_warning ("Unreachable code. List marked deleted but not found in lists hash table");
            }
          continue;
        }

      if (g_hash_table_contains (self->lists, GUINT_TO_POINTER (id)))
        {
          list = g_hash_table_lookup (self->lists, GUINT_TO_POINTER (id));
          new_list_added = FALSE;
        }
      else
        {
          list = gtd_task_list_new (GTD_PROVIDER (self));
          new_list_added = TRUE;
        }

      gtd_task_list_set_name (list, name);
      gtd_task_list_set_color (list, convert_color_code (color_index));
      gtd_task_list_set_is_removable (list, TRUE);
      gtd_object_set_uid (GTD_OBJECT (list), uid);

      g_hash_table_insert (self->lists, GUINT_TO_POINTER (id), list);

      if (new_list_added)
        g_signal_emit_by_name (self, "list-added", list);
    }
}

static GDateTime*
parse_date (const gchar *due_date)
{
  g_autoptr (GDateTime) dt = NULL;
  struct tm due_dt = { 0, };

  if (!strptime (due_date, "%a %d %b %Y %T %z", &due_dt))
    return NULL;

  dt = g_date_time_new_utc (due_dt.tm_year + 1900,
                            due_dt.tm_mon + 1,
                            due_dt.tm_mday,
                            due_dt.tm_hour,
                            due_dt.tm_min,
                            due_dt.tm_sec);

  return g_date_time_to_local (dt);
}

static gint
compare_tasks_by_position (gconstpointer a,
                           gconstpointer b)
{
  GtdTask *task_a, *task_b;

  task_a = *((GtdTask **) a);
  task_b = *((GtdTask **) b);

  return gtd_task_get_position (task_a) - gtd_task_get_position (task_b);
}

static void
parse_tasks (GtdProviderTodoist *self,
             JsonArray          *items)
{
  g_autoptr (GHashTable) list_to_tasks = NULL;
  GHashTableIter iter;
  GPtrArray *tasks;
  GQueue tasks_stack;
  gint64 project;
  GList *lists;
  GList *l;

  lists = json_array_get_elements (items);

  /* First, create all the tasks and store them temporarily in a GPtrArray */
  list_to_tasks = g_hash_table_new_full (g_direct_hash,
                                         g_direct_equal,
                                         NULL,
                                         (GDestroyNotify) g_ptr_array_unref);

  for (l = lists; l != NULL; l = l->next)
    {
      g_autofree gchar *uid = NULL;
      JsonObject *object;
      GtdTask *task;
      const gchar *title;
      const gchar *due_date;
      const gchar *date_added;
      gint64 complete;
      gint64 position;
      gint64 indent;
      gint64 id;
      gint priority;
      gint is_deleted;

      object = json_node_get_object (l->data);

      title = json_object_get_string_member (object, "content");
      priority = json_object_get_int_member (object, "priority");
      id = json_object_get_int_member (object, "id");
      project = json_object_get_int_member (object, "project_id");
      complete = json_object_get_int_member (object, "checked");
      due_date = json_object_get_string_member (object, "due_date_utc");
      date_added = json_object_get_string_member (object, "date_added");
      indent = json_object_get_int_member (object, "indent");
      position = json_object_get_int_member (object, "item_order");
      is_deleted = json_object_get_int_member (object, "is_deleted");

      uid = g_strdup_printf ("%ld", id);

      if (is_deleted)
        {
          if (g_hash_table_contains (self->tasks, GUINT_TO_POINTER (id)))
            {
              GtdTaskList *list;

              task = g_hash_table_lookup (self->tasks, GUINT_TO_POINTER (id));
              list = gtd_task_get_list (task);

              g_hash_table_remove (self->tasks, GUINT_TO_POINTER (id));

              gtd_task_list_remove_task (list, task);
            }
          else
            {
              g_warning ("Unreachable code. List marked deleted but not found in lists hash table");
            }
          continue;
        }

      /* Setup the new task */
      if (g_hash_table_contains (self->tasks, GUINT_TO_POINTER (id)))
        task = g_hash_table_lookup (self->tasks, GUINT_TO_POINTER (id));
      else
        task = gtd_task_new ();

      gtd_object_set_uid (GTD_OBJECT (task), uid);
      gtd_task_set_title (task, title);
      gtd_task_set_priority (task, priority - 1);
      gtd_task_set_complete (task, complete != 0);
      gtd_task_set_position (task, position);

      /* Due date */
      if (due_date)
        {
          g_autoptr (GDateTime) dt = parse_date (due_date);
          gtd_task_set_due_date (task, dt);
        }

      /* Date added */
      if (date_added)
        {
          g_autoptr (GDateTime) dt = parse_date (date_added);
          gtd_task_set_creation_date (task, dt);
        }

      g_object_set_data (G_OBJECT (task), "indent", GINT_TO_POINTER (indent));

      g_hash_table_insert (self->tasks, (gpointer) id, task);

      /* Add to the temporary GPtrArray that will be consumed below */
      if (!g_hash_table_contains (list_to_tasks, GINT_TO_POINTER (project)))
        g_hash_table_insert (list_to_tasks, GINT_TO_POINTER (project), g_ptr_array_new ());

      tasks = g_hash_table_lookup (list_to_tasks, GINT_TO_POINTER (project));
      g_ptr_array_add (tasks, task);
    }

  /*
   * Now that all the tasks are created and properly stored in a GPtrArray,
   * we have to go through each GPtrArray, sort it, and figure out the parent
   * & children relationship between the tasks.
   */
  g_queue_init (&tasks_stack);
  g_hash_table_iter_init (&iter, list_to_tasks);

  while (g_hash_table_iter_next (&iter, (gpointer) &project, (gpointer) &tasks))
    {
      GtdTaskList *list;
      gint64 previous_indent;
      gint64 i;

      list = g_hash_table_lookup (self->lists, GUINT_TO_POINTER (project));
      previous_indent = 0;

      GTD_TRACE_MSG ("Setting up tasklist '%s'", gtd_task_list_get_name (list));

      g_ptr_array_sort (tasks, compare_tasks_by_position);

      for (i = 0; tasks && i < tasks->len; i++)
        {
          GtdTask *parent_task;
          GtdTask *task;
          gint64 indent;
          gint j;

          task = g_ptr_array_index (tasks, i);
          parent_task = NULL;
          indent = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (task), "indent"));

          GTD_TRACE_MSG ("  Adding task '%s' (%s)",
                         gtd_task_get_title (task),
                         gtd_object_get_uid (GTD_OBJECT (task)));

          /* If the indent changed, remove from the difference in level from stack */
          for (j = 0; j <= previous_indent - indent; j++)
            g_queue_pop_head (&tasks_stack);

          parent_task = g_queue_peek_head (&tasks_stack);

          if (parent_task)
            gtd_task_add_subtask (parent_task, task);

          gtd_task_set_list (task, list);
          gtd_task_list_save_task (list, task);

          g_queue_push_head (&tasks_stack, task);

          previous_indent = indent;
        }

      /* Clear the queue since we're changing projects */
      g_queue_clear (&tasks_stack);
    }
}

static void
load_tasks (GtdProviderTodoist *self,
            JsonObject         *object)
{
  JsonArray *projects;
  JsonArray *items;

  projects = json_object_get_array_member (object, "projects");
  items = json_object_get_array_member (object, "items");

  parse_task_lists (self, projects);
  parse_tasks (self, items);
}

static void
check_post_response_for_errors (RestProxyCall     *call,
                                JsonParser        *parser,
                                PostCallbackData  *data,
                                const GError      *post_error,
                                GError           **error)
{
  JsonObject *object;
  guint status_code;

  status_code = rest_proxy_call_get_status_code (call);

  g_debug ("Received status code: %d", status_code);

  switch (status_code)
    {
    case 400:
      g_set_error (error,
                   GTD_PROVIDER_TODOIST_ERROR,
                   GTD_PROVIDER_TODOIST_ERROR_BAD_REQUEST,
                   "Bad request");
      return;

    case 429:
      g_set_error (error,
                   GTD_PROVIDER_TODOIST_ERROR,
                   GTD_PROVIDER_TODOIST_ERROR_LIMIT_REACHED,
                   "Too many requests");
      return;

    case 502:
      g_set_error (error,
                   GTD_PROVIDER_TODOIST_ERROR,
                   GTD_PROVIDER_TODOIST_ERROR_BAD_GATEWAY,
                   "Bad gateway error received when sending POST request to Todoist servers");
      return;

    default:
      break;
    }

  object = json_node_get_object (json_parser_get_root (parser));

  if (json_object_has_member (object, "sync_status"))
    {
      JsonObject *response;
      JsonNode *command_status;

      response = json_object_get_object_member (object, "sync_status");
      command_status = json_object_get_member (response, data->command_uid);

      /* This was a Todoist-specific error, parse the message */
      if (JSON_NODE_TYPE (command_status) == JSON_NODE_OBJECT)
        {
          JsonObject *error_object;
          const gchar *message;
          gint error_code;

          error_object = json_node_get_object (command_status);
          error_code = json_object_get_int_member (error_object, "error_code");
          message = json_object_get_string_member (error_object, "error");

          switch (error_code)
            {
            case 34:
              g_set_error (error,
                           GTD_PROVIDER_TODOIST_ERROR,
                           GTD_PROVIDER_TODOIST_ERROR_NOT_ALLOWED,
                           _("GNOME To Do doesn’t have the necessary permissions to perform this action: %s"),
                           message);
              break;

            default:
              g_set_error (error,
                           GTD_PROVIDER_TODOIST_ERROR,
                           GTD_PROVIDER_TODOIST_ERROR_INVALID_RESPONSE,
                           _("Invalid response received from Todoist servers. Please reload GNOME To Do."));
            }
        }
    }
}

static void
update_transient_id (GtdProviderTodoist *self,
                     JsonObject         *id_map,
                     GtdObject          *object,
                     GtdTodoistRequest   req)
{
  g_autoptr (GList) temp_ids;
  GList *l;

  temp_ids = json_object_get_members (id_map);

  for (l = temp_ids; l != NULL; l = l->next)
    {
      g_autofree gchar *uid;
      const gchar *temp_id;
      guint32 id;

      temp_id = gtd_object_get_uid (object);
      id = json_object_get_int_member (id_map, l->data);
      uid = g_strdup_printf ("%u", id);

      /* Update temp id to permanent id if temp-id in response matches object temp-id */
      if (g_str_equal (temp_id, l->data))
        gtd_object_set_uid (object, uid);
    }
}

static void
parse_request (GtdProviderTodoist *self,
               gpointer            object,
               GtdTodoistRequest   request)
{
  GTD_ENTRY;

  GTD_TRACE_MSG ("Applying request %d at %p", request, object);

  /* If we reached here, then the object is not loading anymore */
  gtd_object_pop_loading (object);

  switch (request)
    {
    case REQUEST_LIST_CREATE:
      g_assert (GTD_IS_TASK_LIST (object));

      g_hash_table_insert (self->lists, g_strdup (gtd_object_get_uid (object)), object);
      g_signal_emit_by_name (self, "list-added", object);
      break;

    case REQUEST_LIST_REMOVE:
      g_assert (GTD_IS_TASK_LIST (object));

      g_hash_table_remove (self->lists, gtd_object_get_uid (object));
      g_signal_emit_by_name (self, "list-removed", object);
      break;

    case REQUEST_LIST_UPDATE:
      g_assert (GTD_IS_TASK_LIST (object));

      g_signal_emit_by_name (self, "list-changed", object);
      break;

    case REQUEST_TASK_CREATE:
      g_assert (GTD_IS_TASK (object));

      g_hash_table_insert (self->tasks, g_strdup (gtd_object_get_uid (object)), object);
      break;

    case REQUEST_TASK_REMOVE:
        {
          g_autoptr (GList) subtasks = NULL;
          g_autoptr (GList) l = NULL;

          g_assert (GTD_IS_TASK (object));

          subtasks = gtd_task_get_subtasks (object);
          subtasks = g_list_prepend (subtasks, object);

          for (l = subtasks; l; l = l->next)
            {
              g_hash_table_remove (self->tasks, gtd_object_get_uid (object));
              gtd_task_list_remove_task (gtd_task_get_list (object), object);
            }

          /* Ensure we have the most updated positions */
          update_task_position (self, GTD_TASK (object), FALSE);
        }
      break;

    case REQUEST_TASK_UPDATE:
      g_assert (GTD_IS_TASK (object));
      break;

    case REQUEST_OTHER:
      /* Nothing */
      break;
    }

  GTD_EXIT;
}

static void
post (GtdProviderTodoist         *self,
      JsonObject                 *params,
      RestProxyCallAsyncCallback  callback,
      gpointer                    user_data)
{
  g_autoptr (GError) error = NULL;
  RestProxyCall *call;
  RestProxy *proxy;
  GList *param;
  GList *l;

  proxy = rest_proxy_new (TODOIST_URL, FALSE);
  call = rest_proxy_new_call (proxy);
  param = json_object_get_members (params);

  g_debug ("Sending POST request");

  /* Hold the application when starting a POST op, release on the callback */
  g_application_hold (g_application_get_default ());

  rest_proxy_call_set_method (call, "POST");
  rest_proxy_call_add_header (call, "content-type", "application/x-www-form-urlencoded");

  for (l = param; l != NULL; l = l->next)
    {
      JsonNode *node;
      const gchar *value;

      node = json_object_get_member (params, l->data);
      value = json_node_get_string (node);

      rest_proxy_call_add_param (call, l->data, value);
    }

  rest_proxy_call_async (call, callback, G_OBJECT (self), user_data, &error);

  g_object_unref (proxy);
  g_object_unref (call);
  g_list_free (param);
}

static gboolean
synchronize (GtdProviderTodoist *self)
{
  g_autoptr (JsonObject) params = NULL;

  CHECK_ACCESS_TOKEN (self);

  params = json_object_new ();

  json_object_set_string_member (params, "token", self->access_token);
  json_object_set_string_member (params, "sync_token", self->sync_token);
  json_object_set_string_member (params, "resource_types", "[\"all\"]");

  /*
   * While we're synchronizing, the GtdManager is loading, so that the
   * "Loading task lists" notification is visible.
   */
  gtd_object_push_loading (GTD_OBJECT (gtd_manager_get_default ()));
  gtd_object_push_loading (GTD_OBJECT (self));

  post (self, params, on_synchronize_completed_cb, self);

  return TRUE;
}

static gchar*
compress_commands (GtdProviderTodoist  *self,
                   GList              **out_list)
{
  g_autoptr (GString) command = NULL;
  g_autoptr (GList) requests = NULL;
  guint i;

  /*
   * Compress at most MAX_COMMANDS_PER_REQUEST (which is 100 - see the developer doc at
   * https://developer.todoist.com/sync/v7/#batching-commands) commands in the queue.
   */
  command = g_string_new ("[");

  for (i = 0; i < MAX_COMMANDS_PER_REQUEST && !g_queue_is_empty (self->queue); i++)
    {
      PostCallbackData *data = g_queue_pop_tail (self->queue);

      if (i > 0)
        g_string_append (command, ",\n");

      g_string_append (command, data->command);

      /*
       * Store which command was processed, so we can eventually put it back
       * into the queue if something goes wrong.
       */
      requests = g_list_prepend (requests, data);

      /* The object is in loading state while it's in the queue */
      gtd_object_push_loading (data->object);
    }

  g_string_append (command, "]");

  /* We need to parse the commands back in the order they were sent */
  requests = g_list_reverse (requests);

  g_assert (out_list != NULL);
  *out_list = g_steal_pointer (&requests);

  g_debug ("Compressed %u commands, request is:\n%s", i, command->str);

  return g_string_free (g_steal_pointer (&command), FALSE);
}

static void
process_request_queue (GtdProviderTodoist *self)
{
  g_autoptr (JsonObject) params = NULL;
  g_autoptr (GList) requests = NULL;
  g_autofree gchar *command = NULL;

  if (g_queue_is_empty (self->queue) || self->timeout_id > 0)
    return;

  g_debug ("Processing request queue");

  command = compress_commands (self, &requests);

  /* Build up the JSON command */
  params = json_object_new ();
  json_object_set_string_member (params, "commands", command);
  json_object_set_string_member (params, "token", self->access_token);

  post (self, params, on_operation_completed_cb, g_steal_pointer (&requests));
}

static void
schedule_post_request (GtdProviderTodoist *self,
                       gpointer            object,
                       GtdTodoistRequest   request,
                       const gchar        *command_uid,
                       const gchar        *command)
{
  PostCallbackData *data;

  g_assert (GTD_IS_OBJECT (object));

  /* Set params for post request */
  data = g_new0 (PostCallbackData, 1);
  data->self = self;
  data->request_type = request;
  data->object = GTD_OBJECT (object);
  data->command = g_strdup (command);
  data->command_uid = g_strdup (command_uid);

  g_debug ("Scheduling POST request");

  g_queue_push_head (self->queue, data);

  /* If the current timeout is set, cancel it */
  if (self->timeout_id > 0)
    g_source_remove (self->timeout_id);

  self->timeout_id = g_timeout_add_seconds (1, on_schedule_wait_timeout_cb, self);
}

static void
store_access_token (GtdProviderTodoist *self)
{
  g_autoptr (GError) error = NULL;
  GoaOAuth2Based *oauth2;

  oauth2 = goa_object_get_oauth2_based (self->account_object);

  goa_oauth2_based_call_get_access_token_sync (oauth2, &self->access_token, NULL, NULL, &error);

  if (error)
    g_warning ("Error retrieving OAuth2 access token: %s", error->message);

  g_clear_object (&oauth2);
}

static void
parse_json_from_response (JsonParser    *parser,
                          RestProxyCall *call)
{
  g_autoptr (GError) error = NULL;
  const gchar *payload;
  gsize payload_length;

  /* Parse the JSON response */
  payload = rest_proxy_call_get_payload (call);
  payload_length = rest_proxy_call_get_payload_length (call);

  if (!json_parser_load_from_data (parser, payload, payload_length, &error))
    {
      gtd_manager_emit_error_message (gtd_manager_get_default (),
                                      _("An error occurred while updating a Todoist task"),
                                      error->message,
                                      NULL,
                                      NULL);

      g_warning ("Error executing request: %s", error->message);
      return;
    }

  /* Print the response if tracing is enabled */
#ifdef GTD_ENABLE_TRACE
  {
    g_autoptr (JsonGenerator) generator;

    generator = g_object_new (JSON_TYPE_GENERATOR,
                              "root", json_parser_get_root (parser),
                              "pretty", TRUE,
                              NULL);

    g_debug ("Response: \n%s", json_generator_to_data (generator, NULL));
  }
#endif
}

static void
update_task_position (GtdProviderTodoist *self,
                      GtdTask            *task,
                      gboolean            schedule_requests)
{
  g_autoptr (GList) tasks = NULL;
  g_autoptr (GList) l = NULL;
  GtdTaskList *list;
  gint64 i;

  list = gtd_task_get_list (task);
  tasks = gtd_task_list_get_tasks (list);
  tasks = g_list_sort (tasks, (GCompareFunc) gtd_task_compare);

  for (l = tasks, i = 0; l; l = l->next, i++)
    {
      g_autofree gchar *command_uid = NULL;
      g_autofree gchar *command = NULL;
      GtdTask *t = l->data;

      if (gtd_task_get_position (t) == i)
        continue;

      /* Update the position */
      gtd_task_set_position (l->data, i);

      if (!schedule_requests)
        continue;

      /* Queue more update commands to update the other tasks' position */
      command_uid = g_uuid_string_random ();
      command = g_strdup_printf ("{                                \n"
                                 "    \"type\": \"item_update\",   \n"
                                 "    \"uuid\": \"%s\",            \n"
                                 "    \"args\": {                  \n"
                                 "        \"id\": %s,              \n"
                                 "        \"indent\": %d,          \n"
                                 "        \"item_order\": %ld      \n"
                                 "    }                            \n"
                                 "}",
                                 command_uid,
                                 gtd_object_get_uid (GTD_OBJECT (t)),
                                 gtd_task_get_depth (t) + 1,
                                 i);

      schedule_post_request (self, t, REQUEST_TASK_UPDATE, command_uid, command);
    }
}

static void
setup_sync_timeout (GtdProviderTodoist *self,
                    GtkWindow          *window)
{
  gboolean active;

  active = gtk_window_has_toplevel_focus (window);

  if (self->timeout_id)
    g_source_remove (self->timeout_id);

  /*
   * If To Do window is active, synchronize immediately and set timeout
   * to sync more frequently else set sync to occur less frequently
   */
  if (active)
    {
      synchronize (self);
      self->timeout_id = g_timeout_add_seconds (FOCUS_SYNCHRONIZE_INTERVAL, (GSourceFunc) synchronize , self);
    }
  else
    {
      self->timeout_id = g_timeout_add_seconds (UNFOCUS_SYNCHRONIZE_INTERVAL, (GSourceFunc) synchronize , self);
    }
}

/*
 * Callbacks
 */

static gboolean
on_schedule_wait_timeout_cb (gpointer data)
{
  GtdProviderTodoist *self = (GtdProviderTodoist *) data;

  self->timeout_id = 0;

  process_request_queue (self);

  return G_SOURCE_REMOVE;
}

static void
on_synchronize_completed_cb (RestProxyCall *call,
                             const GError  *post_error,
                             GObject       *weak_object,
                             gpointer       user_data)
{
  g_autoptr (JsonParser) parser = NULL;
  g_autoptr (JsonObject) object = NULL;
  g_autoptr (GError) error = NULL;
  GtdProviderTodoist *self;

  self = GTD_PROVIDER_TODOIST (weak_object);
  parser = json_parser_new ();

  /* Unmark the GtdManager as loading */
  gtd_object_pop_loading (GTD_OBJECT (self));
  gtd_object_pop_loading (GTD_OBJECT (gtd_manager_get_default ()));

  /* Release the application */
	g_application_release (g_application_get_default ());

  parse_json_from_response (parser, call);
  check_post_response_for_errors (call, parser, NULL, post_error, &error);

  if (error)
    {
      gtd_manager_emit_error_message (gtd_manager_get_default (),
                                      _("An error occurred while retrieving Todoist data"),
                                      error->message,
                                      NULL,
                                      NULL);

      g_warning ("Error synchronizing with Todoist: %s", error->message);
      return;
    }

  object = json_node_dup_object (json_parser_get_root (parser));

  if (json_object_has_member (object, "sync_token"))
    {
      g_clear_pointer (&self->sync_token, g_free);
      self->sync_token = g_strdup (json_object_get_string_member (object, "sync_token"));
    }

  load_tasks (self, object);
}

static void
on_operation_completed_cb (RestProxyCall *call,
                           const GError  *post_error,
                           GObject       *weak_object,
                           gpointer       user_data)
{
  g_autoptr (JsonParser) parser = NULL;
  g_autoptr (GList) requests = NULL;
  g_autoptr (GList) l = NULL;
  GtdProviderTodoist *self;
  JsonObject *object;

  self = GTD_PROVIDER_TODOIST (weak_object);
  requests = (GList *) user_data;

  g_debug ("Received response for POST request");

  /* Release the hold since queue is empty */
  g_application_release (g_application_get_default ());

  /* Parse the response */
  parser = json_parser_new ();

  parse_json_from_response (parser, call);

  for (l = requests; l; l = l->next)
    {
      g_autoptr (GError) error = NULL;
      PostCallbackData *data;

      data = l->data;

      g_debug ("Parsing command %s", data->command_uid);

      check_post_response_for_errors (call, parser, data, post_error, &error);

      if (error)
        {
          if (g_error_matches (error, GTD_PROVIDER_TODOIST_ERROR, GTD_PROVIDER_TODOIST_ERROR_BAD_GATEWAY))
            {
              g_debug ("Bad gateway received, trying again immediately");
              g_queue_push_tail (self->queue, data);
            }
          else if (g_error_matches (error, GTD_PROVIDER_TODOIST_ERROR, GTD_PROVIDER_TODOIST_ERROR_LIMIT_REACHED))
            {
              g_debug ("Rescheduling dispatch timeout to 60 seconds");

              g_queue_push_tail (self->queue, data);

              if (self->timeout_id == 0)
                self->timeout_id = g_timeout_add_seconds (60, on_schedule_wait_timeout_cb, self);
            }
          else
            {
              gtd_manager_emit_error_message (gtd_manager_get_default (),
                                              _("An error occurred while updating Todoist"),
                                              error->message,
                                              NULL,
                                              NULL);

              g_warning ("Error executing request: %s", error->message);

              /* Not too much we can do here... */
              gtd_object_pop_loading (data->object);

              g_clear_pointer (&data->command_uid, g_free);
              g_clear_pointer (&data->command, g_free);
              g_clear_pointer (&data, g_free);
            }

          continue;
        }

      object = json_node_get_object (json_parser_get_root (parser));
      if (json_object_has_member (object, "sync_token"))
        {
          g_clear_pointer (&self->sync_token, g_free);
          self->sync_token = g_strdup (json_object_get_string_member (object, "sync_token"));
        }

      /* Update temp-id if response contains temp-id mapping */
      if (json_object_has_member (object, "temp_id_mapping"))
        {
          update_transient_id (self,
                               json_object_get_object_member (object, "temp_id_mapping"),
                               data->object,
                               data->request_type);
        }

      /* Apply the request operation */
      parse_request (self, data->object, data->request_type);

      g_clear_pointer (&data->command_uid, g_free);
      g_clear_pointer (&data->command, g_free);
      g_clear_pointer (&data, g_free);
    }

  /* Dispatch next batch of queued requests */
  process_request_queue (self);
}

static void
on_window_focus_changed (GtkWindow          *window,
                         GParamSpec         *pspec,
                         GtdProviderTodoist *self)
{
  g_debug ("%s value changed", pspec->name);

  setup_sync_timeout (self, window);
}

/*
 * GtdProviderInterface implementation
 */

static const gchar*
gtd_provider_todoist_get_id (GtdProvider *provider)
{
  return "todoist";
}

static const gchar*
gtd_provider_todoist_get_name (GtdProvider *provider)
{
  return _("Todoist");
}

static const gchar*
gtd_provider_todoist_get_provider_type (GtdProvider *provider)
{
  return "todoist";
}

static const gchar*
gtd_provider_todoist_get_description (GtdProvider *provider)
{
  GtdProviderTodoist *self;

  self = GTD_PROVIDER_TODOIST (provider);

  return self->description;
}


static gboolean
gtd_provider_todoist_get_enabled (GtdProvider *provider)
{
  return TRUE;
}

static GIcon*
gtd_provider_todoist_get_icon (GtdProvider *provider)
{
  GtdProviderTodoist *self;

  self = GTD_PROVIDER_TODOIST (provider);

  return self->icon;
}

static void
gtd_provider_todoist_create_task (GtdProvider *provider,
                                  GtdTaskList *list,
                                  const gchar *title,
                                  GDateTime   *due_date)
{
  GtdProviderTodoist *self;
  g_autoptr (GDateTime) creation_date = NULL;
  g_autoptr (GtdTask) new_task = NULL;
  g_autoptr (GList) tasks = NULL;
  g_autofree gchar *command = NULL;
  g_autofree gchar *command_uid = NULL;
  g_autofree gchar *temp_id = NULL;
  g_autofree gchar *due_dt = NULL;
  g_autofree gchar *escaped_title = NULL;
  const gchar *project_id;
  gint position;

  self = GTD_PROVIDER_TODOIST (provider);

  CHECK_ACCESS_TOKEN (self);

  project_id = gtd_object_get_uid (GTD_OBJECT (list));
  escaped_title = escape_string_for_post (title);
  due_dt = due_date ? g_date_time_format (due_date, "\"%FT%R\"") : g_strdup ("null");
  creation_date = g_date_time_new_now_utc ();
  tasks = gtd_task_list_get_tasks (list);
  position = g_list_length (tasks);

  command_uid = g_uuid_string_random ();
  temp_id = g_uuid_string_random ();

  /* Create the new task */
  new_task = gtd_task_new ();
  gtd_task_set_due_date (new_task, due_date);
  gtd_task_set_list (new_task, list);
  gtd_task_set_title (new_task, title);
  gtd_task_set_position (new_task, position);
  gtd_task_set_creation_date (new_task, creation_date);
  gtd_object_set_uid (GTD_OBJECT (new_task), temp_id);

  gtd_task_list_save_task (list, new_task);

  command = g_strdup_printf ("{                              \n"
                             "    \"type\": \"item_add\",    \n"
                             "    \"temp_id\": \"%s\",       \n"
                             "    \"uuid\": \"%s\",          \n"
                             "    \"args\": {                \n"
                             "        \"content\": \"%s\",   \n"
                             "        \"priority\": 1,       \n"
                             "        \"project_id\": %s,    \n"
                             "        \"indent\": 1,         \n"
                             "        \"checked\": 0,        \n"
                             "        \"item_order\": %d,    \n"
                             "        \"due_date_utc\": %s   \n"
                             "    }                          \n"
                             "}",
                             temp_id,
                             command_uid,
                             escaped_title,
                             project_id,
                             position,
                             due_dt);

  schedule_post_request (self, g_steal_pointer (&new_task), REQUEST_TASK_CREATE, command_uid, command);
}

static void
gtd_provider_todoist_update_task (GtdProvider *provider,
                                  GtdTask     *task)
{
  g_autoptr (GDateTime) local_due_date = NULL;
  g_autoptr (GDateTime) due_date = NULL;
  GtdProviderTodoist *self;
  g_autofree gchar *escaped_title = NULL;
  g_autofree gchar *command_uid = NULL;
  g_autofree gchar *command = NULL;
  g_autofree gchar *due_dt = NULL;

  self = GTD_PROVIDER_TODOIST (provider);
  due_dt = command = command_uid = NULL;
  escaped_title = NULL;

  CHECK_ACCESS_TOKEN (self);

  escaped_title = escape_string_for_post (gtd_task_get_title (task));
  local_due_date = gtd_task_get_due_date (task);
  due_date = local_due_date ? g_date_time_to_utc (local_due_date) : NULL;
  due_dt = due_date ? g_date_time_format (due_date, "\"%FT%R\"") : g_strdup ("null");

  command_uid = g_uuid_string_random ();
  command = g_strdup_printf ("{                                \n"
                             "    \"type\": \"item_update\",   \n"
                             "    \"uuid\": \"%s\",            \n"
                             "    \"args\": {                  \n"
                             "        \"checked\": %d,         \n"
                             "        \"content\": \"%s\",     \n"
                             "        \"due_date_utc\": %s,    \n"
                             "        \"id\": %s,              \n"
                             "        \"indent\": %d,          \n"
                             "        \"item_order\": %ld,     \n"
                             "        \"priority\": %d,        \n"
                             "    }                            \n"
                             "}",
                             command_uid,
                             gtd_task_get_complete (task),
                             escaped_title,
                             due_dt,
                             gtd_object_get_uid (GTD_OBJECT (task)),
                             gtd_task_get_depth (task) + 1,
                             gtd_task_get_position (task),
                             gtd_task_get_priority (task) + 1);

  schedule_post_request (self, task, REQUEST_TASK_UPDATE, command_uid, command);

  /*
   * Update the task position, and the position of the other tasks too. We need to reset
   * the task position so that we can properly calculate it again
   */
  update_task_position (self, task, TRUE);
}

static void
gtd_provider_todoist_remove_task (GtdProvider *provider,
                                  GtdTask     *task)
{
  g_autofree gchar *command_uid = NULL;
  g_autofree gchar *command = NULL;
  GtdProviderTodoist *self;

  self = GTD_PROVIDER_TODOIST (provider);

  CHECK_ACCESS_TOKEN (self);

  command_uid = g_uuid_string_random ();
  command = g_strdup_printf ("{                                 \n"
                             "    \"type\": \"item_delete\",    \n"
                             "    \"uuid\": \"%s\",             \n"
                             "    \"args\": { \"ids\": [%s] }   \n"
                             "}",
                             command_uid,
                             gtd_object_get_uid (GTD_OBJECT (task)));

  schedule_post_request (self, task, REQUEST_TASK_REMOVE, command_uid, command);
}

static void
gtd_provider_todoist_create_task_list (GtdProvider *provider,
                                       const gchar *name)
{
  g_autoptr (GtdTaskList) new_list = NULL;
  g_autofree gchar *command = NULL;
  g_autofree gchar *command_uid = NULL;
  g_autofree gchar *escaped_name = NULL;
  g_autofree gchar *temp_id = NULL;
  GtdProviderTodoist *self;
  GdkRGBA color;

  self = GTD_PROVIDER_TODOIST (provider);

  CHECK_ACCESS_TOKEN (self);

  escaped_name = escape_string_for_post (name);
  command_uid = g_uuid_string_random ();
  temp_id = g_uuid_string_random ();

  /* Create the new list */
  new_list = gtd_task_list_new (provider);
  gtd_task_list_set_name (new_list, name);
  gtd_object_set_uid (GTD_OBJECT (new_list), temp_id);

  /* Default color is Todoist orange/red, since it has no white */
  gdk_rgba_parse (&color, colormap [8]);
  gtd_task_list_set_color (new_list, &color);

  command = g_strdup_printf ("{                                 \n"
                             "    \"type\": \"project_add\",    \n"
                             "    \"temp_id\": \"%s\",          \n"
                             "    \"uuid\": \"%s\",             \n"
                             "    \"args\": {                   \n"
                             "        \"name\": \"%s\",         \n"
                             "        \"color\": 8              \n"
                             "    }                             \n"
                             "}",
                             temp_id,
                             command_uid,
                             escaped_name);

  schedule_post_request (self, g_steal_pointer (&new_list), REQUEST_LIST_CREATE, command_uid, command);
}

static void
gtd_provider_todoist_update_task_list (GtdProvider *provider,
                                       GtdTaskList *list)
{
  GtdProviderTodoist *self;
  g_autoptr (GdkRGBA) list_color = NULL;
  g_autofree gchar *command = NULL;
  g_autofree gchar *command_uid = NULL;

  self = GTD_PROVIDER_TODOIST (provider);

  CHECK_ACCESS_TOKEN (self);

  list_color = gtd_task_list_get_color (list);

  command_uid = g_uuid_string_random ();
  command = g_strdup_printf ("{                                    \n"
                             "    \"type\": \"project_update\",    \n"
                             "    \"uuid\": \"%s\",                \n"
                             "    \"args\": {                      \n"
                             "        \"id\": %s,                  \n"
                             "        \"name\": \"%s\",            \n"
                             "        \"color\": %d                \n"
                             "    }                                \n"
                             "}",
                             command_uid,
                             gtd_object_get_uid (GTD_OBJECT (list)),
                             gtd_task_list_get_name (list),
                             get_color_code_index (list_color));

  schedule_post_request (self, list, REQUEST_LIST_UPDATE, command_uid, command);
}

static void
gtd_provider_todoist_remove_task_list (GtdProvider *provider,
                                       GtdTaskList *list)
{
  GtdProviderTodoist *self;
  g_autofree gchar *command = NULL;
  g_autofree gchar *command_uid = NULL;

  self = GTD_PROVIDER_TODOIST (provider);

  CHECK_ACCESS_TOKEN (self);

  command_uid = g_uuid_string_random ();
  command = g_strdup_printf ("{                                  \n"
                             "    \"type\": \"project_delete\",  \n"
                             "    \"uuid\": \"%s\",              \n"
                             "    \"args\": { \"ids\": [ %s ]}   \n"
                             "}",
                             command_uid,
                             gtd_object_get_uid (GTD_OBJECT (list)));

  schedule_post_request (self, list, REQUEST_LIST_REMOVE, command_uid, command);
}

static GList*
gtd_provider_todoist_get_task_lists (GtdProvider *provider)
{
  GtdProviderTodoist *self;

  self = GTD_PROVIDER_TODOIST (provider);

  return g_hash_table_get_values (self->lists);
}

static GtdTaskList*
gtd_provider_todoist_get_default_task_list (GtdProvider *provider)
{
  return NULL;
}

static void
gtd_provider_todoist_set_default_task_list (GtdProvider *provider,
                                            GtdTaskList *list)
{

}

static void
update_description (GtdProviderTodoist *self)
{
  GoaAccount *account;
  const gchar *identity;

  account = goa_object_get_account (self->account_object);
  identity = goa_account_get_identity (account);
  self->description = g_strdup_printf (_("Todoist: %s"), identity);

  g_object_unref (account);
}

static void
gtd_provider_iface_init (GtdProviderInterface *iface)
{
  iface->get_id = gtd_provider_todoist_get_id;
  iface->get_name = gtd_provider_todoist_get_name;
  iface->get_provider_type = gtd_provider_todoist_get_provider_type;
  iface->get_description = gtd_provider_todoist_get_description;
  iface->get_enabled = gtd_provider_todoist_get_enabled;
  iface->get_icon = gtd_provider_todoist_get_icon;
  iface->create_task = gtd_provider_todoist_create_task;
  iface->update_task = gtd_provider_todoist_update_task;
  iface->remove_task = gtd_provider_todoist_remove_task;
  iface->create_task_list = gtd_provider_todoist_create_task_list;
  iface->update_task_list = gtd_provider_todoist_update_task_list;
  iface->remove_task_list = gtd_provider_todoist_remove_task_list;
  iface->get_task_lists = gtd_provider_todoist_get_task_lists;
  iface->get_default_task_list = gtd_provider_todoist_get_default_task_list;
  iface->set_default_task_list = gtd_provider_todoist_set_default_task_list;
}

static void
gtd_provider_todoist_finalize (GObject *object)
{
  GtdProviderTodoist *self = (GtdProviderTodoist *)object;

  g_clear_pointer (&self->lists, g_hash_table_destroy);
  g_clear_pointer (&self->tasks, g_hash_table_destroy);
  g_clear_object (&self->icon);
  g_clear_pointer (&self->sync_token, g_free);
  g_clear_pointer (&self->description, g_free);
  g_queue_free (self->queue);

  G_OBJECT_CLASS (gtd_provider_todoist_parent_class)->finalize (object);
}

static void
gtd_provider_todoist_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  GtdProvider *provider = GTD_PROVIDER (object);

  switch (prop_id)
    {
    case PROP_DESCRIPTION:
      g_value_set_string (value, gtd_provider_todoist_get_description (provider));
      break;

    case PROP_ENABLED:
      g_value_set_boolean (value, gtd_provider_todoist_get_enabled (provider));
      break;

    case PROP_ICON:
      g_value_set_object (value, gtd_provider_todoist_get_icon (provider));
      break;

    case PROP_ID:
      g_value_set_string (value, gtd_provider_todoist_get_id (provider));
      break;

    case PROP_NAME:
      g_value_set_string (value, gtd_provider_todoist_get_name (provider));
      break;

    case PROP_PROVIDER_TYPE:
      g_value_set_string (value, gtd_provider_todoist_get_provider_type (provider));
      break;

    case PROP_GOA_OBJECT:
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_provider_todoist_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  GtdProviderTodoist *self = GTD_PROVIDER_TODOIST (object);

  switch (prop_id)
    {
    case PROP_GOA_OBJECT:
      self->account_object = GOA_OBJECT (g_value_dup_object (value));

      update_description (self);
      store_access_token (self);

      /* Only synchronize if we have an access token */
      if (self->access_token)
        synchronize (self);

      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_provider_todoist_class_init (GtdProviderTodoistClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gtd_provider_todoist_finalize;
  object_class->get_property = gtd_provider_todoist_get_property;
  object_class->set_property = gtd_provider_todoist_set_property;

  g_object_class_install_property (object_class,
                                   PROP_GOA_OBJECT,
                                   g_param_spec_object ("goa-object",
                                                        "Goa Object",
                                                        "GOA Object around a Todoist Goa Account",
                                                        GOA_TYPE_OBJECT,
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
gtd_provider_todoist_init (GtdProviderTodoist *self)
{
  self->sync_token = g_strdup ("*");
  self->queue = g_queue_new ();
  self->icon = G_ICON (g_themed_icon_new_with_default_fallbacks ("goa-account-todoist"));

  /* Project id → GtdTaskList */
  self->lists = g_hash_table_new (g_direct_hash, g_direct_equal);

  /* Task id → GtdTask */
  self->tasks = g_hash_table_new (g_direct_hash, g_direct_equal);

  /* Timeout to sync with Todoist */
  setup_sync_timeout (self, gtk_application_get_active_window (GTK_APPLICATION (g_application_get_default ())));

  g_signal_connect (gtk_application_get_active_window (GTK_APPLICATION (g_application_get_default ())),
                    "notify::has-toplevel-focus",
                    G_CALLBACK (on_window_focus_changed),
                    self);
}

GtdProviderTodoist*
gtd_provider_todoist_new (GoaObject *account_object)
{

  return g_object_new (GTD_TYPE_PROVIDER_TODOIST,
                       "goa-object", account_object,
                       NULL);
}

GoaObject*
gtd_provider_todoist_get_goa_object (GtdProviderTodoist  *self)
{
  return self->account_object;
}
