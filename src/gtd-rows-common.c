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

#define MAX_WIDTH                  800

void
gtd_row_measure_with_max (GtkWidget      *widget,
                          GtkOrientation  orientation,
                          gint            for_size,
                          gint           *minimum,
                          gint           *natural,
                          gint           *minimum_baseline,
                          gint           *natural_baseline)
{
  if (orientation == GTK_ORIENTATION_VERTICAL)
    {
      gtk_widget_measure (gtk_bin_get_child (GTK_BIN (widget)),
                          orientation,
                          for_size,
                          minimum,
                          natural,
                          minimum_baseline,
                          natural_baseline);
    }
  else
    {
      gint local_minimum_width;
      gint local_natural_width;
      gint scale_factor;
      gint margins;

      gtk_widget_measure (gtk_bin_get_child (GTK_BIN (widget)),
                          orientation,
                          for_size,
                          &local_minimum_width,
                          &local_natural_width,
                          minimum_baseline,
                          natural_baseline);

      scale_factor = gtk_widget_get_scale_factor (widget);
      margins = gtk_widget_get_margin_start (widget) + gtk_widget_get_margin_end (widget);

      if (minimum)
        *minimum = (MIN (local_minimum_width, MAX_WIDTH) - margins) * scale_factor;

      if (natural)
        *natural = (MAX (local_minimum_width, MAX_WIDTH) - margins) * scale_factor;
    }
}

