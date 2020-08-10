/* gtd-star-widget.h
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

#include "gnome-todo.h"

G_BEGIN_DECLS

#define GTD_TYPE_STAR_WIDGET (gtd_star_widget_get_type())
G_DECLARE_FINAL_TYPE (GtdStarWidget, gtd_star_widget, GTD, STAR_WIDGET, GtdWidget)

GtkWidget*           gtd_star_widget_new                         (void);

gboolean             gtd_star_widget_get_active                  (GtdStarWidget      *self);

void                 gtd_star_widget_set_active                  (GtdStarWidget      *self,
                                                                  gboolean            active);

G_END_DECLS