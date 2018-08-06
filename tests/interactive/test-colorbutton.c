/* test-colorbutton.c
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

#include "gtd-color-button.h"

static const gchar * const colors[] =
{
  "#ffffff",
  "#dddddd",
  "#ababab",
  "#fafa00",
  "#888888",
  "#333333",
  "#000000",
  "#96ff11",
  "#03fa95",
};

gint
main (gint   argc,
      gchar *argv[])
{
  GtkWindow *window = NULL;
  GtkWidget *group = NULL;
  GtkWidget *grid = NULL;
  guint columns;
  guint i;

  g_set_prgname ("test-colorbutton");
  g_set_application_name ("GNOME To Do | Color Button Test");

  gtk_init ();

  grid = g_object_new (GTK_TYPE_GRID,
                       "row-homogeneous", TRUE,
                       "column-homogeneous", TRUE,
                       NULL);

  columns = ceil (sqrt (G_N_ELEMENTS (colors)));

  for (i = 0; i < G_N_ELEMENTS (colors); i++)
    {
      GtkWidget *color_button = NULL;
      GdkRGBA color;

      gdk_rgba_parse (&color, colors[i]);

      color_button = g_object_new (GTD_TYPE_COLOR_BUTTON,
                                   "color", &color,
                                   "group", group,
                                   NULL);

      gtk_grid_attach (GTK_GRID (grid),
                       color_button,
                       i % columns,
                       i / columns,
                       1,
                       1);

      if (!group)
        group = color_button;
    }

  window = GTK_WINDOW (gtk_window_new (GTK_WINDOW_TOPLEVEL));
  gtk_container_add (GTK_CONTAINER (window), grid);
  gtk_window_present (window);

  gtk_main ();

  return 0;
}

