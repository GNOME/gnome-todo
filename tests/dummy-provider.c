/* dummy-provider.c
 *
 * Copyright 2018-2020 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "DummyProvider"

#include "gnome-todo.h"

#include "gtd-debug.h"
#include "dummy-provider.h"

struct _DummyProvider
{
  GtdObject           parent;

  GSequence          *lists;

  guint32             number_of_tasks;
  guint               remove_task_source_id;
};

static void          gtd_provider_iface_init                     (GtdProviderInterface *iface);

G_DEFINE_TYPE_WITH_CODE (DummyProvider, dummy_provider, GTD_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GTD_TYPE_PROVIDER, gtd_provider_iface_init))

enum
{
  PROP_0,
  PROP_DESCRIPTION,
  PROP_ENABLED,
  PROP_ICON,
  PROP_ID,
  PROP_NAME,
  PROP_PROVIDER_TYPE,
  N_PROPS
};


/*
 * Callbacks
 */

static gboolean
remove_task_cb (gpointer user_data)
{
  DummyProvider *self = (DummyProvider*) user_data;

  dummy_provider_randomly_remove_task (self);

  return G_SOURCE_CONTINUE;
}


/*
 * Auxiliary methods
 */

static GList*
sequence_to_list (GSequence *sequence)
{
  GSequenceIter *iter = NULL;
  GList *list = NULL;

  for (iter = g_sequence_get_begin_iter (sequence);
       !g_sequence_iter_is_end (iter);
       iter = g_sequence_iter_next (iter))
    {
      list = g_list_prepend (list, g_sequence_get (iter));
    }

  return g_list_reverse (list);
}


/*
 * GtdProvider iface
 */

static const gchar*
dummy_provider_get_id (GtdProvider *provider)
{
  return "dummy-provider";
}

static const gchar*
dummy_provider_get_name (GtdProvider *provider)
{
  return "Dummy Provider";
}

static const gchar*
dummy_provider_get_provider_type (GtdProvider *provider)
{
  return "dummy-provider";
}

static const gchar*
dummy_provider_get_description (GtdProvider *provider)
{
  return "Dummyest provider of the known human history";
}

static gboolean
dummy_provider_get_enabled (GtdProvider *provider)
{
  return TRUE;
}

static GIcon*
dummy_provider_get_icon (GtdProvider *provider)
{
  return g_themed_icon_new ("face-monkey-symbolic");
}
static void
dummy_provider_create_task (GtdProvider         *provider,
                            GtdTaskList         *list,
                            const gchar         *title,
                            GDateTime           *due_date,
                            GCancellable        *cancellable,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data)
{
}

static void
dummy_provider_update_task (GtdProvider         *provider,
                            GtdTask             *task,
                            GCancellable        *cancellable,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data)
{
  g_debug ("Updating task '%s'", gtd_task_get_title (task));
}

static void
dummy_provider_remove_task (GtdProvider         *provider,
                            GtdTask             *task,
                            GCancellable        *cancellable,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data)
{
}

static void
dummy_provider_create_task_list (GtdProvider         *provider,
                                 const gchar         *name,
                                 GCancellable        *cancellable,
                                 GAsyncReadyCallback  callback,
                                 gpointer             user_data)
{
  GSequenceIter *iter;
  DummyProvider *self;
  GtdTaskList* list;

  self = DUMMY_PROVIDER (provider);

  list = gtd_task_list_new (provider);
  gtd_task_list_set_name (list, name);

  iter = g_sequence_append (self->lists, list);
  g_object_set_data (G_OBJECT (list), "DummyProvider::iter", iter);

  g_signal_emit_by_name (self, "list-added", list);
}

static void
dummy_provider_update_task_list (GtdProvider         *provider,
                                 GtdTaskList         *list,
                                 GCancellable        *cancellable,
                                 GAsyncReadyCallback  callback,
                                 gpointer             user_data)
{
  g_signal_emit_by_name (provider, "list-changed", list);
}

static void
dummy_provider_remove_task_list (GtdProvider         *provider,
                                 GtdTaskList         *list,
                                 GCancellable        *cancellable,
                                 GAsyncReadyCallback  callback,
                                 gpointer             user_data)
{

  GSequenceIter *iter;

  iter = g_object_get_data (G_OBJECT (list), "DummyProvider::iter");
  g_sequence_remove (iter);

  g_signal_emit_by_name (provider, "list-removed", list);
}

static GList*
dummy_provider_get_task_lists (GtdProvider *provider)
{
  DummyProvider *self = DUMMY_PROVIDER (provider);
  return sequence_to_list (self->lists);
}

static GtdTaskList*
dummy_provider_get_inbox (GtdProvider *provider)
{
  return NULL;
}

static void
gtd_provider_iface_init (GtdProviderInterface *iface)
{
  iface->get_id = dummy_provider_get_id;
  iface->get_name = dummy_provider_get_name;
  iface->get_provider_type = dummy_provider_get_provider_type;
  iface->get_description = dummy_provider_get_description;
  iface->get_enabled = dummy_provider_get_enabled;
  iface->get_icon = dummy_provider_get_icon;
  iface->create_task = dummy_provider_create_task;
  iface->update_task = dummy_provider_update_task;
  iface->remove_task = dummy_provider_remove_task;
  iface->create_task_list = dummy_provider_create_task_list;
  iface->update_task_list = dummy_provider_update_task_list;
  iface->remove_task_list = dummy_provider_remove_task_list;
  iface->get_task_lists = dummy_provider_get_task_lists;
  iface->get_inbox = dummy_provider_get_inbox;
}


/*
 * GObject overrides
 */

static void
dummy_provider_finalize (GObject *object)
{
  DummyProvider *self = (DummyProvider *)object;

  g_clear_pointer (&self->lists, g_sequence_free);

  G_OBJECT_CLASS (dummy_provider_parent_class)->finalize (object);
}

static void
dummy_provider_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  GtdProvider *provider = GTD_PROVIDER (object);

  switch (prop_id)
    {
    case PROP_DESCRIPTION:
      g_value_set_string (value, dummy_provider_get_description (provider));
      break;

    case PROP_ENABLED:
      g_value_set_boolean (value, dummy_provider_get_enabled (provider));
      break;

    case PROP_ICON:
      g_value_set_object (value, dummy_provider_get_icon (provider));
      break;

    case PROP_ID:
      g_value_set_string (value, dummy_provider_get_id (provider));
      break;

    case PROP_NAME:
      g_value_set_string (value, dummy_provider_get_name (provider));
      break;

    case PROP_PROVIDER_TYPE:
      g_value_set_string (value, dummy_provider_get_provider_type (provider));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
dummy_provider_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
dummy_provider_class_init (DummyProviderClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = dummy_provider_finalize;
  object_class->get_property = dummy_provider_get_property;
  object_class->set_property = dummy_provider_set_property;

  g_object_class_override_property (object_class, PROP_DESCRIPTION, "description");
  g_object_class_override_property (object_class, PROP_ENABLED, "enabled");
  g_object_class_override_property (object_class, PROP_ICON, "icon");
  g_object_class_override_property (object_class, PROP_ID, "id");
  g_object_class_override_property (object_class, PROP_NAME, "name");
  g_object_class_override_property (object_class, PROP_PROVIDER_TYPE, "provider-type");
}

static void
dummy_provider_init (DummyProvider *self)
{
  self->lists = g_sequence_new (g_object_unref);
}

DummyProvider*
dummy_provider_new (void)
{
  return g_object_new (DUMMY_TYPE_PROVIDER, NULL);
}

guint
dummy_provider_generate_task_list (DummyProvider *self)
{
  GSequenceIter *iter;
  GtdTaskList *list;
  guint n_generated_tasks;
  gint i;

  /*
   * This generates a task list with the following layout:
   *
   * - Task
   * - Task
   *    - Task
   * - Task
   *    - Task
   *    - Task
   * - Task
   *    - Task
   *    - Task
   *    - Task
   */

  gtd_provider_create_task_list (GTD_PROVIDER (self), "List", NULL, NULL, NULL);
  iter = g_sequence_iter_prev (g_sequence_get_end_iter (self->lists));
  list = g_sequence_get (iter);

  n_generated_tasks = 0;

  for (i = 0; i < 4; i++)
    {
      g_autoptr (GtdTask) task = NULL;
      g_autofree gchar *title = NULL;
      g_autofree gchar *uuid = NULL;

      uuid = g_uuid_string_random ();
      title = g_strdup_printf ("%d", i);

      task = gtd_task_new ();
      gtd_task_set_list (task, list);
      gtd_object_set_uid (GTD_OBJECT (task), uuid);
      gtd_task_set_title (task, title);
      gtd_task_set_position (task, n_generated_tasks++);
      gtd_task_list_add_task (list, task);
    }

  return n_generated_tasks;
}

guint
dummy_provider_generate_task_lists (DummyProvider *self)
{
  static guint32 task_id = 0;
  guint32 n_lists;
  guint32 i;
  guint32 j;

  g_return_val_if_fail (DUMMY_IS_PROVIDER (self), 0);

  n_lists = g_random_int_range (2, 5);

  g_debug ("Creating %u task lists", n_lists);

  for (i = 0; i < n_lists; i++)
    {
      g_autofree gchar *list_name = NULL;
      GSequenceIter *iter;
      GtdTaskList *new_list;
      guint32 n_tasks;

      list_name = g_strdup_printf ("List %u", task_id++ + 1);
      gtd_provider_create_task_list (GTD_PROVIDER (self), list_name, NULL, NULL, NULL);

      /* The new list is the last one */
      iter = g_sequence_iter_prev (g_sequence_get_end_iter (self->lists));
      new_list = g_sequence_get (iter);

      /* Create a random number of stub tasks */
      n_tasks = g_random_int_range (10, 20);

      g_debug ("  Creating %u tasks at list %u (%s)", n_tasks, i, list_name);

      self->number_of_tasks += n_tasks;

      for (j = 0; j < n_tasks; j++)
        {
          g_autofree gchar *title = NULL;
          g_autofree gchar *uuid = NULL;
          GtdTask *task;

          task = gtd_task_new ();
          gtd_task_set_list (task, new_list);
          gtd_task_set_due_date (task, NULL);

          title = g_strdup_printf ("Task %u", j + 1);
          gtd_task_set_title (task, title);

          uuid = g_uuid_string_random ();
          gtd_object_set_uid (GTD_OBJECT (task), uuid);

          gtd_task_list_add_task (new_list, task);
        }
    }

  return self->number_of_tasks;
}

void
dummy_provider_schedule_remove_task (DummyProvider *self)
{
  g_return_if_fail (DUMMY_IS_PROVIDER (self));

  if (self->remove_task_source_id > 0)
    return;

  self->remove_task_source_id = g_timeout_add_seconds (2, remove_task_cb, self);
}

guint
dummy_provider_randomly_remove_task (DummyProvider *self)
{
  guint32 n_lists;
  guint32 i;
  guint32 j;

  g_return_val_if_fail (DUMMY_IS_PROVIDER (self), 0);

  if (self->number_of_tasks == 0)
    return 0;

  n_lists = g_random_int_range (1, 3);

  g_debug ("Removing tasks from %u task lists", n_lists);

  for (i = 0; i < n_lists; i++)
    {
      GSequenceIter *iter;
      GtdTaskList *list;
      guint32 list_position;
      guint32 n_tasks;
      guint n_list_tasks;

      n_tasks = 0;

      do
        {
          list_position = g_random_int_range (0, g_sequence_get_length (self->lists));

          /* The new list is the last one */
          iter = g_sequence_get_iter_at_pos (self->lists, list_position);
          list = g_sequence_get (iter);

          n_list_tasks = g_list_model_get_n_items (G_LIST_MODEL (list));

          if (n_list_tasks == 0)
            continue;

          /* Create a random number of stub tasks */
          if (n_list_tasks > 1)
            n_tasks = g_random_int_range (0, g_list_model_get_n_items (G_LIST_MODEL (list)));
          else
            n_tasks = 1;
        }
      while (n_tasks == 0);

      g_debug ("  Selected list was %s (%u)", gtd_task_list_get_name (list), list_position);
      g_debug ("  Removing %u tasks from the list", n_tasks);

      self->number_of_tasks -= n_tasks;

      for (j = 0; j < n_tasks; j++)
        {
          g_autoptr (GtdTask) task = NULL;
          guint task_position;

          if (n_list_tasks > 1)
            task_position = g_random_int_range (0, g_list_model_get_n_items (G_LIST_MODEL (list)) - 1);
          else
            task_position = 0;

          task = g_list_model_get_item (G_LIST_MODEL (list), task_position);

          g_debug ("    Removing task %u", task_position);

          gtd_task_list_remove_task (list, task);
        }

      if (self->number_of_tasks == 0)
        break;
    }

  return self->number_of_tasks;
}
