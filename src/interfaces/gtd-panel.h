/* gtd-panel.h
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

#ifndef GTD_PANEL_H
#define GTD_PANEL_H

#include <glib-object.h>
#include <glib.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GTD_TYPE_PANEL (gtd_panel_get_type ())

G_DECLARE_INTERFACE (GtdPanel, gtd_panel, GTD, PANEL, GtkWidget)

struct _GtdPanelInterface
{
  GTypeInterface parent;

  const gchar*       (*get_panel_name)                     (GtdPanel        *panel);

  const gchar*       (*get_panel_title)                    (GtdPanel        *panel);

  GList*             (*get_header_widgets)                 (GtdPanel        *panel);

  const GMenu*       (*get_menu)                           (GtdPanel        *panel);

  GIcon*            (*get_icon)                            (GtdPanel        *self);

  guint32           (*get_priority)                        (GtdPanel        *self);

  gchar*            (*get_subtitle)                        (GtdPanel        *self);
};

const gchar*         gtd_panel_get_panel_name                    (GtdPanel           *panel);

const gchar*         gtd_panel_get_panel_title                   (GtdPanel           *panel);

GList*               gtd_panel_get_header_widgets                (GtdPanel           *panel);

const GMenu*         gtd_panel_get_menu                          (GtdPanel           *panel);

GIcon*               gtd_panel_get_icon                          (GtdPanel           *self);

guint32              gtd_panel_get_priority                      (GtdPanel           *self);

gchar*               gtd_panel_get_subtitle                      (GtdPanel           *self);

G_END_DECLS

#endif /* GTD_PANEL_H */
