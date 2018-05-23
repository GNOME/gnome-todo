/* gtd-utils.c
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

#include "gtd-utils.h"

#include <string.h>

gchar*
gtd_str_replace (const gchar *source,
                 const gchar *search,
                 const gchar *replacement)
{
  gchar *new_string, *new_aux;
  const gchar *source_aux;
  const gchar *source_aux2;
  gint64 n_ocurrences;
  gint64 replacement_len;
  gint64 search_len;
  gint64 source_len;
  gint64 final_size;
  gint64 diff;

  g_assert_nonnull (source);
  g_assert_nonnull (search);
  g_assert_nonnull (replacement);

  /* Count the number of ocurrences of "search" inside "source" */
  source_len = strlen (source);
  search_len = strlen (search);
  replacement_len = strlen (replacement);
  n_ocurrences = 0;

  for (source_aux = g_strstr_len (source, source_len, search);
       source_aux != NULL;
       source_aux = g_strstr_len (source_aux + search_len, -1, search))
    {
      n_ocurrences++;
    }

  /* Calculate size of the new string */
  diff = replacement_len - search_len;
  final_size = source_len + diff * n_ocurrences + 1;

  /* Create the new string */
  new_string = g_malloc (final_size);
  new_string[final_size - 1] = '\0';

  /*
   * And copy the contents of the source string into the new string,
   * substituting the search by replacement
   */
  source_aux2 = source;
  new_aux = new_string;

  for (source_aux = g_strstr_len (source, source_len, search);
       source_aux != NULL;
       source_aux = g_strstr_len (source_aux + search_len, -1, search))
    {
      diff = source_aux - source_aux2;

      /* Copy the non-search part between the instances of "search" in "source" */
      strncpy (new_aux, source_aux2, diff);

      new_aux += diff;

      /* Now copy the "replacement" where "search would be in source */
      strncpy (new_aux, replacement, replacement_len);

      source_aux2 = source_aux + search_len;
      new_aux += replacement_len;
    }

  /* Copy the last chunk of string if any */
  diff = source_len - (source_aux2 - source);
  strncpy (new_aux, source_aux2, diff);

  return new_string;
}

gint
gtd_collate_compare_strings (const gchar *string_a,
                             const gchar *string_b)
{
  g_autofree gchar *collated_a = NULL;
  g_autofree gchar *collated_b = NULL;

  collated_a = g_utf8_collate_key (string_a, -1);
  collated_b = g_utf8_collate_key (string_b, -1);

  return g_strcmp0 (collated_a, collated_b);
}
