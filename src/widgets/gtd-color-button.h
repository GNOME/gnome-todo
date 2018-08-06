/* gtd-color-button.h
 *
 * Copyright 2018 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GTD_TYPE_COLOR_BUTTON (gtd_color_button_get_type())

G_DECLARE_FINAL_TYPE (GtdColorButton, gtd_color_button, GTD, COLOR_BUTTON, GtkWidget)

GtkWidget*           gtd_color_button_new                        (const GdkRGBA      *color);

const GdkRGBA*       gtd_color_button_get_color                  (GtdColorButton     *self);

void                 gtd_color_button_set_color                  (GtdColorButton     *self,
                                                                  const GdkRGBA      *color);

G_END_DECLS
