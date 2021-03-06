/* gtd-task-list-popover.c
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

#define G_LOG_DOMAIN "GtdTaskListPopover"

#include "models/gtd-list-model-filter.h"
#include "gtd-debug.h"
#include "gtd-manager.h"
#include "gtd-provider.h"
#include "gtd-task-list.h"
#include "gtd-task-list-popover.h"
#include "gtd-utils.h"

struct _GtdTaskListPopover
{
  GtkPopover          parent;

  GtkFilterListModel *filter_model;

  GtkSizeGroup       *sizegroup;
  GtkListBox         *listbox;
  GtkEditable        *search_entry;

  GtdTaskList        *selected_list;
  GtdManager         *manager;
};

G_DEFINE_TYPE (GtdTaskListPopover, gtd_task_list_popover, GTK_TYPE_POPOVER)

enum
{
  PROP_0,
  PROP_TASK_LIST,
  N_PROPS
};

static GParamSpec *properties[N_PROPS] = { NULL, };

/*
 * Auxiliary methods
 */

static void
set_selected_tasklist (GtdTaskListPopover *self,
                       GtdTaskList        *list)
{
  GtdManager *manager;

  manager = gtd_manager_get_default ();

  /* NULL list means the inbox */
  if (!list)
    list = gtd_manager_get_inbox (manager);

  if (g_set_object (&self->selected_list, list))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TASK_LIST]);
}


/*
 * Callbacks
 */

static gboolean
filter_listbox_cb (gpointer  item,
                   gpointer  user_data)
{
  GtdTaskListPopover *self;
  g_autofree gchar *normalized_list_name = NULL;
  g_autofree gchar *normalized_search_query = NULL;
  GtdTaskList *list;

  self = (GtdTaskListPopover*) user_data;
  list = (GtdTaskList*) item;

  normalized_search_query = gtd_normalize_casefold_and_unaccent (gtk_editable_get_text (self->search_entry));
  normalized_list_name = gtd_normalize_casefold_and_unaccent (gtd_task_list_get_name (list));

  return g_strrstr (normalized_list_name, normalized_search_query) != NULL;
}

static GtkWidget*
create_list_row_cb (gpointer item,
                    gpointer data)
{
  g_autoptr (GdkPaintable) paintable = NULL;
  g_autoptr (GdkRGBA) color = NULL;
  GtdTaskListPopover *self;
  GtkWidget *provider;
  GtkWidget *icon;
  GtkWidget *name;
  GtkWidget *row;
  GtkWidget *box;

  self = GTD_TASK_LIST_POPOVER (data);
  box = g_object_new (GTK_TYPE_BOX,
                      "orientation", GTK_ORIENTATION_HORIZONTAL,
                      "spacing", 12,
                      "margin-top", 6,
                      "margin-bottom", 6,
                      "margin-start", 6,
                      "margin-end", 6,
                      NULL);

  /* Icon */
  color = gtd_task_list_get_color (item);
  paintable = gtd_create_circular_paintable (color, 12);
  icon = gtk_image_new_from_paintable (paintable);
  gtk_widget_set_size_request (icon, 12, 12);
  gtk_widget_set_halign (icon, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (icon, GTK_ALIGN_CENTER);

  gtk_box_append (GTK_BOX (box), icon);

  /* Tasklist name */
  name = g_object_new (GTK_TYPE_LABEL,
                       "label", gtd_task_list_get_name (item),
                       "xalign", 0.0,
                       "hexpand", TRUE,
                       NULL);

  gtk_box_append (GTK_BOX (box), name);

  /* Provider name */
  provider = g_object_new (GTK_TYPE_LABEL,
                           "label", gtd_provider_get_description (gtd_task_list_get_provider (item)),
                           "xalign", 0.0,
                           NULL);
  gtk_style_context_add_class (gtk_widget_get_style_context (provider), "dim-label");
  gtk_size_group_add_widget (self->sizegroup, provider);
  gtk_box_append (GTK_BOX (box), provider);

  /* The row itself */
  row = gtk_list_box_row_new ();
  gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), box);

  g_object_set_data (G_OBJECT (row), "tasklist", item);

  return row;
}

static void
on_listbox_row_activated_cb (GtkListBox         *listbox,
                             GtkListBoxRow      *row,
                             GtdTaskListPopover *self)
{
  GtdTaskList *list = g_object_get_data (G_OBJECT (row), "tasklist");

  set_selected_tasklist (self, list);
  gtk_popover_popdown (GTK_POPOVER (self));
  gtk_editable_set_text (self->search_entry, "");
}

static void
on_popover_closed_cb (GtkPopover         *popover,
                      GtdTaskListPopover *self)
{
  gtk_editable_set_text (self->search_entry, "");
}

static void
on_search_entry_activated_cb (GtkEntry           *entry,
                              GtdTaskListPopover *self)
{
  g_autoptr (GtdTaskList) list = g_list_model_get_item (G_LIST_MODEL (self->filter_model), 0);

  set_selected_tasklist (self, list);
  gtk_popover_popdown (GTK_POPOVER (self));
  gtk_editable_set_text (self->search_entry, "");
}

static void
on_search_entry_search_changed_cb (GtkEntry           *search_entry,
                                   GtdTaskListPopover *self)
{
  GtkFilter *filter;

  filter = gtk_filter_list_model_get_filter (self->filter_model);
  gtk_filter_changed (filter, GTK_FILTER_CHANGE_DIFFERENT);
}


/*
 * GObject overrides
 */

static void
gtd_task_list_popover_finalize (GObject *object)
{
  GtdTaskListPopover *self = (GtdTaskListPopover *)object;

  g_clear_object (&self->selected_list);

  G_OBJECT_CLASS (gtd_task_list_popover_parent_class)->finalize (object);
}

static void
gtd_task_list_popover_class_init (GtdTaskListPopoverClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gtd_task_list_popover_finalize;

  properties[PROP_TASK_LIST] = g_param_spec_object ("task-list",
                                                    "Task list",
                                                    "Task list",
                                                    GTD_TYPE_TASK_LIST,
                                                    G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/todo/ui/gtd-task-list-popover.ui");

  gtk_widget_class_bind_template_child (widget_class, GtdTaskListPopover, listbox);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskListPopover, search_entry);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskListPopover, sizegroup);

  gtk_widget_class_bind_template_callback (widget_class, on_listbox_row_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_popover_closed_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_search_entry_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_search_entry_search_changed_cb);
}

static void
gtd_task_list_popover_init (GtdTaskListPopover *self)
{
  GtdManager *manager = gtd_manager_get_default ();
  GtkCustomFilter *filter;

  filter = gtk_custom_filter_new (filter_listbox_cb, self, NULL);
  self->filter_model = gtk_filter_list_model_new (gtd_manager_get_task_lists_model (manager),
                                                  GTK_FILTER (filter));

  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_list_box_bind_model (self->listbox,
                           G_LIST_MODEL (self->filter_model),
                           create_list_row_cb,
                           self,
                           NULL);

  self->manager = manager;

  set_selected_tasklist (self, NULL);
}

GtdTaskList*
gtd_task_list_popover_get_task_list (GtdTaskListPopover *self)
{
  g_return_val_if_fail (GTD_IS_TASK_LIST_POPOVER (self), NULL);

  return self->selected_list;
}
