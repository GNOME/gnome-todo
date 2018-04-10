/* gtd-sidebar-list-row.c
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

#include "gtd-task.h"
#include "gtd-task-list.h"
#include "gtd-sidebar-list-row.h"

#include <math.h>

struct _GtdSidebarListRow
{
  GtkListBoxRow       parent;

  GtkImage           *color_icon;
  GtkLabel           *name_label;
  GtkLabel           *tasks_counter_label;

  GtdTaskList        *list;
};


static void          on_list_changed_cb                          (GtdSidebarListRow  *self);


static void          on_list_color_changed_cb                    (GtdTaskList        *list,
                                                                  GParamSpec         *pspec,
                                                                  GtdSidebarListRow  *self);

G_DEFINE_TYPE (GtdSidebarListRow, gtd_sidebar_list_row, GTK_TYPE_LIST_BOX_ROW)

enum
{
  PROP_0,
  PROP_LIST,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];


/*
 * Auxiliary methods
 */

static cairo_surface_t*
create_circular_icon (GtdTaskList *list,
                      gint         size)
{
  g_autoptr (GdkRGBA) color = NULL;
  cairo_surface_t *surface;
  cairo_t *cr;

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, size, size);
  cr = cairo_create (surface);

  /* Draw the list's background color */
  color = gtd_task_list_get_color (list);

  cairo_set_source_rgba (cr,
                         color->red,
                         color->green,
                         color->blue,
                         color->alpha);

  cairo_arc (cr,
             size / 2.0,
             size / 2.0,
             size / 2.0,
             0.,
             2 * M_PI);

  cairo_fill (cr);

  g_clear_pointer (&cr, cairo_destroy);

  return surface;
}

static void
update_color_icon (GtdSidebarListRow *self)
{
  cairo_surface_t *surface;

  surface = create_circular_icon (self->list, 12);

  gtk_image_set_from_surface (self->color_icon, surface);

  cairo_surface_destroy (surface);
}

static void
update_counter_label (GtdSidebarListRow *self)
{
  g_autoptr (GList) tasks = NULL;
  g_autofree gchar *label = NULL;
  GList *l;
  guint counter = 0;

  tasks = gtd_task_list_get_tasks (self->list);

  for (l = tasks; l; l = l->next)
    counter += !gtd_task_get_complete (l->data);

  label = counter > 0 ? g_strdup_printf ("%u", counter) : g_strdup ("");

  gtk_label_set_label (self->tasks_counter_label, label);
}

static void
set_list (GtdSidebarListRow *self,
          GtdTaskList       *list)
{
  g_assert (list != NULL);
  g_assert (self->list == NULL);

  self->list = g_object_ref (list);

  g_object_bind_property (list,
                          "name",
                          self->name_label,
                          "label",
                          G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

  /* Always keep the counter label updated */
  g_signal_connect_object (list, "task-added", G_CALLBACK (on_list_changed_cb), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (list, "task-updated", G_CALLBACK (on_list_changed_cb), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (list, "task-removed", G_CALLBACK (on_list_changed_cb), self, G_CONNECT_SWAPPED);

  update_counter_label (self);

  /* And also the color icon */
  g_signal_connect_object (list, "notify::color", G_CALLBACK (on_list_color_changed_cb), self, 0);

  update_color_icon (self);
}


/*
 * Callbacks
 */

static void
on_list_changed_cb (GtdSidebarListRow *self)
{
  update_counter_label (self);
}

static void
on_list_color_changed_cb (GtdTaskList       *list,
                          GParamSpec        *pspec,
                          GtdSidebarListRow *self)
{
  update_color_icon (self);
}


/*
 * GObject overrides
 */

static void
gtd_sidebar_list_row_finalize (GObject *object)
{
  GtdSidebarListRow *self = (GtdSidebarListRow *)object;

  g_clear_object (&self->list);

  G_OBJECT_CLASS (gtd_sidebar_list_row_parent_class)->finalize (object);
}

static void
gtd_sidebar_list_row_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  GtdSidebarListRow *self = GTD_SIDEBAR_LIST_ROW (object);

  switch (prop_id)
    {
    case PROP_LIST:
      g_value_set_object (value, self->list);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_sidebar_list_row_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  GtdSidebarListRow *self = GTD_SIDEBAR_LIST_ROW (object);

  switch (prop_id)
    {
    case PROP_LIST:
      set_list (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_sidebar_list_row_class_init (GtdSidebarListRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gtd_sidebar_list_row_finalize;
  object_class->get_property = gtd_sidebar_list_row_get_property;
  object_class->set_property = gtd_sidebar_list_row_set_property;

  properties[PROP_LIST] = g_param_spec_object ("list",
                                               "List",
                                               "The task list this row represents",
                                               GTD_TYPE_TASK_LIST,
                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/todo/ui/sidebar-list-row.ui");

  gtk_widget_class_bind_template_child (widget_class, GtdSidebarListRow, color_icon);
  gtk_widget_class_bind_template_child (widget_class, GtdSidebarListRow, name_label);
  gtk_widget_class_bind_template_child (widget_class, GtdSidebarListRow, tasks_counter_label);
}

static void
gtd_sidebar_list_row_init (GtdSidebarListRow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget*
gtd_sidebar_list_row_new (GtdTaskList *list)
{
  return g_object_new (GTD_TYPE_SIDEBAR_LIST_ROW,
                       "list", list,
                       NULL);
}

GtdTaskList*
gtd_sidebar_list_row_get_task_list (GtdSidebarListRow *self)
{
  g_return_val_if_fail (GTD_IS_SIDEBAR_LIST_ROW (self), NULL);

  return self->list;
}
