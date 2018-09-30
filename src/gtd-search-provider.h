/* gtd-search-provider.h
 *
 * Copyright 2018 Vyas Giridharan <vyasgiridhar27@gmail.com>
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

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define GTD_TYPE_SEARCH_PROVIDER (gtd_search_provider_get_type())

G_DECLARE_FINAL_TYPE (GtdSearchProvider, gtd_search_provider, GTD, SEARCH_PROVIDER, GObject)

GtdSearchProvider*   gtd_search_provider_new                     (void);

gboolean             gtd_search_provider_register                (GtdSearchProvider *self,
                                                                  GDBusConnection   *connection,
                                                                  GError            *error);

void                 gtd_search_provider_unregister              (GtdSearchProvider *self);

gboolean             gtd_search_provider_dbus_export             (GtdSearchProvider  *self,
                                                                  GDBusConnection    *connection,
                                                                  const gchar        *object_path,
                                                                  GError            **error);

void                 gtd_search_provider_dbus_unexport           (GtdSearchProvider *self,
                                                                  GDBusConnection   *connection,
                                                                  const gchar       *object_path);

G_END_DECLS
