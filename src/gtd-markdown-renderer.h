/* gtd-markdown-buffer.h
 *
 * Copyright (C) 2018 Vyas Giridharan <vyasgiridhar27@gmail.com>
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

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GTD_TYPE_MARKDOWN_RENDERER (gtd_markdown_renderer_get_type())

G_DECLARE_FINAL_TYPE (GtdMarkdownRenderer, gtd_markdown_renderer, GTD, MARKDOWN_RENDERER, GObject)

GtdMarkdownRenderer* gtd_markdown_renderer_new                   (void);

void                 gtd_markdown_renderer_add_buffer            (GtdMarkdownRenderer*self,
                                                                  GtkTextBuffer      *buffer);

G_END_DECLS

