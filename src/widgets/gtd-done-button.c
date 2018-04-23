/* gtd-done-button.c
 *
 * Copyright (C) 2018 Andrei Lisita <andreii.lisita@gmail.com>
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

#include "gtd-done-button.h"
#include "gtd-rows-common-private.h"

#include <gtk/gtk.h>

struct _GtdDoneButton
{
  GtkButton           parent;
};

G_DEFINE_TYPE (GtdDoneButton, gtd_done_button, GTK_TYPE_BUTTON)

static void
gtd_done_button_class_init (GtdDoneButtonClass *klass)
{
}

static void
gtd_done_button_init (GtdDoneButton *self)
{
}
