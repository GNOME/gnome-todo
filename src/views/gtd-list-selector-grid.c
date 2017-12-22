/* gtd-list-selector-grid.c
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

#define G_LOG_DOMAIN "GtdListSelectorGrid"

#include "interfaces/gtd-provider.h"
#include "gtd-list-selector.h"
#include "gtd-list-selector-grid.h"
#include "gtd-list-selector-grid-item.h"
#include "gtd-list-selector-item.h"
#include "gtd-manager.h"
#include "gtd-task-list.h"

struct _GtdListSelectorGrid
{
  GtkFlowBox          parent;

  gchar              *search_query;

  GtdWindowMode       mode;
};

static void          gtd_list_selector_iface_init                (GtdListSelectorInterface *iface);

G_DEFINE_TYPE_EXTENDED (GtdListSelectorGrid, gtd_list_selector_grid, GTK_TYPE_FLOW_BOX,
                        0,
                        G_IMPLEMENT_INTERFACE (GTD_TYPE_LIST_SELECTOR,
                                               gtd_list_selector_iface_init))

enum {
  PROP_0,
  PROP_MODE,
  PROP_SEARCH_QUERY,
  N_PROPS
};

static void
gtd_list_selector_grid_list_added (GtdManager          *manager,
                                   GtdTaskList         *list,
                                   GtdListSelectorGrid *selector)
{
  GtkWidget *item;

  item = gtd_list_selector_grid_item_new (list);

  g_object_bind_property (selector,
                          "mode",
                          item,
                          "mode",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);

  gtk_widget_show (item);

  gtk_flow_box_insert (GTK_FLOW_BOX (selector),
                       item,
                       -1);
}

static void
gtd_list_selector_grid_list_removed (GtdManager          *manager,
                                     GtdTaskList         *list,
                                     GtdListSelectorGrid *selector)
{
  GList *children;
  GList *l;

  children = gtk_container_get_children (GTK_CONTAINER (selector));

  for (l = children; l != NULL; l = l->next)
    {
      if (gtd_list_selector_item_get_list (l->data) == list)
        gtk_widget_destroy (l->data);
    }

  g_list_free (children);
}

static gint
gtd_list_selector_grid_sort_func (GtdListSelectorItem *a,
                                  GtdListSelectorItem *b,
                                  GtdListSelectorGrid *selector)
{
  GtdProvider *p1;
  GtdProvider *p2;
  GtdTaskList *l1;
  GtdTaskList *l2;
  gchar *name1;
  gchar *name2;
  gint retval = 0;

  l1 = gtd_list_selector_item_get_list (a);
  p1 = gtd_task_list_get_provider (l1);

  l2 = gtd_list_selector_item_get_list (b);
  p2 = gtd_task_list_get_provider (l2);

  retval = g_strcmp0 (gtd_provider_get_description (p1), gtd_provider_get_description (p2));

  if (retval != 0)
    return retval;

  name1 = g_utf8_normalize (gtd_task_list_get_name (l1), -1, G_NORMALIZE_DEFAULT);
  name2 = g_utf8_normalize (gtd_task_list_get_name (l2), -1, G_NORMALIZE_DEFAULT);

  retval = g_ascii_strcasecmp (name1, name2);

  g_free (name1);
  g_free (name2);

  return retval;
}

static gboolean
gtd_list_selector_grid_filter_func (GtdListSelectorItem *item,
                                    GtdListSelectorGrid *selector)
{
  GtdTaskList *list;
  gboolean return_value;
  gchar *search_folded;
  gchar *list_name_folded;
  gchar *haystack;

  /*
   * When no search query is set, we obviously don't
   * filter out anything.
   */
  if (!selector->search_query)
    return TRUE;

  list = gtd_list_selector_item_get_list (item);
  haystack = NULL;
  search_folded = g_utf8_casefold (selector->search_query, -1);
  list_name_folded = g_utf8_casefold (gtd_task_list_get_name (list), -1);

  if (!search_folded || search_folded[0] == '\0')
    {
      return_value = TRUE;
      goto out;
    }

  haystack = g_strstr_len (list_name_folded,
                           -1,
                           search_folded);

  return_value = (haystack != NULL);

out:
  g_free (search_folded);
  g_free (list_name_folded);

  return return_value;
}

/******************************
 * GtdListSelector iface init *
 ******************************/
static GtdWindowMode
gtd_list_selector_grid_get_mode (GtdListSelector *selector)
{
  g_return_val_if_fail (GTD_IS_LIST_SELECTOR_GRID (selector), GTD_WINDOW_MODE_NORMAL);

  return GTD_LIST_SELECTOR_GRID (selector)->mode;
}

static void
gtd_list_selector_grid_set_mode (GtdListSelector *selector,
                                 GtdWindowMode    mode)
{
  GtdListSelectorGrid *self;

  g_return_if_fail (GTD_IS_LIST_SELECTOR_GRID (selector));

  self = GTD_LIST_SELECTOR_GRID (selector);

  if (self->mode != mode)
    {
      self->mode = mode;

      /* Unselect all items when leaving selection mode */
      if (mode != GTD_WINDOW_MODE_SELECTION)
        {
          gtk_container_foreach (GTK_CONTAINER (self),
                                 (GtkCallback) gtd_list_selector_item_set_selected,
                                 FALSE);
        }


      g_object_notify (G_OBJECT (self), "mode");
    }
}

static const gchar*
gtd_list_selector_grid_get_search_query (GtdListSelector *selector)
{
  g_return_val_if_fail (GTD_IS_LIST_SELECTOR_GRID (selector), NULL);

  return GTD_LIST_SELECTOR_GRID (selector)->search_query;
}

static void
gtd_list_selector_grid_set_search_query (GtdListSelector *selector,
                                         const gchar     *search_query)
{
  GtdListSelectorGrid *self;

  g_return_if_fail (GTD_IS_LIST_SELECTOR_GRID (selector));

  self = GTD_LIST_SELECTOR_GRID (selector);

  if (g_strcmp0 (self->search_query, search_query) != 0)
    {
      g_clear_pointer (&self->search_query, g_free);
      self->search_query = g_strdup (search_query);

      gtk_flow_box_invalidate_filter (GTK_FLOW_BOX (self));

      g_object_notify (G_OBJECT (self), "search-query");
    }
}

static GList*
gtd_list_selector_grid_get_selected_lists (GtdListSelector *selector)
{
  GList *selected;
  GList *children;
  GList *l;

  /* Retrieve the only selected task list */
  children = gtk_container_get_children (GTK_CONTAINER (selector));
  selected = NULL;

  for (l = children; l != NULL; l = l->next)
    {
      if (gtd_list_selector_item_get_selected (l->data))
        selected = g_list_append (selected, l->data);
    }

  g_list_free (children);

  return selected;
}

static void
gtd_list_selector_iface_init (GtdListSelectorInterface *iface)
{
  iface->get_mode = gtd_list_selector_grid_get_mode;
  iface->set_mode = gtd_list_selector_grid_set_mode;
  iface->get_search_query = gtd_list_selector_grid_get_search_query;
  iface->set_search_query = gtd_list_selector_grid_set_search_query;
  iface->get_selected_lists = gtd_list_selector_grid_get_selected_lists;
}

static void
gtd_list_selector_grid_finalize (GObject *object)
{
  GtdListSelectorGrid *self = (GtdListSelectorGrid *)object;

  g_clear_pointer (&self->search_query, g_free);

  G_OBJECT_CLASS (gtd_list_selector_grid_parent_class)->finalize (object);
}

static void
gtd_list_selector_grid_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  GtdListSelectorGrid *self = GTD_LIST_SELECTOR_GRID (object);

  switch (prop_id)
    {
    case PROP_MODE:
      g_value_set_enum (value, self->mode);
      break;

    case PROP_SEARCH_QUERY:
      g_value_set_string (value, self->search_query);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_list_selector_grid_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  GtdListSelector *self = GTD_LIST_SELECTOR (object);

  switch (prop_id)
    {
    case PROP_MODE:
      gtd_list_selector_set_mode (self, g_value_get_enum (value));
      break;

    case PROP_SEARCH_QUERY:
      gtd_list_selector_set_search_query (self, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_list_selector_grid_child_activated (GtkFlowBox      *flowbox,
                                        GtkFlowBoxChild *child)
{
  GtdListSelectorItem *item;
  GtdListSelectorGrid *self;

  self = GTD_LIST_SELECTOR_GRID (flowbox);

  if (!GTD_IS_LIST_SELECTOR_GRID_ITEM (child))
    return;

  item = GTD_LIST_SELECTOR_ITEM (child);

  /* We only mark the item as selected when we're in selection mode */
  if (self->mode == GTD_WINDOW_MODE_SELECTION)
    {
      gtd_list_selector_item_set_selected (item, !gtd_list_selector_item_get_selected (item));
    }

  g_signal_emit_by_name (flowbox, "list-selected", gtd_list_selector_item_get_list (item));
}

static void
gtd_list_selector_grid_class_init (GtdListSelectorGridClass *klass)
{
  GtkFlowBoxClass *flowbox_class = GTK_FLOW_BOX_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  flowbox_class->child_activated = gtd_list_selector_grid_child_activated;

  object_class->finalize = gtd_list_selector_grid_finalize;
  object_class->get_property = gtd_list_selector_grid_get_property;
  object_class->set_property = gtd_list_selector_grid_set_property;

  g_object_class_override_property (object_class,
                                    PROP_MODE,
                                    "mode");

  g_object_class_override_property (object_class,
                                    PROP_SEARCH_QUERY,
                                    "search-query");
}

static void
gtd_list_selector_grid_init (GtdListSelectorGrid *self)
{
  GtdManager *manager;
  GtkFlowBox *flowbox;
  GtkWidget *widget;
  GList *lists;
  GList *l;

  manager = gtd_manager_get_default ();

  g_signal_connect (manager,
                    "list-added",
                    G_CALLBACK (gtd_list_selector_grid_list_added),
                    self);
  g_signal_connect (manager,
                    "list-removed",
                    G_CALLBACK (gtd_list_selector_grid_list_removed),
                    self);

  /* Add already loaded lists */
  lists = gtd_manager_get_task_lists (manager);

  for (l = lists; l != NULL; l = l->next)
    {
      gtd_list_selector_grid_list_added (manager,
                                         l->data,
                                         self);
    }

  g_list_free (lists);

  /* Setup filter and sorting functions */
  gtk_flow_box_set_sort_func (GTK_FLOW_BOX (self),
                              (GtkFlowBoxSortFunc) gtd_list_selector_grid_sort_func,
                              NULL,
                              NULL);

  gtk_flow_box_set_filter_func (GTK_FLOW_BOX (self),
                                (GtkFlowBoxFilterFunc) gtd_list_selector_grid_filter_func,
                                self,
                                NULL);
  /* Setup some properties */
  widget = GTK_WIDGET (self);
  flowbox = GTK_FLOW_BOX (self);

  gtk_flow_box_set_max_children_per_line (flowbox, 9);
  gtk_flow_box_set_selection_mode (flowbox, GTK_SELECTION_NONE);
  gtk_flow_box_set_column_spacing (flowbox, 12);
  gtk_flow_box_set_row_spacing (flowbox, 12);
  gtk_flow_box_set_homogeneous (flowbox, TRUE);
  gtk_container_set_border_width (GTK_CONTAINER (self), 12);
  gtk_widget_set_hexpand (widget, TRUE);
  gtk_widget_set_vexpand (widget, TRUE);
  gtk_widget_set_halign (widget, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (widget, GTK_ALIGN_START);
  gtk_widget_show_all (widget);
}

GtkWidget*
gtd_list_selector_grid_new (void)
{
  return g_object_new (GTD_TYPE_LIST_SELECTOR_GRID, NULL);
}

