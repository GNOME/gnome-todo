/* test-task-model.c
 *
 * Copyright 2018-2020 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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

#include "gnome-todo.h"

#include "core/gtd-log.h"
#include "gtd-task-list.h"
#include "gtd-manager-protected.h"
#include "dummy-provider.h"

static void
test_move (void)
{
  g_autoptr (DummyProvider) dummy_provider = NULL;
  g_autoptr (GtdTask) first_task = NULL;
  g_autoptr (GtdTask) last_task = NULL;
  g_autoptr (GList) lists = NULL;
  GtdTaskList *list = NULL;
  GListModel *model;
  guint n_tasks;

  dummy_provider = dummy_provider_new ();
  n_tasks = dummy_provider_generate_task_list (dummy_provider);
  g_assert_cmpuint (n_tasks, ==, 10);

  gtd_manager_add_provider (gtd_manager_get_default (), GTD_PROVIDER (dummy_provider));

  lists = gtd_provider_get_task_lists (GTD_PROVIDER (dummy_provider));
  g_assert_nonnull (lists);
  g_assert_cmpint (g_list_length (lists), ==, 1);

  list = lists->data;
  model = G_LIST_MODEL (list);
  g_assert_nonnull (list);
  g_assert_cmpstr (gtd_task_list_get_name (list), ==, "List");

  first_task = g_list_model_get_item (model, 0);
  g_assert_nonnull (first_task);
  g_assert_cmpint (gtd_task_get_position (first_task), ==, 0);
  g_assert_true (g_list_model_get_item (model, 0) == first_task);

  last_task = g_list_model_get_item (model, 9);
  g_assert_nonnull (last_task);
  g_assert_cmpint (gtd_task_get_position (last_task), ==, 9);
  g_assert_true (g_list_model_get_item (model, 9) == last_task);

  /* Move the task to 0 */
  gtd_task_list_move_task_to_position (list, last_task, 0);

  g_assert_cmpint (gtd_task_get_position (last_task), ==, 0);
  g_assert_cmpint (gtd_task_get_position (first_task), ==, 1);
  g_assert_true (g_list_model_get_item (model, 0) == last_task);
  g_assert_true (g_list_model_get_item (model, 1) == first_task);

  /* Move the task to 1 */
  gtd_task_list_move_task_to_position (list, last_task, 1);

  g_assert_cmpint (gtd_task_get_position (last_task), ==, 1);
  g_assert_cmpint (gtd_task_get_position (first_task), ==, 0);
  g_assert_true (g_list_model_get_item (model, 0) == first_task);
  g_assert_true (g_list_model_get_item (model, 1) == last_task);

  /* Move the task to 9 */
  gtd_task_list_move_task_to_position (list, last_task, 9);

  g_assert_cmpint (gtd_task_get_position (last_task), ==, 9);
  g_assert_true (g_list_model_get_item (model, 9) == last_task);
}

gint
main (gint argc,
      gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);

  if (g_getenv ("G_MESSAGES_DEBUG"))
    gtd_log_init ();

  g_test_add_func ("/task-list/move", test_move);

  return g_test_run ();
}
