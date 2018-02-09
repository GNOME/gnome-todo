#73/* gtd-list-selector-list-item.c
 *
 * Copyright (C) 2016 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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

#define G_LOG_DOMAIN "GtdListSelectorItem"

#include "gtd-enum-types.h"
#include "gtd-task.h"
#include "gtd-task-list.h"
#include "gtd-list-selector-item.h"
#include "gtd-list-selector-list-item.h"

#include <math.h>

struct _GtdListSelectorListItem
{
  GtkListBoxRow       parent;

  GtkWidget          *eventbox;
  GtkWidget          *name_label;
  GtkWidget          *provider_label;
  GtkWidget          *selection_check;
  GtkWidget          *spinner;
  GtkWidget          *stack;
  GtkWidget          *thumbnail_image;

  /* data */
  GtdTaskList        *list;
  GtdWindowMode       mode;
};


static void          gtd_list_selector_item_iface_init           (GtdListSelectorItemInterface *iface);

G_DEFINE_TYPE_EXTENDED (GtdListSelectorListItem, gtd_list_selector_list_item, GTK_TYPE_LIST_BOX_ROW,
                        0,
                        G_IMPLEMENT_INTERFACE (GTD_TYPE_LIST_SELECTOR_ITEM,
                                               gtd_list_selector_item_iface_init))

#define LUMINANCE(c)              (0.299 * c->red + 0.587 * c->green + 0.114 * c->blue)

#define THUMBNAIL_SIZE            24

enum {
  PROP_0,
  PROP_MODE,
  PROP_SELECTED,
  PROP_TASK_LIST,
  N_PROPS
};

static void
set_hand_cursor (GtkWidget *widget,
                 gboolean   show_cursor)
{
  GdkDisplay *display;
  GdkCursor *cursor;

  if (!gtk_widget_get_realized (widget))
    return;

  display = gtk_widget_get_display (widget);
  cursor = show_cursor ? gdk_cursor_new_from_name (display, "pointer") : NULL;

  gdk_window_set_cursor (gtk_widget_get_window (widget), cursor);
  gdk_display_flush (display);

  g_clear_object (&cursor);
}

static gboolean
enter_notify_event (GtkWidget               *widget,
                    GdkEvent                *event,
                    GtdListSelectorListItem *item)
{
  /* When in selection mode, ignore this and always show the checkbox */
  if (item->mode == GTD_WINDOW_MODE_SELECTION)
    return GDK_EVENT_PROPAGATE;

  set_hand_cursor (widget, TRUE);
  gtk_stack_set_visible_child (GTK_STACK (item->stack), item->selection_check);

  return GDK_EVENT_PROPAGATE;
}

static gboolean
leave_notify_event (GtkWidget               *widget,
                    GdkEvent                *event,
                    GtdListSelectorListItem *item)
{
  /* When in selection mode, ignore this and always show the checkbox */
  if (item->mode == GTD_WINDOW_MODE_SELECTION)
    return GDK_EVENT_PROPAGATE;

  set_hand_cursor (widget, FALSE);
  gtk_stack_set_visible_child (GTK_STACK (item->stack), item->thumbnail_image);

  return GDK_EVENT_PROPAGATE;
}

static cairo_surface_t*
gtd_list_selector_list_item__render_thumbnail (GtdListSelectorListItem *item)
{
  GtkStyleContext *context;
  cairo_surface_t *surface;
  GtdTaskList *list;
  GdkRGBA *color;
  cairo_t *cr;
  gint width;
  gint height;

  list = item->list;
  gtk_widget_get_size_request (item->thumbnail_image,
                               &width,
                               &height);
  context = gtk_widget_get_style_context (GTK_WIDGET (item));
  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                        width,
                                        height);
  cr = cairo_create (surface);

  /* Draw the list's background color */
  color = gtd_task_list_get_color (list);

  gtk_style_context_save (context);
  gtk_style_context_add_class (context, "thumbnail");

  gtk_render_background (context,
                         cr,
                         4.0,
                         4.0,
                         THUMBNAIL_SIZE,
                         THUMBNAIL_SIZE);

  cairo_set_source_rgba (cr,
                         color->red,
                         color->green,
                         color->blue,
                         color->alpha);

  cairo_arc (cr,
             THUMBNAIL_SIZE / 2.0 + 4.0,
             THUMBNAIL_SIZE / 2.0 + 4.0,
             THUMBNAIL_SIZE / 2.0,
             0.,
             2 * M_PI);

  cairo_fill (cr);

  gtk_style_context_restore (context);
  gdk_rgba_free (color);
  cairo_destroy (cr);

  return surface;
}

static void
gtd_list_selector_list_item__update_thumbnail (GtdListSelectorListItem *item)
{
  cairo_surface_t *surface;

  surface = gtd_list_selector_list_item__render_thumbnail (item);

  gtk_image_set_from_surface (GTK_IMAGE (item->thumbnail_image), surface);

  cairo_surface_destroy (surface);
}

static GtdTaskList*
gtd_list_selector_list_item_get_list (GtdListSelectorItem *item)
{
  g_return_val_if_fail (GTD_IS_LIST_SELECTOR_LIST_ITEM (item), NULL);

  return GTD_LIST_SELECTOR_LIST_ITEM (item)->list;
}

static gboolean
gtd_list_selector_list_item_get_selected (GtdListSelectorItem *item)
{
  GtdListSelectorListItem *self;

  g_return_val_if_fail (GTD_IS_LIST_SELECTOR_LIST_ITEM (item), FALSE);

  self = GTD_LIST_SELECTOR_LIST_ITEM (item);

  return gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->selection_check));
}

static void
gtd_list_selector_list_item_set_selected (GtdListSelectorItem *item,
                                          gboolean             selected)
{
  GtdListSelectorListItem *self;

  g_return_if_fail (GTD_IS_LIST_SELECTOR_LIST_ITEM (item));

  self = GTD_LIST_SELECTOR_LIST_ITEM (item);

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->selection_check)) != selected)
    {
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (self->selection_check), selected);

      gtd_list_selector_list_item__update_thumbnail (self);

      g_object_notify (G_OBJECT (item), "selected");
    }
}

static void
gtd_list_selector_item_iface_init (GtdListSelectorItemInterface *iface)
{
  iface->get_list = gtd_list_selector_list_item_get_list;
  iface->get_selected = gtd_list_selector_list_item_get_selected;
  iface->set_selected = gtd_list_selector_list_item_set_selected;
}

static void
gtd_list_selector_list_item_get_property (GObject    *object,
                                          guint       prop_id,
                                          GValue     *value,
                                          GParamSpec *pspec)
{
  GtdListSelectorListItem *self = GTD_LIST_SELECTOR_LIST_ITEM (object);

  switch (prop_id)
    {
    case PROP_MODE:
      g_value_set_enum (value, self->mode);
      break;

    case PROP_SELECTED:
      g_value_set_boolean (value, gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->selection_check)));
      break;

    case PROP_TASK_LIST:
      g_value_set_object (value, self->list);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_list_selector_list_item_set_property (GObject      *object,
                                          guint         prop_id,
                                          const GValue *value,
                                          GParamSpec   *pspec)
{
  GtdListSelectorListItem *self = GTD_LIST_SELECTOR_LIST_ITEM (object);

  switch (prop_id)
    {
    case PROP_MODE:
      self->mode = g_value_get_enum (value);

      /* When we go to selection mode, we always show the selection
       * checkbox. If we're not in selection mode, we only show it
       * when the mouse hovers.
       */
      if (self->mode == GTD_WINDOW_MODE_SELECTION)
        gtk_stack_set_visible_child (GTK_STACK (self->stack), self->selection_check);
      else
        gtk_stack_set_visible_child (GTK_STACK (self->stack), self->thumbnail_image);

      g_object_notify (object, "mode");
      break;

    case PROP_SELECTED:
      gtd_list_selector_item_set_selected (GTD_LIST_SELECTOR_ITEM (self),
                                           g_value_get_boolean (value));
      break;

    case PROP_TASK_LIST:
      self->list = g_value_get_object (value);
      g_object_bind_property (self->list,
                              "name",
                              self->name_label,
                              "label",
                              G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

      g_object_bind_property (gtd_task_list_get_provider (self->list),
                              "description",
                              self->provider_label,
                              "label",
                              G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

      g_object_bind_property (self->list,
                              "ready",
                              self->spinner,
                              "visible",
                              G_BINDING_DEFAULT | G_BINDING_INVERT_BOOLEAN | G_BINDING_SYNC_CREATE);

      g_object_bind_property (self->list,
                              "ready",
                              self->spinner,
                              "active",
                              G_BINDING_DEFAULT | G_BINDING_INVERT_BOOLEAN | G_BINDING_SYNC_CREATE);

      g_signal_connect_swapped (self->list,
                                "notify::color",
                                G_CALLBACK (gtd_list_selector_list_item__update_thumbnail),
                                self);

      gtd_list_selector_list_item__update_thumbnail (self);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_list_selector_list_item_class_init (GtdListSelectorListItemClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = gtd_list_selector_list_item_get_property;
  object_class->set_property = gtd_list_selector_list_item_set_property;

  /**
   * GtdListSelectorListItem::mode:
   *
   * The parent source of the list.
   */
  g_object_class_override_property (object_class,
                                    PROP_MODE,
                                    "mode");

  /**
   * GtdListSelectorListItem::selected:
   *
   * Whether this item is selected when in %GTD_WINDOW_MODE_SELECTION.
   */
  g_object_class_override_property (object_class,
                                    PROP_SELECTED,
                                    "selected");

  /**
   * GtdListSelectorListItem::list:
   *
   * The parent source of the list.
   */
  g_object_class_override_property (object_class,
                                    PROP_TASK_LIST,
                                    "task-list");

  /* template class */
  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/todo/ui/list-selector-list-item.ui");

  gtk_widget_class_bind_template_child (widget_class, GtdListSelectorListItem, eventbox);
  gtk_widget_class_bind_template_child (widget_class, GtdListSelectorListItem, name_label);
  gtk_widget_class_bind_template_child (widget_class, GtdListSelectorListItem, provider_label);
  gtk_widget_class_bind_template_child (widget_class, GtdListSelectorListItem, spinner);
  gtk_widget_class_bind_template_child (widget_class, GtdListSelectorListItem, stack);
  gtk_widget_class_bind_template_child (widget_class, GtdListSelectorListItem, selection_check);
  gtk_widget_class_bind_template_child (widget_class, GtdListSelectorListItem, thumbnail_image);

  gtk_widget_class_bind_template_callback (widget_class, enter_notify_event);
  gtk_widget_class_bind_template_callback (widget_class, leave_notify_event);
}

static void
gtd_list_selector_list_item_init (GtdListSelectorListItem *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  g_object_bind_property (self->selection_check,
                          "active",
                          self,
                          "selected",
                          G_BINDING_BIDIRECTIONAL);
}

GtkWidget*
gtd_list_selector_list_item_new (GtdTaskList *list)
{
  return g_object_new (GTD_TYPE_LIST_SELECTOR_LIST_ITEM,
                       "task-list", list,
                       NULL);
}
