/* gtd-rows-common.c
 *
 * Copyright (C) 2017 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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

#include "gtd-rows-common-private.h"

#define MAX_WIDTH                  700

void
gtd_row_get_preferred_width_with_max (GtkWidget *widget,
                                      gint      *minimum_width,
                                      gint      *natural_width)
{
  gint local_minimum_width, local_natural_width;
  gint scale_factor;

  scale_factor = gtk_widget_get_scale_factor (widget);

  gtk_widget_get_preferred_width (gtk_bin_get_child (GTK_BIN (widget)),
                                  &local_minimum_width,
                                  &local_natural_width);

  if (minimum_width)
    *minimum_width = MIN (local_minimum_width, MAX_WIDTH) * scale_factor;

  if (natural_width)
    *natural_width = MAX (local_minimum_width, MAX_WIDTH) * scale_factor;
}

