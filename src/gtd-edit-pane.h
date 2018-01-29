/* gtd-edit-pane.h
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

#ifndef GTD_EDIT_PANE_H
#define GTD_EDIT_PANE_H

#include "gtd-types.h"

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GTD_TYPE_EDIT_PANE (gtd_edit_pane_get_type())

G_DECLARE_FINAL_TYPE (GtdEditPane, gtd_edit_pane, GTD, EDIT_PANE, GtkGrid)

GtkWidget*           gtd_edit_pane_new                           (void);

GtdTask*             gtd_edit_pane_get_task                      (GtdEditPane        *self);

void                 gtd_edit_pane_set_task                      (GtdEditPane        *self,
                                                                  GtdTask            *task);

void                 gtd_edit_pane_set_markdown_renderer         (GtdEditPane         *self,
                                                                  GtdMarkdownRenderer *renderer);

G_END_DECLS

#endif /* GTD_EDIT_PANE_H */
