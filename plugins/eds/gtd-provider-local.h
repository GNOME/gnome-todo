/* gtd-provider-local.h
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

#ifndef GTD_PROVIDER_LOCAL_H
#define GTD_PROVIDER_LOCAL_H

#include "gnome-todo.h"
#include "gtd-provider-eds.h"

#include <glib.h>

#include <libecal/libecal.h>

G_BEGIN_DECLS

#define GTD_TYPE_PROVIDER_LOCAL (gtd_provider_local_get_type())

G_DECLARE_FINAL_TYPE (GtdProviderLocal, gtd_provider_local, GTD, PROVIDER_LOCAL, GtdProviderEds)

GtdProviderLocal*    gtd_provider_local_new                      (ESourceRegistry    *source_registry);

G_END_DECLS

#endif /* GTD_PROVIDER_LOCAL_H */
