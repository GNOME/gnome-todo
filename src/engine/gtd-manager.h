/* gtd-manager.h
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

#pragma once

#include <gio/gio.h>

#include "gtd-object.h"
#include "gtd-types.h"

G_BEGIN_DECLS

#define GTD_TYPE_MANAGER (gtd_manager_get_type())

G_DECLARE_FINAL_TYPE (GtdManager, gtd_manager, GTD, MANAGER, GtdObject)

/**
 * GtdErrorActionFunc:
 */
typedef void         (*GtdErrorActionFunc)                       (GtdNotification    *notification,
                                                                  gpointer            user_data);


GtdManager*          gtd_manager_new                             (void);

GtdManager*          gtd_manager_get_default                     (void);

GList*               gtd_manager_get_task_lists                  (GtdManager         *manager);

GList*               gtd_manager_get_providers                   (GtdManager         *manager);

GList*               gtd_manager_get_panels                      (GtdManager         *manager);

GtdProvider*         gtd_manager_get_default_provider            (GtdManager         *manager);

void                 gtd_manager_set_default_provider            (GtdManager         *manager,
                                                                  GtdProvider        *provider);

GtdTaskList*         gtd_manager_get_default_task_list           (GtdManager         *self);

void                 gtd_manager_set_default_task_list           (GtdManager         *self,
                                                                  GtdTaskList        *list);

GSettings*           gtd_manager_get_settings                    (GtdManager         *manager);

gboolean             gtd_manager_get_is_first_run                (GtdManager         *manager);

void                 gtd_manager_set_is_first_run                (GtdManager         *manager,
                                                                  gboolean            is_first_run);

void                 gtd_manager_emit_error_message              (GtdManager         *manager,
                                                                  const gchar        *title,
                                                                  const gchar        *description,
                                                                  GtdErrorActionFunc  function,
                                                                  gpointer            user_data);

void                 gtd_manager_send_notification               (GtdManager         *self,
                                                                  GtdNotification    *notification);

GtdTimer*            gtd_manager_get_timer                       (GtdManager         *self);

G_END_DECLS
