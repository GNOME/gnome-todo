/* gtd-application.h
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

#ifndef GTD_APPLICATION_H
#define GTD_APPLICATION_H

#include "gtd-types.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GTD_TYPE_APPLICATION (gtd_application_get_type())

G_DECLARE_FINAL_TYPE (GtdApplication, gtd_application, GTD, APPLICATION, GtkApplication)

GtdApplication*                 gtd_application_new                     (void);

void                            gtd_application_set_uuid                (GtdApplication *self,
                                                                         const gchar    *app_uuid);

const gchar*                    gtd_application_get_uuid                (GtdApplication *self);

void                            gtd_application_free_uuid               (GApplication *application);

G_END_DECLS

#endif /* GTD_APPLICATION_H */
