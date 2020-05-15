/* gtd-notification-widget.h
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

#ifndef GTD_NOTIFICATION_WIDGET_H
#define GTD_NOTIFICATION_WIDGET_H

#include "gnome-todo.h"

G_BEGIN_DECLS

#define GTD_TYPE_NOTIFICATION_WIDGET (gtd_notification_widget_get_type())
G_DECLARE_FINAL_TYPE (GtdNotificationWidget, gtd_notification_widget, GTD, NOTIFICATION_WIDGET, GtdWidget)

GtkWidget*           gtd_notification_widget_new                 (void);

void                 gtd_notification_widget_notify              (GtdNotificationWidget *self,
                                                                  GtdNotification       *notification);

void                 gtd_notification_widget_cancel              (GtdNotificationWidget *self,
                                                                  GtdNotification       *notification);

G_END_DECLS

#endif /* GTD_NOTIFICATION_WIDGET_H */
