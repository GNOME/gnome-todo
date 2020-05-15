/* gtd-activatable.h
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

#ifndef GTD_ACTIVATABLE_H
#define GTD_ACTIVATABLE_H

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>
#include <libpeas/peas.h>

G_BEGIN_DECLS

#define GTD_TYPE_ACTIVATABLE (gtd_activatable_get_type ())

G_DECLARE_INTERFACE (GtdActivatable, gtd_activatable, GTD, ACTIVATABLE, GObject)

struct _GtdActivatableInterface
{
  GTypeInterface    parent;

  void             (*activate)                             (GtdActivatable     *activatable);

  void             (*deactivate)                           (GtdActivatable     *activatable);

  GtkWidget*       (*get_preferences_panel)                (GtdActivatable     *activatable);
};

void                 gtd_activatable_activate                    (GtdActivatable     *activatable);

void                 gtd_activatable_deactivate                  (GtdActivatable     *activatable);

GtkWidget*           gtd_activatable_get_preferences_panel       (GtdActivatable     *activatable);

G_END_DECLS

#endif /* GTD_ACTIVATABLE_H */
