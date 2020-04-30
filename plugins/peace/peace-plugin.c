/* zen-plugin.c
 *
 * Copyright 2020 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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

#include <libpeas/peas.h>

#include "gtd-peace-omni-area-addin.h"

G_MODULE_EXPORT void
peace_plugin_register_types (PeasObjectModule *module)
{
  peas_object_module_register_extension_type (module,
                                              GTD_TYPE_OMNI_AREA_ADDIN,
                                              GTD_TYPE_PEACE_OMNI_AREA_ADDIN);
}
