/* gtd-sidebar-panel-row.h
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

#include "gtd-types.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GTD_TYPE_SIDEBAR_PANEL_ROW (gtd_sidebar_panel_row_get_type())

G_DECLARE_FINAL_TYPE (GtdSidebarPanelRow, gtd_sidebar_panel_row, GTD, SIDEBAR_PANEL_ROW, GtkListBoxRow)

GtkWidget*           gtd_sidebar_panel_row_new                   (GtdPanel           *panel);

GtdPanel*            gtd_sidebar_panel_row_get_panel             (GtdSidebarPanelRow *self);

G_END_DECLS
