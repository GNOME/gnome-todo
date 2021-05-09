/* gtd-task-list-view-model.h
 *
 * Copyright 2020 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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

#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define GTD_TYPE_SENTINEL (gtd_sentinel_get_type ())
G_DECLARE_FINAL_TYPE (GtdSentinel, gtd_sentinel, GTD, SENTINEL, GObject)

#define GTD_TYPE_TASK_LIST_VIEW_MODEL (gtd_task_list_view_model_get_type())
G_DECLARE_FINAL_TYPE (GtdTaskListViewModel, gtd_task_list_view_model, GTD, TASK_LIST_VIEW_MODEL, GObject)

GtdTaskListViewModel* gtd_task_list_view_model_new (void);

GListModel* gtd_task_list_view_model_get_model (GtdTaskListViewModel *self);

void gtd_task_list_view_model_set_model (GtdTaskListViewModel *self,
                                         GListModel           *model);

gboolean gtd_task_list_view_model_is_sentinel (GtdTaskListViewModel *self,
                                               guint                 position);

G_END_DECLS
