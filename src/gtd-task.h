/* gtd-task.h
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

#ifndef GTD_TASK_H
#define GTD_TASK_H

#include "gtd-object.h"

#include <glib-object.h>

G_BEGIN_DECLS

#define GTD_TYPE_TASK (gtd_task_get_type())

G_DECLARE_DERIVABLE_TYPE (GtdTask, gtd_task, GTD, TASK, GtdObject)

struct _GtdTaskClass
{
  GtdObjectClass parent;

  gboolean      (*get_complete)                       (GtdTask              *self);
  void          (*set_complete)                       (GtdTask              *self,
                                                       gboolean              complete);

  GDateTime*    (*get_creation_date)                  (GtdTask              *self);
  void          (*set_creation_date)                  (GtdTask              *self,
                                                       GDateTime            *dt);

  GDateTime*    (*get_completion_date)                (GtdTask              *self);
  void          (*set_completion_date)                (GtdTask              *self,
                                                       GDateTime            *dt);

  const gchar*  (*get_description)                    (GtdTask              *self);
  void          (*set_description)                    (GtdTask              *self,
                                                       const gchar          *description);

  GDateTime*    (*get_due_date)                       (GtdTask              *self);
  void          (*set_due_date)                       (GtdTask              *self,
                                                       GDateTime            *due_date);

  gint64        (*get_position)                       (GtdTask              *task);
  void          (*set_position)                       (GtdTask              *task,
                                                       gint64                position);

  gint32        (*get_priority)                       (GtdTask              *self);
  void          (*set_priority)                       (GtdTask              *self,
                                                       gint32                priority);

  const gchar*  (*get_title)                          (GtdTask              *self);
  void          (*set_title)                          (GtdTask              *self,
                                                       const gchar          *title);

  /*< signals >*/

  void          (*subtask_added)                      (GtdTask              *self,
                                                       GtdTask              *subtask);

  void          (*subtask_removed)                    (GtdTask              *self,
                                                       GtdTask              *subtask);

  gpointer       padding[6];
};

GtdTask*            gtd_task_new                      (void);

gboolean            gtd_task_get_complete             (GtdTask              *task);

void                gtd_task_set_complete             (GtdTask              *task,
                                                       gboolean              complete);

GDateTime*          gtd_task_get_creation_date        (GtdTask              *task);

void                gtd_task_set_creation_date        (GtdTask              *task,
                                                       GDateTime            *dt);

GDateTime*          gtd_task_get_completion_date      (GtdTask              *task);

const gchar*        gtd_task_get_description          (GtdTask              *task);

void                gtd_task_set_description          (GtdTask              *task,
                                                       const gchar          *description);

GDateTime*          gtd_task_get_due_date             (GtdTask              *task);

void                gtd_task_set_due_date             (GtdTask              *task,
                                                       GDateTime            *dt);

GtdTaskList*        gtd_task_get_list                 (GtdTask              *task);

void                gtd_task_set_list                 (GtdTask              *task,
                                                       GtdTaskList          *list);

gint64              gtd_task_get_position             (GtdTask              *self);

void                gtd_task_set_position             (GtdTask              *self,
                                                       gint64                position);

gint                gtd_task_get_priority             (GtdTask              *task);

void                gtd_task_set_priority             (GtdTask              *task,
                                                       gint                  priority);

const gchar*        gtd_task_get_title                (GtdTask              *task);

void                gtd_task_set_title                (GtdTask              *task,
                                                       const gchar          *title);

gint                gtd_task_compare                  (GtdTask              *t1,
                                                       GtdTask              *t2);

GtdTask*            gtd_task_get_parent               (GtdTask              *self);

GList*              gtd_task_get_subtasks             (GtdTask              *self);

void                gtd_task_add_subtask              (GtdTask              *self,
                                                       GtdTask              *subtask);

void                gtd_task_remove_subtask           (GtdTask              *self,
                                                       GtdTask              *subtask);

gboolean            gtd_task_is_subtask               (GtdTask              *self,
                                                       GtdTask              *subtask);

gint                gtd_task_get_depth                (GtdTask              *self);

GtdProvider*        gtd_task_get_provider             (GtdTask              *self);

G_END_DECLS

#endif /* GTD_TASK_H */
