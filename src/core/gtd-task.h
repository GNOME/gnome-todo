/* gtd-task.h
 *
 * Copyright (C) 2015-2020 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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

  gboolean      (*get_important)                      (GtdTask              *self);
  void          (*set_important)                      (GtdTask              *self,
                                                       gboolean              important);

  gint64        (*get_position)                       (GtdTask              *task);
  void          (*set_position)                       (GtdTask              *task,
                                                       gint64                position);

  const gchar*  (*get_title)                          (GtdTask              *self);
  void          (*set_title)                          (GtdTask              *self,
                                                       const gchar          *title);

  gpointer       padding[8];
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

gboolean            gtd_task_get_important            (GtdTask              *self);

void                gtd_task_set_important            (GtdTask              *self,
                                                       gboolean              important);

GtdTaskList*        gtd_task_get_list                 (GtdTask              *task);

void                gtd_task_set_list                 (GtdTask              *task,
                                                       GtdTaskList          *list);

gint64              gtd_task_get_position             (GtdTask              *self);

void                gtd_task_set_position             (GtdTask              *self,
                                                       gint64                position);

const gchar*        gtd_task_get_title                (GtdTask              *task);

void                gtd_task_set_title                (GtdTask              *task,
                                                       const gchar          *title);

gint                gtd_task_compare                  (GtdTask              *t1,
                                                       GtdTask              *t2);

GtdProvider*        gtd_task_get_provider             (GtdTask              *self);

G_END_DECLS

#endif /* GTD_TASK_H */
