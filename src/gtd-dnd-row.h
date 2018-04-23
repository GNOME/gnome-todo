/* gtd-dnd-row.h
 *
 * Copyright (C) 2016 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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

#ifndef GTD_DND_ROW_H
#define GTD_DND_ROW_H

#include "gtd-types.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GTD_TYPE_DND_ROW (gtd_dnd_row_get_type())

G_DECLARE_FINAL_TYPE (GtdDndRow, gtd_dnd_row, GTD, DND_ROW, GtkListBoxRow)

GtkWidget*           gtd_dnd_row_new                             (void);

GtkListBoxRow*       gtd_dnd_row_get_row_above                   (GtdDndRow          *self);

void                 gtd_dnd_row_set_row_above                   (GtdDndRow          *self,
                                                                  GtkListBoxRow      *row);


gboolean             gtd_dnd_row_drag_drop                       (GtkWidget          *widget,
                                                                  GdkDrop            *drop,
                                                                  gint                x,
                                                                  gint                y);

gboolean             gtd_dnd_row_drag_motion                     (GtkWidget          *widget,
                                                                  gint                x,
                                                                  gint                y);

G_END_DECLS

#endif /* GTD_DND_ROW_H */

