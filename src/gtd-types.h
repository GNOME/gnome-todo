/* gtd-types.h
 *
 * Copyright (C) 2015-2020 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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

#ifndef GTD_TYPES_H
#define GTD_TYPES_H

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _GtdActivatable          GtdActivatable;
typedef struct _GtdAnimatable           GtdAnimatable;
typedef struct _GtdApplication          GtdApplication;
typedef struct _GtdClock                GtdClock;
typedef struct _GtdDoneButton           GtdDoneButton;
typedef struct _GtdInterval             GtdInterval;
typedef struct _GtdInitialSetupWindow   GtdInitialSetupWindow;
typedef struct _GtdListView             GtdListView;
typedef struct _GtdManager              GtdManager;
typedef struct _GtdMarkdownRenderer     GtdMarkdownRenderer;
typedef struct _GtdNotification         GtdNotification;
typedef struct _GtdNotificationWidget   GtdNotificationWidget;
typedef struct _GtdObject               GtdObject;
typedef struct _GtdOmniArea             GtdOmniArea;
typedef struct _GtdPanel                GtdPanel;
typedef struct _GtdPluginManager        GtdPluginManager;
typedef struct _GtdProvider             GtdProvider;
typedef struct _GtdStorage              GtdStorage;
typedef struct _GtdStoragePopover       GtdStoragePopover;
typedef struct _GtdStorageRow           GtdStorageRow;
typedef struct _GtdStorageSelector      GtdStorageSelector;
typedef struct _GtdTask                 GtdTask;
typedef struct _GtdTaskList             GtdTaskList;
typedef struct _GtdTaskListItem         GtdTaskListItem;
typedef struct _GtdTaskRow              GtdTaskRow;
typedef struct _GtdTransition           GtdTransition;
typedef struct _GtdWidget               GtdWidget;
typedef struct _GtdWindow               GtdWindow;
typedef struct _GtdWorkspace            GtdWorkspace;

G_END_DECLS

#endif /* GTD_TYPES_H */
