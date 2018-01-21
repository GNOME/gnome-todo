/* gtd-markup-buffer.h
 *
 * Copyright (C) 2017 Vyas Giridharan <vyasgiridhar27@gmail.com>
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

#ifndef GTD_MARKUP_RENDERER_H 
#define GTD_MARKUP_RENDERER_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GTD_TYPE_MARKUP_RENDERER (gtd_markup_renderer_get_type())

G_DECLARE_FINAL_TYPE (GtdMarkupRenderer, gtd_markup_renderer, GTD, MARKUP_RENDERER, GObject)

GtdMarkupRenderer*   gtd_markup_renderer_new                     (void);

void                 gtd_markup_renderer_set_buffer              (GtdMarkupRenderer  *self,
                                                                  GtkTextBuffer      *buffer);

void                 gtd_markup_renderer_render_markup           (GtdMarkupRenderer  *self);

void                 gtd_markup_renderer_populate_tag_table      (GtdMarkupRenderer  *self);

void                 gtd_markup_renderer_clear_markup            (GtdMarkupRenderer  *self);

G_END_DECLS

#endif /* GTD_MARKUP_RENDERER_H */

