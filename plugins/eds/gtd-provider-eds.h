/* gtd-provider-eds.h
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

#ifndef GTD_PROVIDER_EDS_H
#define GTD_PROVIDER_EDS_H

#include "gnome-todo.h"

#include <glib.h>
#include <libecal/libecal.h>

G_BEGIN_DECLS

#define GTD_TYPE_PROVIDER_EDS (gtd_provider_eds_get_type())

G_DECLARE_DERIVABLE_TYPE (GtdProviderEds, gtd_provider_eds, GTD, PROVIDER_EDS, GtdObject)

struct _GtdProviderEdsClass
{
  GtdObjectClass parent;

  const gchar*       (*get_id)                                   (GtdProviderEds     *self);

  const gchar*       (*get_name)                                 (GtdProviderEds     *self);

  const gchar*       (*get_provider_type)                        (GtdProviderEds     *self);

  const gchar*       (*get_description)                          (GtdProviderEds     *self);

  gboolean           (*get_enabled)                              (GtdProviderEds     *self);

  GIcon*             (*get_icon)                                 (GtdProviderEds     *self);

  ESource*           (*create_source)                            (GtdProviderEds     *self);

  gboolean           (*should_load_source)                       (GtdProviderEds     *provider,
                                                                  ESource            *source);
};

GtdProviderEds*      gtd_provider_eds_new                        (ESourceRegistry    *registry);

ESourceRegistry*     gtd_provider_eds_get_registry               (GtdProviderEds     *local);

G_END_DECLS

#endif /* GTD_PROVIDER_EDS_H */
