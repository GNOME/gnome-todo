/* gtd-panel-scheduled.c
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

#define G_LOG_DOMAIN "GtdPanelScheduled"

#include "gnome-todo.h"

#include "gtd-panel-scheduled.h"

#include <glib/gi18n.h>
#include <math.h>

struct _GtdPanelScheduled
{
  GtkBox              parent;

  GMenu              *menu;

  gchar              *title;
  guint               number_of_tasks;
  GList              *task_list;
  GtkWidget          *view;
};

static void          gtd_panel_iface_init                        (GtdPanelInterface  *iface);

G_DEFINE_TYPE_EXTENDED (GtdPanelScheduled, gtd_panel_scheduled, GTK_TYPE_BOX,
                        0,
                        G_IMPLEMENT_INTERFACE (GTD_TYPE_PANEL,
                                               gtd_panel_iface_init))

#define GTD_PANEL_SCHEDULED_NAME "panel-scheduled"

enum {
  PROP_0,
  PROP_MENU,
  PROP_NAME,
  PROP_TITLE,
  N_PROPS
};

static void
get_date_offset (GDateTime *dt,
                 gint      *days_diff,
                 gint      *years_diff)
{
  g_autoptr (GDateTime) now, utc;

  now = g_date_time_new_now_local ();
  utc = g_date_time_new_utc (g_date_time_get_year (now),
                             g_date_time_get_month (now),
                             g_date_time_get_day_of_month (now),
                             g_date_time_get_hour (now),
                             g_date_time_get_minute (now),
                             g_date_time_get_seconds (now));

  if (days_diff)
    *days_diff = ceil ((gdouble) g_date_time_difference (dt, utc) / (gdouble) G_TIME_SPAN_DAY);

  if (years_diff)
    *years_diff = g_date_time_get_year (dt) - g_date_time_get_year (utc);
}

static gchar*
get_string_for_date (GDateTime *dt,
                     gint      *span)
{
  gchar *str;
  gint days_diff;
  gint years_diff;

  /* This case should never happen */
  if (!dt)
    return g_strdup (_("No date set"));

  days_diff = years_diff = 0;

  get_date_offset (dt, &days_diff, &years_diff);

  if (days_diff < -1)
    {
      /* Translators: This message will never be used with '1 day ago'
       * but the singular form is required because some languages do not
       * have plurals, some languages reuse the singular form for numbers
       * like 21, 31, 41, etc.
       */
      str = g_strdup_printf (g_dngettext (NULL, "%d day ago", "%d days ago", -days_diff), -days_diff);
    }
  else if (days_diff == -1)
    {
      str = g_strdup (_("Yesterday"));
    }
  else if (days_diff == 0)
    {
      str = g_strdup (_("Today"));
    }
  else if (days_diff == 1)
    {
      str = g_strdup (_("Tomorrow"));
    }
  else if (days_diff > 1 && days_diff < 7)
    {
      str = g_date_time_format (dt, "%A"); // Weekday name
    }
  else if (days_diff >= 7 && years_diff == 0)
    {
      str = g_date_time_format (dt, "%B"); // Full month name
    }
  else
    {
      str = g_strdup_printf ("%d", g_date_time_get_year (dt));
    }

  if (span)
    *span = days_diff;

  return str;
}

static GtkWidget*
create_label (const gchar *text,
              gint         span,
              gboolean     first_header)
{
  GtkStyleContext *context;
  GtkWidget *label;
  GtkWidget *box;

  label = g_object_new (GTK_TYPE_LABEL,
                        "label", text,
                        "margin-left", 12,
                        "margin-bottom", 6,
                        "margin-top", first_header ? 6 : 18,
                        "xalign", 0.0,
                        "hexpand", TRUE,
                        NULL);

  context = gtk_widget_get_style_context (label);
  gtk_style_context_add_class (context, span < 0 ? "date-overdue" : "date-scheduled");

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  gtk_container_add (GTK_CONTAINER (box), label);

  gtk_widget_show_all (box);

  return box;
}

static gint
compare_by_date (GDateTime *d1,
                 GDateTime *d2)
{
  if (g_date_time_get_year (d1) != g_date_time_get_year (d2))
    return g_date_time_get_year (d1) - g_date_time_get_year (d2);

  return g_date_time_get_day_of_year (d1) - g_date_time_get_day_of_year (d2);
}

static void
gtd_panel_scheduled_header_func (GtkListBoxRow     *row,
                                 GtdTask           *row_task,
                                 GtkListBoxRow     *before,
                                 GtdTask           *before_task,
                                 GtdPanelScheduled *panel)
{
  g_autoptr (GDateTime) dt = NULL;
  gchar *text;
  gint span;

  dt = gtd_task_get_due_date (row_task);

  if (!before)
    {
      text = get_string_for_date (dt, &span);

      gtk_list_box_row_set_header (row, create_label (text,
                                                      span,
                                                      TRUE));

      g_free (text);
    }
  else
    {
      g_autoptr (GDateTime) before_dt = NULL;
      gint diff;

      before_dt = gtd_task_get_due_date (before_task);
      diff = compare_by_date (before_dt, dt);

      if (diff != 0)
        {
          text = get_string_for_date (dt, &span);

          gtk_list_box_row_set_header (row, create_label (text, span, FALSE));

          g_free (text);
        }
      else
        {
          gtk_list_box_row_set_header (row, NULL);
        }
    }
}

static gint
gtd_panel_scheduled_sort_func (GtkListBoxRow     *row1,
                               GtdTask           *row1_task,
                               GtkListBoxRow     *row2,
                               GtdTask           *row2_task,
                               GtdPanelScheduled *panel)
{
  GDateTime *dt1;
  GDateTime *dt2;
  gint retval;
  gchar *t1;
  gchar *t2;

  if (!row1_task && !row2_task)
    return  0;
  if (!row1_task)
    return  1;
  if (!row2_task)
    return -1;

  /* First, compare by ::due-date. */
  dt1 = gtd_task_get_due_date (row1_task);
  dt2 = gtd_task_get_due_date (row2_task);

  if (!dt1 && !dt2)
    retval = 0;
  else if (!dt1)
    retval = 1;
  else if (!dt2)
    retval = -1;
  else
    retval = compare_by_date (dt1, dt2);

  g_clear_pointer (&dt1, g_date_time_unref);
  g_clear_pointer (&dt2, g_date_time_unref);

  if (retval != 0)
    return retval;

  /* Second, compare by ::complete. */
  retval = gtd_task_get_complete (row1_task) - gtd_task_get_complete (row2_task);

  if (retval != 0)
    return retval;

  /* Third, compare by ::priority. Inversely to the  */
  retval = gtd_task_get_priority (row2_task) - gtd_task_get_priority (row1_task);

  if (retval != 0)
    return retval;

  /* Fourth, compare by ::creation-date. */
  dt1 = gtd_task_get_creation_date (row1_task);
  dt2 = gtd_task_get_creation_date (row2_task);

  if (!dt1 && !dt2)
    retval =  0;
  else if (!dt1)
    retval =  1;
  else if (!dt2)
    retval = -1;
  else
    retval = g_date_time_compare (dt1, dt2);

  g_clear_pointer (&dt1, g_date_time_unref);
  g_clear_pointer (&dt2, g_date_time_unref);

  if (retval != 0)
    return retval;

  /* Finally, compare by ::title. */
  t1 = t2 = NULL;

  t1 = g_utf8_casefold (gtd_task_get_title (row1_task), -1);
  t2 = g_utf8_casefold (gtd_task_get_title (row2_task), -1);

  retval = g_strcmp0 (t1, t2);

  g_free (t1);
  g_free (t2);

  return retval;
}

static void
gtd_panel_scheduled_count_tasks (GtdPanelScheduled *panel)
{
  g_autoptr (GDateTime) now;
  GtdManager *manager;
  GList *tasklists;
  GList *l;
  guint number_of_tasks;

  now = g_date_time_new_now_local ();
  manager = gtd_manager_get_default ();
  tasklists = gtd_manager_get_task_lists (manager);
  number_of_tasks = 0;

  g_clear_pointer (&panel->task_list, g_list_free);

  /* Recount tasks */
  for (l = tasklists; l != NULL; l = l->next)
    {
      GList *tasks;
      GList *t;

      tasks = gtd_task_list_get_tasks (l->data);

      for (t = tasks; t != NULL; t = t->next)
        {
          GDateTime *task_dt;

          task_dt = gtd_task_get_due_date (t->data);

          /*
           * GtdTaskListView automagically updates the list
           * whever a task is added/removed/changed.
           */
          if (task_dt)
            {
              panel->task_list = g_list_prepend (panel->task_list, t->data);

              if (!gtd_task_get_complete (t->data))
                number_of_tasks++;
            }

          g_clear_pointer (&task_dt, g_date_time_unref);
        }

      g_list_free (tasks);
    }

  /* Add the tasks to the view */
  gtd_task_list_view_set_list (GTD_TASK_LIST_VIEW (panel->view), panel->task_list);
  gtd_task_list_view_set_default_date (GTD_TASK_LIST_VIEW (panel->view), now);

  if (number_of_tasks != panel->number_of_tasks)
    {
      panel->number_of_tasks = number_of_tasks;

      /* Update title */
      g_clear_pointer (&panel->title, g_free);
      if (number_of_tasks == 0)
        {
          panel->title = g_strdup (_("Scheduled"));
        }
      else
        {
          panel->title = g_strdup_printf ("%s (%d)",
                                          _("Scheduled"),
                                          panel->number_of_tasks);
        }

      g_object_notify (G_OBJECT (panel), "title");
    }

  g_list_free (tasklists);
}

/**********************
 * GtdPanel iface init
 **********************/
static const gchar*
gtd_panel_scheduled_get_panel_name (GtdPanel *panel)
{
  return GTD_PANEL_SCHEDULED_NAME;
}

static const gchar*
gtd_panel_scheduled_get_panel_title (GtdPanel *panel)
{
  return GTD_PANEL_SCHEDULED (panel)->title;
}

static GList*
gtd_panel_scheduled_get_header_widgets (GtdPanel *panel)
{
  return NULL;
}

static const GMenu*
gtd_panel_scheduled_get_menu (GtdPanel *panel)
{
  return GTD_PANEL_SCHEDULED (panel)->menu;
}

static void
gtd_panel_iface_init (GtdPanelInterface *iface)
{
  iface->get_panel_name = gtd_panel_scheduled_get_panel_name;
  iface->get_panel_title = gtd_panel_scheduled_get_panel_title;
  iface->get_header_widgets = gtd_panel_scheduled_get_header_widgets;
  iface->get_menu = gtd_panel_scheduled_get_menu;
}

static void
gtd_panel_scheduled_finalize (GObject *object)
{
  GtdPanelScheduled *self = (GtdPanelScheduled *)object;

  g_clear_object (&self->menu);
  g_clear_pointer (&self->title, g_free);
  g_clear_pointer (&self->task_list, g_list_free);

  G_OBJECT_CLASS (gtd_panel_scheduled_parent_class)->finalize (object);
}

static void
gtd_panel_scheduled_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  GtdPanelScheduled *self = GTD_PANEL_SCHEDULED (object);

  switch (prop_id)
    {
    case PROP_MENU:
      g_value_set_object (value, NULL);
      break;

    case PROP_NAME:
      g_value_set_string (value, GTD_PANEL_SCHEDULED_NAME);
      break;

    case PROP_TITLE:
      g_value_set_string (value, self->title);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_panel_scheduled_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
gtd_panel_scheduled_class_init (GtdPanelScheduledClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gtd_panel_scheduled_finalize;
  object_class->get_property = gtd_panel_scheduled_get_property;
  object_class->set_property = gtd_panel_scheduled_set_property;

  g_object_class_override_property (object_class, PROP_MENU, "menu");
  g_object_class_override_property (object_class, PROP_NAME, "name");
  g_object_class_override_property (object_class, PROP_TITLE, "title");
}

static void
gtd_panel_scheduled_init (GtdPanelScheduled *self)
{
  GtdManager *manager;

  /* Connect to GtdManager::list-* signals to update the title */
  manager = gtd_manager_get_default ();

  g_signal_connect_swapped (manager,
                            "list-added",
                            G_CALLBACK (gtd_panel_scheduled_count_tasks),
                            self);

  g_signal_connect_swapped (manager,
                            "list-removed",
                            G_CALLBACK (gtd_panel_scheduled_count_tasks),
                            self);

  g_signal_connect_swapped (manager,
                            "list-changed",
                            G_CALLBACK (gtd_panel_scheduled_count_tasks),
                            self);

  g_signal_connect_swapped (gtd_manager_get_timer (manager),
                            "update",
                            G_CALLBACK (gtd_panel_scheduled_count_tasks),
                            self);

  /* Setup a title */
  self->title = g_strdup (_("Scheduled"));

  /* Menu */
  self->menu = g_menu_new ();
  g_menu_append (self->menu,
                 _("Clear completed tasks…"),
                 "list.clear-completed-tasks");

  /* The main view */
  self->view = gtd_task_list_view_new ();
  gtd_task_list_view_set_handle_subtasks (GTD_TASK_LIST_VIEW (self->view), FALSE);
  gtd_task_list_view_set_show_list_name (GTD_TASK_LIST_VIEW (self->view), TRUE);
  gtd_task_list_view_set_show_due_date (GTD_TASK_LIST_VIEW (self->view), FALSE);

  gtk_widget_set_hexpand (self->view, TRUE);
  gtk_widget_set_vexpand (self->view, TRUE);
  gtk_container_add (GTK_CONTAINER (self), self->view);

  gtd_task_list_view_set_header_func (GTD_TASK_LIST_VIEW (self->view),
                                      (GtdTaskListViewHeaderFunc) gtd_panel_scheduled_header_func,
                                      self);

  gtd_task_list_view_set_sort_func (GTD_TASK_LIST_VIEW (self->view),
                                    (GtdTaskListViewSortFunc) gtd_panel_scheduled_sort_func,
                                    self);

  gtk_widget_show_all (GTK_WIDGET (self));
}

GtkWidget*
gtd_panel_scheduled_new (void)
{
  return g_object_new (GTD_TYPE_PANEL_SCHEDULED, NULL);
}

