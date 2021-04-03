/* gtd-window.h
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


#ifndef GTD_WINDOW_H
#define GTD_WINDOW_H

#include "gtd-types.h"

#include <adwaita.h>

G_BEGIN_DECLS

#define GTD_TYPE_WINDOW (gtd_window_get_type())

G_DECLARE_FINAL_TYPE (GtdWindow, gtd_window, GTD, WINDOW, AdwApplicationWindow)

GtkWidget*                gtd_window_new                  (GtdApplication       *application);

GtdManager*               gtd_window_get_manager          (GtdWindow            *window);

void                      gtd_window_notify               (GtdWindow            *window,
                                                           GtdNotification      *notification);

void                      gtd_window_cancel_notification  (GtdWindow             *window,
                                                           GtdNotification      *notification);

void                      gtd_window_embed_widget_in_header (GtdWindow           *self,
                                                             GtkWidget           *widget,
                                                             GtkPositionType      position);

GtdWorkspace*             gtd_window_get_current_workspace (GtdWindow           *self);

G_END_DECLS

#endif /* GTD_WINDOW_H */
