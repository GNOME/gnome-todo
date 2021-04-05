/* gtd-next-week-panel.c
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


#define G_LOG_DOMAIN "GtdNextWeekPanel"

#include "gtd-next-week-panel.h"

#include "gnome-todo.h"

#include <glib/gi18n.h>
#include <math.h>


#define GTD_NEXT_WEEK_PANEL_NAME     "next-week-panel"
#define GTD_NEXT_WEEK_PANEL_PRIORITY 700

struct _GtdNextWeekPanel
{
  GtkBox              parent;

  GIcon              *icon;

  guint               number_of_tasks;
  GtdTaskListView    *view;

  GtkFilterListModel *filter_model;
  GtkFilterListModel *incomplete_model;
  GtkSortListModel   *sort_model;

  GtkCssProvider     *css_provider;
};

static void          gtd_panel_iface_init                        (GtdPanelInterface  *iface);

G_DEFINE_TYPE_WITH_CODE (GtdNextWeekPanel, gtd_next_week_panel, GTK_TYPE_BOX,
                         G_IMPLEMENT_INTERFACE (GTD_TYPE_PANEL, gtd_panel_iface_init))

enum
{
  PROP_0,
  PROP_ICON,
  PROP_MENU,
  PROP_NAME,
  PROP_PRIORITY,
  PROP_SUBTITLE,
  PROP_TITLE,
  N_PROPS
};


/*
 * Auxiliary methods
 */

static void
load_css_provider (GtdNextWeekPanel *self)
{
  g_autoptr (GSettings) settings = NULL;
  g_autoptr (GFile) css_file = NULL;
  g_autofree gchar *theme_name = NULL;
  g_autofree gchar *theme_uri = NULL;

  /* Load CSS provider */
  settings = g_settings_new ("org.gnome.desktop.interface");
  theme_name = g_settings_get_string (settings, "gtk-theme");
  theme_uri = g_build_filename ("resource:///org/gnome/todo/plugins/next-week-panel/theme", theme_name, ".css", NULL);
  css_file = g_file_new_for_uri (theme_uri);

  self->css_provider = gtk_css_provider_new ();
  gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                              GTK_STYLE_PROVIDER (self->css_provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  if (g_file_query_exists (css_file, NULL))
    gtk_css_provider_load_from_file (self->css_provider, css_file);
  else
    gtk_css_provider_load_from_resource (self->css_provider, "/org/gnome/todo/plugins/next-week-panel/theme/Adwaita.css");
}

static gboolean
get_date_offset (GDateTime *dt,
                 gint      *days_diff,
                 gint      *years_diff)
{
  g_autoptr (GDateTime) now = NULL;
  GDate now_date, dt_date;

  g_date_clear (&dt_date, 1);
  g_date_set_dmy (&dt_date,
                  g_date_time_get_day_of_month (dt),
                  g_date_time_get_month (dt),
                  g_date_time_get_year (dt));

  now = g_date_time_new_now_local ();

  g_date_clear (&now_date, 1);
  g_date_set_dmy (&now_date,
                  g_date_time_get_day_of_month (now),
                  g_date_time_get_month (now),
                  g_date_time_get_year (now));


  if (days_diff)
    *days_diff = g_date_days_between (&now_date, &dt_date);

  if (years_diff)
    *years_diff = g_date_time_get_year (dt) - g_date_time_get_year (now);

  return TRUE;
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

  if (days_diff < 0)
    {
      str = g_strdup (_("Overdue"));
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
      str = g_date_time_format (dt, "%OB"); // Full month name
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
                        "margin-top", first_header ? 6 : 18,
                        "margin-bottom", 6,
                        "margin-start", 6,
                        "margin-end", 6,
                        "xalign", 0.0,
                        "hexpand", TRUE,
                        NULL);

  context = gtk_widget_get_style_context (label);
  gtk_style_context_add_class (context, span < 0 ? "date-overdue" : "date-scheduled");

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  gtk_box_append (GTK_BOX (box), label);

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

static GtkWidget*
header_func (GtdTask          *task,
             GtdTask          *previous_task,
             GtdNextWeekPanel *self)
{
  g_autoptr (GDateTime) dt = NULL;
  g_autofree gchar *text = NULL;
  gint span;

  dt = gtd_task_get_due_date (task);

  if (previous_task)
    {
      g_autoptr (GDateTime) before_dt = NULL;
      gint before_diff, current_diff;

      before_dt = gtd_task_get_due_date (previous_task);

      get_date_offset (before_dt, &before_diff, NULL);
      get_date_offset (dt, &current_diff, NULL);

      if ((before_diff < 0 && current_diff >= 0) ||
          (before_diff >= 0 && current_diff >= 0 && before_diff != current_diff))
        {
          text = get_string_for_date (dt, &span);
        }
    }
  else
    {
      text = get_string_for_date (dt, &span);
    }

  return text ? create_label (text, span, !previous_task) : NULL;
}

static gint
sort_func (gconstpointer a,
           gconstpointer b,
           gpointer      user_data)
{
  GDateTime *dt1;
  GDateTime *dt2;
  GtdTask *task1;
  GtdTask *task2;
  gint retval;
  gchar *t1;
  gchar *t2;

  task1 = (GtdTask*) a;
  task2 = (GtdTask*) b;

  /* First, compare by ::due-date. */
  dt1 = gtd_task_get_due_date (task1);
  dt2 = gtd_task_get_due_date (task2);

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

  /* Third, compare by ::creation-date. */
  dt1 = gtd_task_get_creation_date (task1);
  dt2 = gtd_task_get_creation_date (task2);

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

  t1 = g_utf8_casefold (gtd_task_get_title (task1), -1);
  t2 = g_utf8_casefold (gtd_task_get_title (task2), -1);

  retval = g_strcmp0 (t1, t2);

  g_free (t1);
  g_free (t2);

  return retval;
}

static gboolean
filter_func (gpointer item,
             gpointer user_data)
{
  g_autoptr (GDateTime) task_dt = NULL;
  GtdTask *task;
  gboolean complete;
  gint days_offset;

  task = (GtdTask*) item;
  complete = gtd_task_get_complete (task);
  task_dt = gtd_task_get_due_date (task);

  return task_dt != NULL &&
         get_date_offset (task_dt, &days_offset, NULL) &&
         days_offset < 7 &&
         ((days_offset < 0 && !complete) || days_offset >= 0);
}

static gboolean
filter_complete_func (gpointer item,
                      gpointer user_data)
{
  GtdTask *task = (GtdTask*) item;

  return !gtd_task_get_complete (task);
}

static void
on_model_items_changed_cb (GListModel       *model,
                           guint             position,
                           guint             n_removed,
                           guint             n_added,
                           GtdNextWeekPanel *self)
{
  if (self->number_of_tasks == g_list_model_get_n_items (model))
    return;

  self->number_of_tasks = g_list_model_get_n_items (model);
  g_object_notify (G_OBJECT (self), "subtitle");
}

static void
on_clock_day_changed_cb (GtdClock         *clock,
                         GtdNextWeekPanel *self)
{
  g_autoptr (GDateTime) now = NULL;
  GtkFilter *filter;

  now = g_date_time_new_now_local ();
  gtd_task_list_view_set_default_date (self->view, now);

  filter = gtk_filter_list_model_get_filter (self->filter_model);
  gtk_filter_changed (filter, GTK_FILTER_CHANGE_DIFFERENT);
}

/*
 * GtdPanel iface
 */

static const gchar*
gtd_panel_next_week_get_panel_name (GtdPanel *panel)
{
  return GTD_NEXT_WEEK_PANEL_NAME;
}

static const gchar*
gtd_panel_next_week_get_panel_title (GtdPanel *panel)
{
  return _("Next 7 Days");
}

static GList*
gtd_panel_next_week_get_header_widgets (GtdPanel *panel)
{
  return NULL;
}

static const GMenu*
gtd_panel_next_week_get_menu (GtdPanel *panel)
{
  return NULL;
}

static GIcon*
gtd_panel_next_week_get_icon (GtdPanel *panel)
{
  return g_object_ref (GTD_NEXT_WEEK_PANEL (panel)->icon);
}

static guint32
gtd_panel_next_week_get_priority (GtdPanel *panel)
{
  return GTD_NEXT_WEEK_PANEL_PRIORITY;
}

static gchar*
gtd_panel_next_week_get_subtitle (GtdPanel *panel)
{
  GtdNextWeekPanel *self = GTD_NEXT_WEEK_PANEL (panel);

  return g_strdup_printf ("%d", self->number_of_tasks);
}

static void
gtd_panel_iface_init (GtdPanelInterface *iface)
{
  iface->get_panel_name = gtd_panel_next_week_get_panel_name;
  iface->get_panel_title = gtd_panel_next_week_get_panel_title;
  iface->get_header_widgets = gtd_panel_next_week_get_header_widgets;
  iface->get_menu = gtd_panel_next_week_get_menu;
  iface->get_icon = gtd_panel_next_week_get_icon;
  iface->get_priority = gtd_panel_next_week_get_priority;
  iface->get_subtitle = gtd_panel_next_week_get_subtitle;
}


/*
 * GObject overrides
 */

static void
gtd_next_week_panel_finalize (GObject *object)
{
  GtdNextWeekPanel *self = (GtdNextWeekPanel *)object;

  g_clear_object (&self->css_provider);
  g_clear_object (&self->icon);
  g_clear_object (&self->filter_model);
  g_clear_object (&self->incomplete_model);
  g_clear_object (&self->sort_model);

  G_OBJECT_CLASS (gtd_next_week_panel_parent_class)->finalize (object);
}

static void
gtd_next_week_panel_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  GtdNextWeekPanel *self = GTD_NEXT_WEEK_PANEL (object);

  switch (prop_id)
    {
    case PROP_ICON:
      g_value_set_object (value, self->icon);
      break;

    case PROP_MENU:
      g_value_set_object (value, NULL);
      break;

    case PROP_NAME:
      g_value_set_string (value, GTD_NEXT_WEEK_PANEL_NAME);
      break;

    case PROP_PRIORITY:
      g_value_set_uint (value, GTD_NEXT_WEEK_PANEL_PRIORITY);
      break;

    case PROP_SUBTITLE:
      g_value_take_string (value, gtd_panel_get_subtitle (GTD_PANEL (self)));
      break;

    case PROP_TITLE:
      g_value_set_string (value, gtd_panel_get_panel_title (GTD_PANEL (self)));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_next_week_panel_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
gtd_next_week_panel_class_init (GtdNextWeekPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gtd_next_week_panel_finalize;
  object_class->get_property = gtd_next_week_panel_get_property;
  object_class->set_property = gtd_next_week_panel_set_property;

  g_object_class_override_property (object_class, PROP_ICON, "icon");
  g_object_class_override_property (object_class, PROP_MENU, "menu");
  g_object_class_override_property (object_class, PROP_NAME, "name");
  g_object_class_override_property (object_class, PROP_PRIORITY, "priority");
  g_object_class_override_property (object_class, PROP_SUBTITLE, "subtitle");
  g_object_class_override_property (object_class, PROP_TITLE, "title");
}

static void
gtd_next_week_panel_init (GtdNextWeekPanel *self)
{
  g_autoptr (GDateTime) now = g_date_time_new_now_local ();
  GtdManager *manager = gtd_manager_get_default ();
  GtkCustomFilter *incomplete_filter;
  GtkCustomFilter *filter;
  GtkCustomSorter *sorter;

  self->icon = g_themed_icon_new ("view-tasks-week-symbolic");

  filter = gtk_custom_filter_new (filter_func, self, NULL);
  self->filter_model = gtk_filter_list_model_new (gtd_manager_get_tasks_model (manager),
                                                  GTK_FILTER (filter));

  sorter = gtk_custom_sorter_new (sort_func, self, NULL);
  self->sort_model = gtk_sort_list_model_new (G_LIST_MODEL (self->filter_model),
                                              GTK_SORTER (sorter));

  incomplete_filter = gtk_custom_filter_new (filter_complete_func, self, NULL);
  self->incomplete_model = gtk_filter_list_model_new (G_LIST_MODEL (self->sort_model),
                                                      GTK_FILTER (incomplete_filter));

  /* The main view */
  self->view = GTD_TASK_LIST_VIEW (gtd_task_list_view_new ());
  gtd_task_list_view_set_model (GTD_TASK_LIST_VIEW (self->view), G_LIST_MODEL (self->sort_model));
  gtd_task_list_view_set_handle_subtasks (GTD_TASK_LIST_VIEW (self->view), FALSE);
  gtd_task_list_view_set_show_list_name (GTD_TASK_LIST_VIEW (self->view), TRUE);
  gtd_task_list_view_set_show_due_date (GTD_TASK_LIST_VIEW (self->view), FALSE);
  gtd_task_list_view_set_default_date (self->view, now);

  gtk_widget_set_hexpand (GTK_WIDGET (self->view), TRUE);
  gtk_widget_set_vexpand (GTK_WIDGET (self->view), TRUE);
  gtk_box_append (GTK_BOX (self), GTK_WIDGET (self->view));

  gtd_task_list_view_set_header_func (GTD_TASK_LIST_VIEW (self->view),
                                      (GtdTaskListViewHeaderFunc) header_func,
                                      self);

  g_signal_connect_object (self->incomplete_model,
                           "items-changed",
                           G_CALLBACK (on_model_items_changed_cb),
                           self,
                           0);

  g_signal_connect_object (gtd_manager_get_clock (manager),
                           "day-changed",
                           G_CALLBACK (on_clock_day_changed_cb),
                           self,
                           0);
  load_css_provider (self);
}

GtkWidget*
gtd_next_week_panel_new (void)
{
  return g_object_new (GTD_TYPE_NEXT_WEEK_PANEL, NULL);
}
