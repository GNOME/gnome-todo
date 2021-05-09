/* gtd-task-list-view-model.c
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

#include "gtd-task-list.h"
#include "gtd-task-list-view-model.h"


/*
 * Sentinel
 */

struct _GtdSentinel
{
  GObject parent_instance;
};

G_DEFINE_TYPE (GtdSentinel, gtd_sentinel, G_TYPE_OBJECT);

static void
gtd_sentinel_init (GtdSentinel *self)
{
}

static void
gtd_sentinel_class_init (GtdSentinelClass *klass)
{
}



struct _GtdTaskListViewModel
{
  GObject             parent_instance;

  GtdSentinel        *sentinel;

  GListModel         *model;
  guint               n_items;
};

static void g_list_model_iface_init (GListModelInterface *iface);

G_DEFINE_TYPE_WITH_CODE (GtdTaskListViewModel, gtd_task_list_view_model, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, g_list_model_iface_init))


/*
 * Auxiliary methods
 */

static void
update_n_items (GtdTaskListViewModel *self,
                guint                 position,
                guint                 removed,
                guint                 added)
{
  self->n_items = self->n_items - removed + added;
  g_list_model_items_changed (G_LIST_MODEL (self), position, removed, added);
}


/*
 * Callbacks
 */

static void
on_model_items_changed_cb (GListModel           *model,
                           guint                 position,
                           guint                 removed,
                           guint                 added,
                           GtdTaskListViewModel *self)
{
  update_n_items (self, position, removed, added);
}


/*
 * GListModel interface
 */

static guint
gtd_task_list_view_model_get_n_items (GListModel *model)
{
  GtdTaskListViewModel *self = (GtdTaskListViewModel *)model;

  return self->n_items + 1;
}

static GType
gtd_task_list_view_model_get_item_type (GListModel *model)
{
  return G_TYPE_OBJECT;
}

static gpointer
gtd_task_list_view_model_get_item (GListModel *model,
                                   guint       position)
{
  GtdTaskListViewModel *self = (GtdTaskListViewModel *)model;

  if (gtd_task_list_view_model_is_sentinel (self, position))
    return g_object_ref (self->sentinel);

  return g_list_model_get_item (self->model, position);
}

static void
g_list_model_iface_init (GListModelInterface *iface)
{
  iface->get_n_items = gtd_task_list_view_model_get_n_items;
  iface->get_item_type = gtd_task_list_view_model_get_item_type;
  iface->get_item = gtd_task_list_view_model_get_item;
}


/*
 * GObject overrides
 */

static void
gtd_task_list_view_model_finalize (GObject *object)
{
  GtdTaskListViewModel *self = (GtdTaskListViewModel *)object;

  g_clear_object (&self->model);
  g_clear_object (&self->sentinel);

  G_OBJECT_CLASS (gtd_task_list_view_model_parent_class)->finalize (object);
}

static void
gtd_task_list_view_model_class_init (GtdTaskListViewModelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gtd_task_list_view_model_finalize;
}

static void
gtd_task_list_view_model_init (GtdTaskListViewModel *self)
{
  self->sentinel = g_object_new (GTD_TYPE_SENTINEL, NULL);
}

GtdTaskListViewModel *
gtd_task_list_view_model_new (void)
{
  return g_object_new (GTD_TYPE_TASK_LIST_VIEW_MODEL, NULL);
}

GListModel *
gtd_task_list_view_model_get_model (GtdTaskListViewModel *self)
{
  return self->model;
}

void
gtd_task_list_view_model_set_model (GtdTaskListViewModel *self,
                                    GListModel           *model)
{
  guint old_n_items = 0;
  guint new_n_items = 0;

  if (G_UNLIKELY (self->model == model))
    return;

  old_n_items = self->n_items;

  if (self->model)
    {
      g_signal_handlers_disconnect_by_func (self->model,
                                            on_model_items_changed_cb,
                                            self);
    }

  g_clear_object (&self->model);

  if (model)
    {
      self->model = g_object_ref (model);

      g_signal_connect_object (model,
                               "items-changed",
                               G_CALLBACK (on_model_items_changed_cb),
                               self,
                               0);

      new_n_items = g_list_model_get_n_items (model);
    }

  update_n_items (self, 0, old_n_items, new_n_items);
}

gboolean
gtd_task_list_view_model_is_sentinel (GtdTaskListViewModel *self,
                                      guint                 position)
{
  return position == self->n_items;
}
