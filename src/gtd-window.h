/* gtd-window.h
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


#ifndef GTD_WINDOW_H
#define GTD_WINDOW_H

#include "gtd-types.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GTD_TYPE_WINDOW (gtd_window_get_type())

G_DECLARE_FINAL_TYPE (GtdWindow, gtd_window, GTD, WINDOW, GtkApplicationWindow)

GtkWidget*                gtd_window_new                  (GtdApplication       *application);

GtdManager*               gtd_window_get_manager          (GtdWindow            *window);

void                      gtd_window_notify               (GtdWindow            *window,
                                                           GtdNotification      *notification);

void                      gtd_window_cancel_notification  (GtdWindow             *window,
                                                           GtdNotification      *notification);

GtdWindowMode             gtd_window_get_mode             (GtdWindow             *window);

void                      gtd_window_set_mode             (GtdWindow             *window,
                                                           GtdWindowMode          mode);

void                      gtd_window_set_custom_title     (GtdWindow             *window,
                                                           const gchar           *title,
                                                           const gchar           *subtitle);

void                      gtd_window_open_event_by_uuid   (GtdWindow             *self,
                                                           gchar                 *uuid);

void                      gtd_log_task_info_from_uuid     (GtdManager *manager,
                                                           GtdTaskList *list,
                                                           gpointer uuid);
G_END_DECLS

#endif /* GTD_WINDOW_H */
