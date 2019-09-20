/* gtd-task.c
 *
 * Copyright (C) 2015 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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

#define G_LOG_DOMAIN "GtdTask"

#include "gtd-debug.h"
#include "gtd-task.h"
#include "gtd-task-list.h"

#include <glib/gi18n.h>

/**
 * SECTION:gtd-task
 * @short_description: a task
 * @title:GtdTask
 * @stability:Unstable
 * @see_also:#GtdTaskList
 *
 * A #GtdTask is an object that represents a task. All #GtdTasks
 * must be inside a #GtdTaskList.
 */

typedef struct
{
  gchar           *description;
  GtdTaskList     *list;

  GDateTime       *creation_date;
  GDateTime       *completion_date;
  GDateTime       *due_date;

  gchar           *title;

  GtdTask         *previous_sibling;
  GtdTask         *next_sibling;

  GtdTask         *parent_task;
  GtdTask         *first_subtask;
  GtdTask         *last_subtask;
  guint64          n_direct_subtasks;
  guint64          n_total_subtasks;
  gint             depth;

  gint32           priority;
  gint64           position;
  gboolean         complete;
} GtdTaskPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GtdTask, gtd_task, GTD_TYPE_OBJECT)

enum
{
  PROP_0,
  PROP_COMPLETE,
  PROP_DEPTH,
  PROP_DESCRIPTION,
  PROP_CREATION_DATE,
  PROP_DUE_DATE,
  PROP_LIST,
  PROP_PARENT,
  PROP_POSITION,
  PROP_TITLE,
  LAST_PROP
};

enum
{
  SUBTASK_ADDED,
  SUBTASK_REMOVED,
  N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0, };

static gboolean
share_same_ancestor (GtdTask *t1,
                     GtdTask *t2)
{
  GtdTask *p1, *p2;
  gint depth_difference;

  depth_difference = ABS (gtd_task_get_depth (t1) - gtd_task_get_depth (t2));

  /* Put the tasks at the same depth */
  if (depth_difference != 0)
    {
      gint i;

      for (i = 0; i < depth_difference; i++)
        {
          if (gtd_task_get_depth (t1) > gtd_task_get_depth (t2))
            t1 = gtd_task_get_parent (t1);
          else
            t2 = gtd_task_get_parent (t2);
        }
    }

  p1 = t1;
  p2 = t2;

  /* Walk up in the tree, if they match, we found the ancestor */
  while (p1 && p2)
    {
      if (p1 == p2)
        return TRUE;

      p1 = gtd_task_get_parent (p1);
      p2 = gtd_task_get_parent (p2);
    }

  return FALSE;
}

static GtdTask*
get_root_task (GtdTask *self)
{
  GtdTaskPrivate *priv = gtd_task_get_instance_private (self);
  GtdTask *aux = self;

  while (priv->parent_task)
    {
      aux = priv->parent_task;
      priv = gtd_task_get_instance_private (aux);
    }

  return aux;
}

static void
append_subtask (GtdTask *self,
                GtdTask *subtask)
{
  GtdTaskPrivate *priv, *subtask_priv;

  priv = gtd_task_get_instance_private (self);
  subtask_priv = gtd_task_get_instance_private (subtask);

  subtask_priv->previous_sibling = priv->last_subtask;

  if (priv->last_subtask)
    {
      GtdTaskPrivate *last_subtask_priv = gtd_task_get_instance_private (priv->last_subtask);
      last_subtask_priv->next_sibling = subtask;
    }

  priv->last_subtask = subtask;

  if (!priv->first_subtask)
    priv->first_subtask = subtask;
}

static void
remove_subtask (GtdTask *self,
                GtdTask *subtask)
{
  GtdTaskPrivate *priv, *subtask_priv;

  priv = gtd_task_get_instance_private (self);
  subtask_priv = gtd_task_get_instance_private (subtask);

  if (subtask_priv->previous_sibling)
    {
      GtdTaskPrivate *previous_subtask_sibling_priv = gtd_task_get_instance_private (subtask_priv->previous_sibling);
      previous_subtask_sibling_priv->next_sibling = subtask_priv->next_sibling;
    }
  else
    {
      /*
       * This is the first subtask, so advance the parent's first subtask
       * to the next one (which might be NULL).
       */
      priv->first_subtask = subtask_priv->next_sibling;
    }

  if (subtask_priv->next_sibling)
    {
      GtdTaskPrivate *next_subtask_sibling_priv = gtd_task_get_instance_private (subtask_priv->next_sibling);
      next_subtask_sibling_priv->previous_sibling = subtask_priv->previous_sibling;
    }
  else
    {
      /*
       * This is the last subtask, so rewind the parent's last subtask to
       * the previous one (which might be NULL).
       */
      priv->last_subtask = subtask_priv->previous_sibling;
    }
}

static gint
compare_by_subtasks (GtdTask **t1,
                     GtdTask **t2)
{
  GtdTask *task1, *task2;

  task1 = *t1;
  task2 = *t2;

  if (!task1 && !task2)
    return  0;
  if (!task1)
    return  1;
  if (!task2)
    return -1;

  if (share_same_ancestor (task1, task2))
    {
      gint depth_difference;

      depth_difference = ABS (gtd_task_get_depth (task1) - gtd_task_get_depth (task2));

      if (gtd_task_is_subtask (task1, task2))
        {
          return -1;
        }
      else if (gtd_task_is_subtask (task2, task1))
        {
          return 1;
        }
      else
        {
          GtdTask *p1, *p2;
          gint i;

          for (i = depth_difference; i > 0; i--)
            {
              if (gtd_task_get_depth (task1) > gtd_task_get_depth (task2))
                task1 = gtd_task_get_parent (task1);
              else
                task2 = gtd_task_get_parent (task2);
            }

          p1 = task1;
          p2 = task2;

          /* Walk up in the tree, if they match, we found the ancestor */
          while (gtd_task_get_parent (p1) && gtd_task_get_parent (p2))
            {
              if (gtd_task_get_parent (p1) == gtd_task_get_parent (p2))
                break;

              p1 = gtd_task_get_parent (p1);
              p2 = gtd_task_get_parent (p2);
            }

          *t1 = p1;
          *t2 = p2;
        }
    }
  else
    {
      /*
       * If either t1 or t2 have a parent, we have to compare them
       * solely based on the root task of the subtask tree.
       */
      *t1 = get_root_task (task1);
      *t2 = get_root_task (task2);
    }

  return 0;
}

static void
set_depth (GtdTask *self,
           gint     depth)
{
  GtdTaskPrivate *priv = gtd_task_get_instance_private (self);
  GtdTask *aux;

  priv->depth = depth;
  g_object_notify (G_OBJECT (self), "depth");

  aux = priv->first_subtask;
  while (aux)
    {
      GtdTaskPrivate *aux_priv = gtd_task_get_instance_private (aux);

      set_depth (aux, depth + 1);
      aux = aux_priv->next_sibling;
    }
}

static void
update_total_subtasks_counters (GtdTask *self,
                                gint64   total_difference)
{
  GtdTask *aux;

  aux = self;
  while (aux)
    {
      GtdTaskPrivate *aux_priv = gtd_task_get_instance_private (aux);

      aux_priv->n_total_subtasks += total_difference;

      aux = aux_priv->parent_task;
    }
}

static void
real_add_subtask (GtdTask *self,
                  GtdTask *subtask)
{
  GtdTaskPrivate *priv, *subtask_priv;

  GTD_ENTRY;

  g_assert (!gtd_task_is_subtask (self, subtask));

  priv = gtd_task_get_instance_private (self);
  subtask_priv = gtd_task_get_instance_private (subtask);

  /* First, remove the subtask from it's parent's subtasks list */
  if (subtask_priv->parent_task)
    gtd_task_remove_subtask (subtask_priv->parent_task, subtask);

  /* Append to this task's list of subtasks */
  append_subtask (self, subtask);

  /* Update counters */
  priv->n_direct_subtasks += 1;
  update_total_subtasks_counters (self, subtask_priv->n_total_subtasks + 1);

  GTD_TRACE_MSG ("Task %p is now subtask of %p", subtask, self);

  /* Update the subtask's parent property */
  subtask_priv->next_sibling = NULL;
  subtask_priv->parent_task = self;

  g_object_notify (G_OBJECT (subtask), "parent");

  /* And also the task's depth */
  set_depth (subtask, priv->depth + 1);

  GTD_EXIT;
}

static void
real_remove_subtask (GtdTask *self,
                     GtdTask *subtask)
{
  GtdTaskPrivate *priv, *subtask_priv;

  g_assert (gtd_task_is_subtask (self, subtask));

  priv = gtd_task_get_instance_private (self);
  subtask_priv = gtd_task_get_instance_private (subtask);

  /* Detach subtask from the linked list of subtasks */
  remove_subtask (self, subtask);

  /* Update counters */
  priv->n_direct_subtasks -= 1;
  update_total_subtasks_counters (self, -subtask_priv->n_total_subtasks - 1);

  GTD_TRACE_MSG ("Task %p was detached from %p", subtask, self);

  /* Update the subtask's parent and siblings */
  subtask_priv->previous_sibling = NULL;
  subtask_priv->next_sibling = NULL;
  subtask_priv->parent_task = NULL;

  g_object_notify (G_OBJECT (subtask), "parent");

  /* And also the task's depth */
  set_depth (subtask, 0);
}

static void
task_list_weak_notified (gpointer  data,
                         GObject  *where_the_object_was)
{
  GtdTask *task = GTD_TASK (data);
  GtdTaskPrivate *priv = gtd_task_get_instance_private (task);
  priv->list = NULL;
}

/*
 * GtdTask default implementations
 */

static GDateTime*
gtd_task_real_get_completion_date (GtdTask *self)
{
  GtdTaskPrivate *priv = gtd_task_get_instance_private (self);

  return priv->completion_date ? g_date_time_ref (priv->completion_date) : NULL;
}

static void
gtd_task_real_set_completion_date (GtdTask   *self,
                                   GDateTime *dt)
{
  GtdTaskPrivate *priv = gtd_task_get_instance_private (self);

  g_clear_pointer (&priv->completion_date, g_date_time_unref);
  priv->completion_date = dt ? g_date_time_ref (dt) : NULL;
}

static gboolean
gtd_task_real_get_complete (GtdTask *self)
{
  GtdTaskPrivate *priv = gtd_task_get_instance_private (self);

  return priv->complete;
}

static void
gtd_task_real_set_complete (GtdTask  *self,
                            gboolean  complete)
{
  GtdTaskPrivate *priv = gtd_task_get_instance_private (self);
  GDateTime *dt;

  dt = complete ? g_date_time_new_now_local () : NULL;
  gtd_task_real_set_completion_date (self, dt);

  priv->complete = complete;
}

static GDateTime*
gtd_task_real_get_creation_date (GtdTask *self)
{
  GtdTaskPrivate *priv = gtd_task_get_instance_private (self);

  return priv->creation_date ? g_date_time_ref (priv->creation_date) : NULL;
}

static void
gtd_task_real_set_creation_date (GtdTask   *self,
                                 GDateTime *dt)
{
  GtdTaskPrivate *priv = gtd_task_get_instance_private (self);

  g_clear_pointer (&priv->creation_date, g_date_time_unref);
  priv->creation_date = dt ? g_date_time_ref (dt) : NULL;
}

static const gchar*
gtd_task_real_get_description (GtdTask *self)
{
  GtdTaskPrivate *priv = gtd_task_get_instance_private (self);

  return priv->description ? priv->description : "";
}

static void
gtd_task_real_set_description (GtdTask     *self,
                               const gchar *description)
{
  GtdTaskPrivate *priv = gtd_task_get_instance_private (self);

  g_clear_pointer (&priv->description, g_free);
  priv->description = g_strdup (description);
}

static GDateTime*
gtd_task_real_get_due_date (GtdTask *self)
{
  GtdTaskPrivate *priv = gtd_task_get_instance_private (self);

  return priv->due_date ? g_date_time_ref (priv->due_date) : NULL;
}

static void
gtd_task_real_set_due_date (GtdTask   *self,
                            GDateTime *due_date)
{
  GtdTaskPrivate *priv = gtd_task_get_instance_private (self);

  g_clear_pointer (&priv->due_date, g_date_time_unref);

  if (due_date)
    priv->due_date = g_date_time_ref (due_date);
}

static gint64
gtd_task_real_get_position (GtdTask *self)
{
  GtdTaskPrivate *priv = gtd_task_get_instance_private (self);

  return priv->position;
}

static void
gtd_task_real_set_position (GtdTask *self,
                            gint64   position)
{
  GtdTaskPrivate *priv = gtd_task_get_instance_private (self);

  priv->position = position;
}

static const gchar*
gtd_task_real_get_title (GtdTask *self)
{
  GtdTaskPrivate *priv = gtd_task_get_instance_private (self);

  return priv->title;
}

static void
gtd_task_real_set_title (GtdTask     *self,
                         const gchar *title)
{
  GtdTaskPrivate *priv = gtd_task_get_instance_private (self);

  g_clear_pointer (&priv->title, g_free);
  priv->title = title ? g_strdup (title) : NULL;
}


/*
 * GObject overrides
 */

static void
gtd_task_finalize (GObject *object)
{
  GtdTask *self = (GtdTask*) object;
  GtdTaskPrivate *priv = gtd_task_get_instance_private (self);

  if (priv->list)
    g_object_weak_unref (G_OBJECT (priv->list), task_list_weak_notified, self);

  priv->list = NULL;
  g_free (priv->description);

  G_OBJECT_CLASS (gtd_task_parent_class)->finalize (object);
}

static void
gtd_task_get_property (GObject    *object,
                       guint       prop_id,
                       GValue     *value,
                       GParamSpec *pspec)
{
  GtdTask *self = GTD_TASK (object);
  GtdTaskPrivate *priv = gtd_task_get_instance_private (self);
  GDateTime *date;

  switch (prop_id)
    {
    case PROP_COMPLETE:
      g_value_set_boolean (value, gtd_task_get_complete (self));
      break;

    case PROP_CREATION_DATE:
      g_value_set_boxed (value, gtd_task_get_creation_date (self));
      break;

    case PROP_DEPTH:
      g_value_set_uint (value, priv->depth);
      break;

    case PROP_DESCRIPTION:
      g_value_set_string (value, gtd_task_get_description (self));
      break;

    case PROP_DUE_DATE:
      date = gtd_task_get_due_date (self);
      g_value_set_boxed (value, date);
      g_clear_pointer (&date, g_date_time_unref);
      break;

    case PROP_LIST:
      g_value_set_object (value, priv->list);
      break;

    case PROP_PARENT:
      g_value_set_object (value, priv->parent_task);
      break;

    case PROP_POSITION:
      g_value_set_int64 (value, gtd_task_get_position (self));
      break;

    case PROP_TITLE:
      g_value_set_string (value, gtd_task_get_title (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_task_set_property (GObject      *object,
                       guint         prop_id,
                       const GValue *value,
                       GParamSpec   *pspec)
{
  GtdTask *self = GTD_TASK (object);

  switch (prop_id)
    {
    case PROP_COMPLETE:
      gtd_task_set_complete (self, g_value_get_boolean (value));
      break;

    case PROP_CREATION_DATE:
      gtd_task_set_creation_date (self, g_value_get_boxed (value));
      break;

    case PROP_DESCRIPTION:
      gtd_task_set_description (self, g_value_get_string (value));
      break;

    case PROP_DUE_DATE:
      gtd_task_set_due_date (self, g_value_get_boxed (value));
      break;

    case PROP_LIST:
      gtd_task_set_list (self, g_value_get_object (value));
      break;

    case PROP_POSITION:
      gtd_task_set_position (self, g_value_get_int64 (value));
      break;

    case PROP_TITLE:
      gtd_task_set_title (self, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_task_class_init (GtdTaskClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  klass->get_complete = gtd_task_real_get_complete;
  klass->set_complete = gtd_task_real_set_complete;
  klass->get_creation_date = gtd_task_real_get_creation_date;
  klass->set_creation_date = gtd_task_real_set_creation_date;
  klass->get_completion_date = gtd_task_real_get_completion_date;
  klass->set_completion_date = gtd_task_real_set_completion_date;
  klass->get_description = gtd_task_real_get_description;
  klass->set_description = gtd_task_real_set_description;
  klass->get_due_date = gtd_task_real_get_due_date;
  klass->set_due_date = gtd_task_real_set_due_date;
  klass->get_position = gtd_task_real_get_position;
  klass->set_position = gtd_task_real_set_position;
  klass->get_title = gtd_task_real_get_title;
  klass->set_title = gtd_task_real_set_title;
  klass->subtask_added = real_add_subtask;
  klass->subtask_removed = real_remove_subtask;

  object_class->finalize = gtd_task_finalize;
  object_class->get_property = gtd_task_get_property;
  object_class->set_property = gtd_task_set_property;

  /**
   * GtdTask::complete:
   *
   * @TRUE if the task is marked as complete or @FALSE otherwise. Usually
   * represented by a checkbox at user interfaces.
   */
  g_object_class_install_property (
        object_class,
        PROP_COMPLETE,
        g_param_spec_boolean ("complete",
                              "Whether the task is completed or not",
                              "Whether the task is marked as completed by the user",
                              FALSE,
                              G_PARAM_READWRITE));

  /**
   * GtdTask::creation-date:
   *
   * The @GDateTime that represents the time in which the task was created.
   */
  g_object_class_install_property (
        object_class,
        PROP_CREATION_DATE,
        g_param_spec_boxed ("creation-date",
                            "Creation date of the task",
                            "The day the task was created.",
                            G_TYPE_DATE_TIME,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GtdTask::depth:
   *
   * The depth of the task inside the subtask tree.
   */
  g_object_class_install_property (
        object_class,
        PROP_DEPTH,
        g_param_spec_uint ("depth",
                           "Depth of the task",
                           "The depth of the task inside the subtasks tree.",
                           0,
                           G_MAXUINT,
                           0,
                           G_PARAM_READABLE));

  /**
   * GtdTask::description:
   *
   * Description of the task.
   */
  g_object_class_install_property (
        object_class,
        PROP_DESCRIPTION,
        g_param_spec_string ("description",
                             "Description of the task",
                             "Optional string describing the task",
                             NULL,
                             G_PARAM_READWRITE));

  /**
   * GtdTask::due-date:
   *
   * The @GDateTime that represents the time in which the task should
   * be completed before.
   */
  g_object_class_install_property (
        object_class,
        PROP_DUE_DATE,
        g_param_spec_boxed ("due-date",
                            "End date of the task",
                            "The day the task is supposed to be completed",
                            G_TYPE_DATE_TIME,
                            G_PARAM_READWRITE));

  /**
   * GtdTask::list:
   *
   * The @GtdTaskList that contains this task.
   */
  g_object_class_install_property (
        object_class,
        PROP_LIST,
        g_param_spec_object ("list",
                             "List of the task",
                             "The list that owns this task",
                             GTD_TYPE_TASK_LIST,
                             G_PARAM_READWRITE));

  /**
   * GtdTask::parent:
   *
   * The parent of the task.
   */
  g_object_class_install_property (
        object_class,
        PROP_PARENT,
        g_param_spec_object ("parent",
                              "Parent of the task",
                              "The GtdTask that is parent of this task.",
                              GTD_TYPE_TASK,
                              G_PARAM_READABLE));

  /**
   * GtdTask::position:
   *
   * Position of the task, -1 if not set.
   */
  g_object_class_install_property (
        object_class,
        PROP_POSITION,
        g_param_spec_int64 ("position",
                            "Position of the task",
                            "The position of the task. -1 means no position, and tasks will be sorted alphabetically.",
                            -1,
                            G_MAXINT64,
                            0,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GtdTask::title:
   *
   * The title of the task, usually the task name.
   */
  g_object_class_install_property (
        object_class,
        PROP_TITLE,
        g_param_spec_string ("title",
                             "Title of the task",
                             "The title of the task",
                             NULL,
                             G_PARAM_READWRITE));

  /* Signals */
  /**
   * GtdTask:subtask-added:
   *
   * Emited when a subtask is added to @self.
   */
  signals[SUBTASK_ADDED] = g_signal_new ("subtask-added",
                                         GTD_TYPE_TASK,
                                         G_SIGNAL_RUN_LAST,
                                         G_STRUCT_OFFSET (GtdTaskClass, subtask_added),
                                         NULL,
                                         NULL,
                                         NULL,
                                         G_TYPE_NONE,
                                         1,
                                         GTD_TYPE_TASK);

  /**
   * GtdTask:subtask-removed:
   *
   * Emited when a subtask is removed from @self.
   */
  signals[SUBTASK_REMOVED] = g_signal_new ("subtask-removed",
                                           GTD_TYPE_TASK,
                                           G_SIGNAL_RUN_LAST,
                                           G_STRUCT_OFFSET (GtdTaskClass, subtask_removed),
                                           NULL,
                                           NULL,
                                           NULL,
                                           G_TYPE_NONE,
                                           1,
                                           GTD_TYPE_TASK);
}

static void
gtd_task_init (GtdTask *self)
{
  GtdTaskPrivate *priv = gtd_task_get_instance_private (self);

  priv->position = -1;
}

/**
 * gtd_task_new:
 *
 * Creates a new #GtdTask
 *
 * Returns: (transfer full): a #GtdTask
 */
GtdTask *
gtd_task_new (void)
{
  return g_object_new (GTD_TYPE_TASK, NULL);
}

/**
 * gtd_task_get_complete:
 * @task: a #GtdTask
 *
 * Retrieves whether the task is complete or not.
 *
 * Returns: %TRUE if the task is complete, %FALSE otherwise
 */
gboolean
gtd_task_get_complete (GtdTask *task)
{
  g_return_val_if_fail (GTD_IS_TASK (task), FALSE);

  return GTD_TASK_CLASS (G_OBJECT_GET_CLASS (task))->get_complete (task);
}

/**
 * gtd_task_set_complete:
 * @task: a #GtdTask
 * @complete: the new value
 *
 * Updates the complete state of @task.
 */
void
gtd_task_set_complete (GtdTask  *task,
                       gboolean  complete)
{
  g_return_if_fail (GTD_IS_TASK (task));

  if (gtd_task_get_complete (task) == complete)
    return;

  GTD_TASK_CLASS (G_OBJECT_GET_CLASS (task))->set_complete (task, complete);

  g_object_notify (G_OBJECT (task), "complete");
}

/**
 * gtd_task_get_creation_date:
 * @task: a #GtdTask
 *
 * Returns the #GDateTime that represents the task's creation date.
 * The value is referenced for thread safety. Returns %NULL if
 * no date is set.
 *
 * Returns: (transfer full): the internal #GDateTime referenced
 * for thread safety, or %NULL. Unreference it after use.
 */
GDateTime*
gtd_task_get_creation_date (GtdTask *task)
{
  g_return_val_if_fail (GTD_IS_TASK (task), NULL);

  return GTD_TASK_CLASS (G_OBJECT_GET_CLASS (task))->get_creation_date (task);
}

/**
 * gtd_task_set_creation_date:
 * @task: a #GtdTask
 * @dt: (nullable): a #GDateTime
 *
 * Sets the creation date of @task.
 */
void
gtd_task_set_creation_date (GtdTask   *task,
                            GDateTime *dt)
{
  g_return_if_fail (GTD_IS_TASK (task));

  if (gtd_task_get_creation_date (task) == dt)
    return;

  GTD_TASK_CLASS (G_OBJECT_GET_CLASS (task))->set_creation_date (task, dt);

  g_object_notify (G_OBJECT (task), "complete");
}

/**
 * gtd_task_get_completion_date:
 * @task: a #GtdTask
 *
 * Returns the #GDateTime that represents the task's completion date.
 * Returns %NULL if no date is set.
 *
 * Returns: (transfer full)(nullable): the internal #GDateTime or %NULL.
 * Unreference it after use.
 */
GDateTime*
gtd_task_get_completion_date (GtdTask *task)
{
  g_return_val_if_fail (GTD_IS_TASK (task), NULL);

  return GTD_TASK_CLASS (G_OBJECT_GET_CLASS (task))->get_completion_date (task);
}

/**
 * gtd_task_get_description:
 * @task: a #GtdTask
 *
 * Retrieves the description of the task.
 *
 * Returns: (transfer none): the description of @task
 */
const gchar*
gtd_task_get_description (GtdTask *task)
{
  g_return_val_if_fail (GTD_IS_TASK (task), NULL);

  return GTD_TASK_CLASS (G_OBJECT_GET_CLASS (task))->get_description (task);
}

/**
 * gtd_task_set_description:
 * @task: a #GtdTask
 * @description: (nullable): the new description, or %NULL
 *
 * Updates the description of @task. The string is not stripped off of
 * spaces to preserve user data.
 */
void
gtd_task_set_description (GtdTask     *task,
                          const gchar *description)
{
  GtdTaskPrivate *priv;

  g_assert (GTD_IS_TASK (task));
  g_assert (g_utf8_validate (description, -1, NULL));

  priv = gtd_task_get_instance_private (task);

  if (g_strcmp0 (priv->description, description) == 0)
    return;

  GTD_TASK_CLASS (G_OBJECT_GET_CLASS (task))->set_description (task, description);

  g_object_notify (G_OBJECT (task), "description");
}

/**
 * gtd_task_get_due_date:
 * @task: a #GtdTask
 *
 * Returns the #GDateTime that represents the task's due date.
 * The value is referenced for thread safety. Returns %NULL if
 * no date is set.
 *
 * Returns: (transfer full): the internal #GDateTime referenced
 * for thread safety, or %NULL. Unreference it after use.
 */
GDateTime*
gtd_task_get_due_date (GtdTask *task)
{
  g_return_val_if_fail (GTD_IS_TASK (task), NULL);

  return GTD_TASK_CLASS (G_OBJECT_GET_CLASS (task))->get_due_date (task);
}

/**
 * gtd_task_set_due_date:
 * @task: a #GtdTask
 * @dt: (nullable): a #GDateTime
 *
 * Updates the internal @GtdTask::due-date property.
 */
void
gtd_task_set_due_date (GtdTask   *task,
                       GDateTime *dt)
{
  g_autoptr (GDateTime) current_dt = NULL;

  g_assert (GTD_IS_TASK (task));

  current_dt = gtd_task_get_due_date (task);

  /* Don't do anything if the date is equal */
  if (current_dt == dt || (current_dt && dt && g_date_time_equal (current_dt, dt)))
    return;

  GTD_TASK_CLASS (G_OBJECT_GET_CLASS (task))->set_due_date (task, dt);

  g_object_notify (G_OBJECT (task), "due-date");
}

/**
 * gtd_task_get_list:
 *
 * Returns a weak reference to the #GtdTaskList that
 * owns the given @task.
 *
 * Returns: (transfer none): a weak reference to the
 * #GtdTaskList that owns @task. Do not free after
 * usage.
 */
GtdTaskList*
gtd_task_get_list (GtdTask *task)
{
  GtdTaskPrivate *priv;

  g_return_val_if_fail (GTD_IS_TASK (task), NULL);

  priv = gtd_task_get_instance_private (task);

  return priv->list;
}

/**
 * gtd_task_set_list:
 * @task: a #GtdTask
 * @list: (nullable): a #GtdTaskList
 *
 * Sets the parent #GtdTaskList of @task.
 */
void
gtd_task_set_list (GtdTask     *task,
                   GtdTaskList *list)
{
  GtdTaskPrivate *priv;

  g_assert (GTD_IS_TASK (task));
  g_assert (GTD_IS_TASK_LIST (list));

  priv = gtd_task_get_instance_private (task);

  if (priv->list == list)
    return;

  if (priv->list)
    g_object_weak_unref (G_OBJECT (priv->list), task_list_weak_notified, task);

  priv->list = list;
  g_object_weak_ref (G_OBJECT (priv->list), task_list_weak_notified, task);
  g_object_notify (G_OBJECT (task), "list");
}

/**
 * gtd_task_get_position:
 * @task: a #GtdTask
 *
 * Returns the position of @task inside the parent #GtdTaskList,
 * or -1 if not set.
 *
 * Returns: the position of the task, or -1
 */
gint64
gtd_task_get_position (GtdTask *task)
{
  g_return_val_if_fail (GTD_IS_TASK (task), -1);

  return GTD_TASK_CLASS (G_OBJECT_GET_CLASS (task))->get_position (task);
}

/**
 * gtd_task_set_position:
 * @task: a #GtdTask
 * @position: the priority of @task, or -1
 *
 * Sets the @task position inside the parent #GtdTaskList. It
 * is up to the interface to handle two or more #GtdTask with
 * the same position value.
 */
void
gtd_task_set_position (GtdTask *task,
                       gint64   position)
{
  g_return_if_fail (GTD_IS_TASK (task));

  if (gtd_task_get_position (task) == position)
    return;

  GTD_TASK_CLASS (G_OBJECT_GET_CLASS (task))->set_position (task, position);

  g_object_notify (G_OBJECT (task), "position");
}

/**
 * gtd_task_get_title:
 * @task: a #GtdTask
 *
 * Retrieves the title of the task, or %NULL.
 *
 * Returns: (transfer none): the title of @task, or %NULL
 */
const gchar*
gtd_task_get_title (GtdTask *task)
{
  const gchar *title;

  g_return_val_if_fail (GTD_IS_TASK (task), NULL);

  title = GTD_TASK_CLASS (G_OBJECT_GET_CLASS (task))->get_title (task);

  return title ? title : "";
}

/**
 * gtd_task_set_title:
 * @task: a #GtdTask
 * @title: (nullable): the new title, or %NULL
 *
 * Updates the title of @task. The string is stripped off of
 * leading spaces.
 */
void
gtd_task_set_title (GtdTask     *task,
                    const gchar *title)
{
  const gchar *current_title;

  g_return_if_fail (GTD_IS_TASK (task));
  g_return_if_fail (g_utf8_validate (title, -1, NULL));

  current_title = gtd_task_get_title (task);

  if (g_strcmp0 (current_title, title) == 0)
    return;

  GTD_TASK_CLASS (G_OBJECT_GET_CLASS (task))->set_title (task, title);

  g_object_notify (G_OBJECT (task), "title");
}

/**
 * gtd_task_compare:
 * @t1: (nullable): a #GtdTask
 * @t2: (nullable): a #GtdTask
 *
 * Compare @t1 and @t2.
 *
 * Returns: %-1 if @t1 comes before @t2, %1 for the opposite, %0 if they're equal
 */
gint
gtd_task_compare (GtdTask *t1,
                  GtdTask *t2)
{
  GDateTime *dt1;
  GDateTime *dt2;
  gchar *txt1;
  gchar *txt2;
  gint retval;

  if (!t1 && !t2)
    return  0;
  if (!t1)
    return  1;
  if (!t2)
    return -1;

  /*
   * The custom position overrides any comparison we can make. To keep compatibility,
   * for now, we only compare by position if both tasks have a custom position set.
   */
  if (gtd_task_get_position (t1) != -1 && gtd_task_get_position (t2) != -1)
    {
      retval = gtd_task_get_position (t1) - gtd_task_get_position (t2);

      if (retval != 0)
        return retval;
    }

  /*
   * Zero, compare by subtask hierarchy
   */
  retval = compare_by_subtasks (&t1, &t2);

  if (retval != 0)
    return retval;

  /*
   * Second, compare by ::due-date.
   */
  dt1 = gtd_task_get_due_date (t1);
  dt2 = gtd_task_get_due_date (t2);

  if (!dt1 && !dt2)
    retval =  0;
  else if (!dt1)
    retval =  1;
  else if (!dt2)
    retval = -1;
  else
    retval = g_date_time_compare (dt1, dt2);

  if (dt1)
    g_date_time_unref (dt1);
  if (dt2)
    g_date_time_unref (dt2);

  if (retval != 0)
    return retval;

  /*
   * Fourth, compare by ::creation-date.
   */
  dt1 = gtd_task_get_creation_date (t1);
  dt2 = gtd_task_get_creation_date (t2);

  if (!dt1 && !dt2)
    retval =  0;
  else if (!dt1)
    retval =  1;
  else if (!dt2)
    retval = -1;
  else
    retval = g_date_time_compare (dt1, dt2);

  g_clear_pointer (&dt1, g_date_time_unref);
  g_clear_pointer (&dt2, g_date_time_unref);

  if (retval != 0)
    return retval;

  /*
   * If they're equal up to now, compare by title.
   */
  txt1 = txt2 = NULL;

  txt1 = g_utf8_casefold (gtd_task_get_title (t1), -1);
  txt2 = g_utf8_casefold (gtd_task_get_title (t2), -1);

  retval = g_strcmp0 (txt1, txt2);

  g_free (txt1);
  g_free (txt2);

  return retval;
}

/**
 * gtd_task_get_parent:
 * @self: a #GtdTask
 *
 * Retrieves the parent task of @self, or %NULL if none is set.
 *
 * Returns: (transfer none)(nullable): the #GtdTask that is parent of @self
 */
GtdTask*
gtd_task_get_parent (GtdTask *self)
{
  GtdTaskPrivate *priv;

  g_return_val_if_fail (GTD_IS_TASK (self), NULL);

  priv = gtd_task_get_instance_private (self);

  return priv->parent_task;
}

/**
 * gtd_task_add_subtask:
 * @self: a #GtdTask
 * @subtask: the subtask to be added to @self
 *
 * Adds @subtask as a subtask of @self.
 */
void
gtd_task_add_subtask (GtdTask *self,
                      GtdTask *subtask)
{
  g_return_if_fail (GTD_IS_TASK (self));
  g_return_if_fail (GTD_IS_TASK (subtask));
  g_return_if_fail (!gtd_task_is_subtask (self, subtask));

  g_signal_emit (self, signals[SUBTASK_ADDED], 0, subtask);
}

/**
 * gtd_task_remove_subtask:
 * @self: a #GtdTask
 * @subtask: the subtask to be removed to @self
 *
 * Removes @subtask from @self.
 */
void
gtd_task_remove_subtask (GtdTask *self,
                         GtdTask *subtask)
{
  g_return_if_fail (GTD_IS_TASK (self));
  g_return_if_fail (GTD_IS_TASK (subtask));
  g_return_if_fail (gtd_task_is_subtask (self, subtask));

  g_signal_emit (self, signals[SUBTASK_REMOVED], 0, subtask);
}

/**
 * gtd_task_is_subtask:
 * @self: a #GtdTask
 * @subtask: a #GtdTask
 *
 * Checks if @subtask is a subtask of @self, directly or indirectly.
 *
 * Returns: %TRUE is @subtask is a subtask of @self, %FALSE otherwise
 */
gboolean
gtd_task_is_subtask (GtdTask *self,
                     GtdTask *subtask)
{
  GtdTask *aux;

  g_return_val_if_fail (GTD_IS_TASK (self), FALSE);
  g_return_val_if_fail (GTD_IS_TASK (subtask), FALSE);

  aux = subtask;
  while (aux)
    {
      GtdTaskPrivate *aux_priv = gtd_task_get_instance_private (aux);

      if (aux == self)
        return TRUE;

      aux = aux_priv->parent_task;
    }

  return FALSE;
}

/**
 * gtd_task_get_depth:
 * @self: a #GtdTask
 *
 * Retrieves the depth of @self in the subtasks tree.
 *
 * Returns: the depth of the task.
 */
gint
gtd_task_get_depth (GtdTask *self)
{
  GtdTaskPrivate *priv;

  g_return_val_if_fail (GTD_IS_TASK (self), 0);

  priv = gtd_task_get_instance_private (self);

  return priv->depth;
}

/**
 * gtd_task_get_first_subtask:
 * @self: a #GtdTaskList
 *
 * Return the first subtask of @self. This is useful for
 * copy-less iteration, for example:
 *
 * |[<!-- language="C" -->
 * for (aux = gtd_task_get_first_subtask (task);
 *      aux;
 *      aux = gtd_task_get_next_sibling (aux))
 *   {
 *     // Do something with aux
 *   }
 * ]|
 *
 * The siblings have no particular order. See also
 * gtd_task_get_previous_sibling() and gtd_task_get_next_sibling().
 *
 * Returns: (nullable): the first subtask of @self
 */
GtdTask*
gtd_task_get_first_subtask (GtdTask *self)
{
  GtdTaskPrivate *priv;

  g_return_val_if_fail (GTD_IS_TASK (self), 0);

  priv = gtd_task_get_instance_private (self);

  return priv->first_subtask;
}

/**
 * gtd_task_get_previous_sibling:
 * @self: a #GtdTaskList
 *
 * Retrieves the previous sibling of @self. This is useful for
 * copy-less iteration.
 *
 * Only subtasks have a previous and a next sibling set. Root
 * tasks depend do not have them, and it is a programming error
 * to assume so.
 *
 * The siblings have no particular order. It is a programming
 * error to assume that siblings are sorted.
 *
 * Returns: (nullable): the previous sibling of @self
 */
GtdTask*
gtd_task_get_previous_sibling (GtdTask *self)
{
  GtdTaskPrivate *priv;

  g_return_val_if_fail (GTD_IS_TASK (self), 0);

  priv = gtd_task_get_instance_private (self);

  return priv->previous_sibling;
}

/**
 * gtd_task_get_next_sibling:
 * @self: a #GtdTaskList
 *
 * Retrieves the next sibling of @self. This is useful for
 * copy-less iteration, for example:
 *
 * |[<!-- language="C" -->
 * for (aux = gtd_task_get_first_subtask (task);
 *      aux;
 *      aux = gtd_task_get_next_sibling (aux))
 *   {
 *     // Do something with aux
 *   }
 * ]|
 *
 * Only subtasks have a previous and a next sibling set. Root
 * tasks depend do not have them, and it is a programming error
 * to assume so.
 *
 * The siblings have no particular order. It is a programming
 * error to assume that siblings are sorted.
 *
 * Returns: (nullable): the next sibling of @self
 */
GtdTask*
gtd_task_get_next_sibling (GtdTask *self)
{
  GtdTaskPrivate *priv;

  g_return_val_if_fail (GTD_IS_TASK (self), 0);

  priv = gtd_task_get_instance_private (self);

  return priv->next_sibling;
}

/**
 * gtd_task_get_n_direct_subtasks:
 * @self: a #GtdTaskList
 *
 * Returns the number of only direct subtasks of this
 * task. This number is updated when adding and removing
 * subtasks, and cached.
 *
 * Returns: the number of direct subtasks of @self
 */
guint64
gtd_task_get_n_direct_subtasks (GtdTask *self)
{
  GtdTaskPrivate *priv;

  g_return_val_if_fail (GTD_IS_TASK (self), 0);

  priv = gtd_task_get_instance_private (self);

  return priv->n_direct_subtasks;
}

/**
 * gtd_task_get_n_total_subtasks:
 * @self: a #GtdTaskList
 *
 * Returns the number of direct and indirect subtasks of
 * this task. This number is updated when adding and removing
 * subtasks, and cached.
 *
 * Returns: the number of direct and indirect subtasks of @self
 */
guint64
gtd_task_get_n_total_subtasks (GtdTask *self)
{
  GtdTaskPrivate *priv;

  g_return_val_if_fail (GTD_IS_TASK (self), 0);

  priv = gtd_task_get_instance_private (self);

  return priv->n_total_subtasks;
}

/**
 * gtd_task_get_provider:
 * @self: a #GtdTaskList
 *
 * Utility function to retrieve the data provider that backs this
 * task. Notice that this is exactly the same as writting:
 *
 * |[<!-- language="C" -->
 * GtdTaskList *list;
 * GtdProvider *provider;
 *
 * list = gtd_task_get_list (task);
 * provider = gtd_task_list_get_provider (list);
 * ]|
 *
 * Returns: (transfer none)(nullable): the #GtdProvider of this task's list.
 */
GtdProvider*
gtd_task_get_provider (GtdTask *self)
{
  GtdTaskPrivate *priv;

  g_return_val_if_fail (GTD_IS_TASK (self), NULL);

  priv = gtd_task_get_instance_private (self);

  if (priv->list)
    return gtd_task_list_get_provider (priv->list);

  return NULL;
}
