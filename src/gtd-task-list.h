/* gtd-task-list.h
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

#ifndef GTD_TASK_LIST_H
#define GTD_TASK_LIST_H

#include <glib-object.h>
#include <gtk/gtk.h>

#include "gtd-object.h"
#include "gtd-types.h"

G_BEGIN_DECLS

#define GTD_TYPE_TASK_LIST (gtd_task_list_get_type())

G_DECLARE_DERIVABLE_TYPE (GtdTaskList, gtd_task_list, GTD, TASK_LIST, GtdObject)

struct _GtdTaskListClass
{
  GtdObjectClass        parent;

  void                  (*task_added)                           (GtdTaskList            *list,
                                                                 GtdTask                *task);

  void                  (*task_updated)                         (GtdTaskList            *list,
                                                                 GtdTask                *task);

  void                  (*task_removed)                         (GtdTaskList            *list,
                                                                 GtdTask                *task);

  gpointer              padding[10];
};

GtdTaskList*            gtd_task_list_new                       (GtdProvider            *provider);

GdkRGBA*                gtd_task_list_get_color                 (GtdTaskList            *list);

void                    gtd_task_list_set_color                 (GtdTaskList            *list,
                                                                 const GdkRGBA          *color);

gboolean                gtd_task_list_is_removable              (GtdTaskList            *list);

void                    gtd_task_list_set_is_removable          (GtdTaskList            *list,
                                                                 gboolean                is_removable);

const gchar*            gtd_task_list_get_name                  (GtdTaskList            *list);

void                    gtd_task_list_set_name                  (GtdTaskList            *list,
                                                                 const gchar            *name);

GtdProvider*            gtd_task_list_get_provider              (GtdTaskList            *list);

void                    gtd_task_list_set_provider              (GtdTaskList            *self,
                                                                 GtdProvider            *provider);

void                    gtd_task_list_save_task                 (GtdTaskList            *list,
                                                                 GtdTask                *task);

void                    gtd_task_list_remove_task               (GtdTaskList            *list,
                                                                 GtdTask                *task);

gboolean                gtd_task_list_contains                  (GtdTaskList            *list,
                                                                 GtdTask                *task);

GtdTask*                gtd_task_list_get_task_by_id            (GtdTaskList            *self,
                                                                 const gchar            *id);

G_END_DECLS

#endif /* GTD_TASK_LIST_H */
