/* gtd-task-list-item.c
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

#define G_LOG_DOMAIN "GtdListSelectorGridItem"

#include "gtd-enum-types.h"
#include "gtd-task.h"
#include "gtd-task-list.h"
#include "gtd-list-selector-grid-item.h"
#include "gtd-list-selector-item.h"

#include <glib/gi18n.h>

struct _GtdListSelectorGridItem
{
  GtkFlowBoxChild            parent;

  GtkImage                  *icon_image;
  GtkLabel                  *subtitle_label;
  GtkLabel                  *title_label;
  GtkSpinner                *spinner;

  /* data */
  GtdTaskList               *list;
  GtdWindowMode              mode;

  /* Custom CSS */
  GtkCssProvider            *css_provider;

  /* flags */
  gint                      selected;

};

static void          gtd_list_selector_item_iface_init           (GtdListSelectorItemInterface *iface);

G_DEFINE_TYPE_EXTENDED (GtdListSelectorGridItem, gtd_list_selector_grid_item, GTK_TYPE_FLOW_BOX_CHILD,
                        0,
                        G_IMPLEMENT_INTERFACE (GTD_TYPE_LIST_SELECTOR_ITEM,
                                               gtd_list_selector_item_iface_init))

#define LUMINANCE(c)              (0.299 * c->red + 0.587 * c->green + 0.114 * c->blue)

#define THUMBNAIL_SIZE            192
#define CHECK_SIZE                40

enum {
  PROP_0,
  PROP_MODE,
  PROP_SELECTED,
  PROP_TASK_LIST,
  LAST_PROP
};

static cairo_surface_t*
gtd_list_selector_grid_item__render_thumbnail (GtdListSelectorGridItem *item)
{
  PangoFontDescription *font_desc;
  GtkStyleContext *context;
  cairo_surface_t *surface;
  GtkStateFlags state;
  PangoLayout *layout;
  GtdTaskList *list;
  GtkBorder margin;
  GtkBorder padding;
  GdkRGBA *color;
  cairo_t *cr;
  GList *tasks;
  gint scale_factor;
  gint width, height;

  list = item->list;
  color = gtd_task_list_get_color (list);
  scale_factor = gtk_widget_get_scale_factor (GTK_WIDGET (item));
  width = THUMBNAIL_SIZE * scale_factor;
  height = THUMBNAIL_SIZE * scale_factor;
  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                        width,
                                        height);
  cr = cairo_create (surface);

  /*
   * We'll draw the task names according to the font size, margin & padding
   * specified by the .thumbnail class. With that, it can be adapted to any
   * other themes.
   */
  context = gtk_widget_get_style_context (GTK_WIDGET (item));
  state = gtk_style_context_get_state (context);

  gtk_style_context_save (context);
  gtk_style_context_add_class (context, "thumbnail");

  gtk_style_context_get (context,
                         state,
                         "font", &font_desc,
                         NULL);
  gtk_style_context_get_margin (context,
                                state,
                                &margin);
  gtk_style_context_get_padding (context,
                                 state,
                                 &padding);

  /* Draw the first tasks from the list */
  layout = pango_cairo_create_layout (cr);
  tasks = gtd_task_list_get_tasks (list);

  /*
   * If the list color is way too dark, we draw the task names in a light
   * font color.
   */
  if (LUMINANCE (color) < 0.5)
    {
      gtk_style_context_add_class (context, "dark");
      gtk_style_context_remove_class (context, "light");
    }
  else
    {
      gtk_style_context_add_class (context, "light");
      gtk_style_context_remove_class (context, "dark");
    }

  /*
   * Sort the list, so that the first tasks are similar to what
   * the user will see when selecting the list.
   */
  tasks = g_list_sort (tasks, (GCompareFunc) gtd_task_compare);
  width -= padding.left + margin.left + padding.right + margin.right;

  pango_layout_set_font_description (layout, font_desc);
  pango_layout_set_ellipsize (layout, PANGO_ELLIPSIZE_END);
  pango_layout_set_width (layout, width * PANGO_SCALE);

  /*
   * If the list exists and it's first element is a completed task,
   * we know for sure (since the list is already sorted) that there's
   * no undone tasks here.
   */
  if (tasks && !gtd_task_get_complete (tasks->data))
    {
      /* Draw the task name for each selected row. */
      gdouble x, y;
      GList *l;

      x = margin.left + padding.left;
      y = margin.top + padding.top;

      for (l = tasks; l != NULL; l = l->next)
        {
          GString *string;
          gchar *formatted_title;
          gint i, font_height;

          /* Don't render completed tasks */
          if (gtd_task_get_complete (l->data))
            continue;

          /* Hardcoded spacing between tasks */
          y += 4;

          /* Adjust the title according to the subtask hierarchy */
          string = g_string_new ("");

          for (i = 0; i < gtd_task_get_depth (l->data); i++)
            g_string_append (string, "    ");

          g_string_append (string, gtd_task_get_title (l->data));

          formatted_title = g_string_free (string, FALSE);

          /* Set the real title */
          pango_layout_set_text (layout,
                                 formatted_title,
                                 -1);

          pango_layout_get_pixel_size (layout,
                                       NULL,
                                       &font_height);

          g_free (formatted_title);

          /*
           * If we reach the last visible row, it should draw a
           * "…" mark and stop drawing anything else
           */
          if (y + font_height + 4 + margin.bottom + padding.bottom > THUMBNAIL_SIZE * scale_factor)
            {
              pango_layout_set_text (layout,
                                     "…",
                                     -1);

              gtk_render_layout (context,
                             cr,
                             x,
                             y,
                             layout);
              break;
            }

          gtk_render_layout (context,
                             cr,
                             x,
                             y,
                             layout);

          y += font_height;
        }

      g_list_free (tasks);
    }
  else
    {
      /*
       * If there's no task available, draw a "No tasks" string at
       * the middle of the list thumbnail.
       */
      gdouble y;
      gint font_height;

      pango_layout_set_text (layout,
                             _("No tasks"),
                             -1);
      pango_layout_set_alignment (layout, PANGO_ALIGN_CENTER);
      pango_layout_get_pixel_size (layout,
                                   NULL,
                                   &font_height);

      y = (THUMBNAIL_SIZE - font_height) * scale_factor / 2.0;

      gtk_render_layout (context,
                         cr,
                         margin.left,
                         y,
                         layout);
    }

  pango_font_description_free (font_desc);
  g_object_unref (layout);

  /* Draws the selection checkbox */
  if (item->mode == GTD_WINDOW_MODE_SELECTION)
    {
      gtk_style_context_add_class (context, GTK_STYLE_CLASS_CHECK);

      if (item->selected)
        gtk_style_context_set_state (context, GTK_STATE_FLAG_CHECKED);

      gtk_render_check (context,
                        cr,
                        THUMBNAIL_SIZE - CHECK_SIZE - padding.right - margin.right,
                        THUMBNAIL_SIZE - CHECK_SIZE - padding.bottom,
                        CHECK_SIZE,
                        CHECK_SIZE);
    }

  gdk_rgba_free (color);

  gtk_style_context_restore (context);
  cairo_destroy (cr);
  return surface;
}

static void
gtd_list_selector_grid_item__update_thumbnail (GtdListSelectorGridItem *item)
{
  cairo_surface_t *surface;

  surface = gtd_list_selector_grid_item__render_thumbnail (item);

  gtk_image_set_from_surface (GTK_IMAGE (item->icon_image), surface);

  cairo_surface_destroy (surface);
}

static void
color_changed (GtdListSelectorGridItem *self)
{
  GdkRGBA *color;
  gchar *color_str, *css;

  color = gtd_task_list_get_color (self->list);
  color_str = gdk_rgba_to_string (color);
  css = g_strdup_printf ("grid-item image { background-color: %s; }", color_str);

  gtk_css_provider_load_from_data (self->css_provider,
                                   css,
                                   -1,
                                   NULL);

  gtd_list_selector_grid_item__update_thumbnail (self);


  g_clear_pointer (&color_str, g_free);
  g_clear_pointer (&color, gdk_rgba_free);
  g_clear_pointer (&css, g_free);
}

static void
gtd_list_selector_grid_item__task_changed (GtdTaskList *list,
                                           GtdTask     *task,
                                           gpointer     user_data)
{
  if (!gtd_task_get_complete (task))
    gtd_list_selector_grid_item__update_thumbnail (GTD_LIST_SELECTOR_GRID_ITEM (user_data));
}

static void
gtd_list_selector_grid_item__notify_ready (GtdListSelectorGridItem *item,
                                  GParamSpec      *pspec,
                                  gpointer         user_data)
{
  gtd_list_selector_grid_item__update_thumbnail (item);
}

GtkWidget*
gtd_list_selector_grid_item_new (GtdTaskList *list)
{
  return g_object_new (GTD_TYPE_LIST_SELECTOR_GRID_ITEM,
                       "task-list", list,
                       NULL);
}

static gboolean
gtd_list_selector_grid_item__button_press_event_cb (GtkWidget *widget,
                                                    GdkEvent  *event,
                                                    gpointer   user_data)
{
  GtdListSelectorGridItem *item;
  GdkEventButton *button_ev;
  gboolean right_click;
  gboolean left_click_with_ctrl;

  item = GTD_LIST_SELECTOR_GRID_ITEM (user_data);
  button_ev = (GdkEventButton*) event;

  right_click = button_ev->button == 3;
  left_click_with_ctrl = button_ev->button == 1 && button_ev->state & GDK_CONTROL_MASK;

  if (right_click || left_click_with_ctrl)
    {
      if (item->mode == GTD_WINDOW_MODE_NORMAL)
        {
          g_object_set (user_data,
                        "mode", GTD_WINDOW_MODE_SELECTION,
                        "selected", TRUE,
                        NULL);
        }
      else
        {
          gtk_widget_activate (user_data);
        }

      return GDK_EVENT_STOP;
    }

  return GDK_EVENT_PROPAGATE;
}

static void
gtd_list_selector_grid_item_state_flags_changed (GtkWidget     *item,
                                         GtkStateFlags  flags)
{
  GtdListSelectorGridItem *self;

  self = GTD_LIST_SELECTOR_GRID_ITEM (item);

  if (GTK_WIDGET_CLASS (gtd_list_selector_grid_item_parent_class)->state_flags_changed)
    GTK_WIDGET_CLASS (gtd_list_selector_grid_item_parent_class)->state_flags_changed (item, flags);

  /*
   * The list might be NULL when the provider has been removed, in which
   * case :dispose() will be called before this function which will remove
   * the list reference from GtdListSelectorGridItem.
   */

  if (self->list)
    gtd_list_selector_grid_item__update_thumbnail (GTD_LIST_SELECTOR_GRID_ITEM (item));
}

static GtdTaskList*
gtd_list_selector_grid_item_get_list (GtdListSelectorItem *item)
{
  g_return_val_if_fail (GTD_IS_LIST_SELECTOR_GRID_ITEM (item), NULL);

  return GTD_LIST_SELECTOR_GRID_ITEM (item)->list;
}

static gboolean
gtd_list_selector_grid_item_get_selected (GtdListSelectorItem *item)
{
  g_return_val_if_fail (GTD_IS_LIST_SELECTOR_GRID_ITEM (item), FALSE);

  return GTD_LIST_SELECTOR_GRID_ITEM (item)->selected;
}

static void
gtd_list_selector_grid_item_set_selected (GtdListSelectorItem *item,
                                          gboolean             selected)
{
  GtdListSelectorGridItem *self;

  g_return_if_fail (GTD_IS_LIST_SELECTOR_GRID_ITEM (item));

  self = GTD_LIST_SELECTOR_GRID_ITEM (item);

  if (self->selected != selected)
    {
      self->selected = selected;

      gtd_list_selector_grid_item__update_thumbnail (self);

      g_object_notify (G_OBJECT (item), "selected");
    }
}

static void
gtd_list_selector_item_iface_init (GtdListSelectorItemInterface *iface)
{
  iface->get_list = gtd_list_selector_grid_item_get_list;
  iface->get_selected = gtd_list_selector_grid_item_get_selected;
  iface->set_selected = gtd_list_selector_grid_item_set_selected;
}

static void
gtd_list_selector_grid_item_finalize (GObject *object)
{
  GtdListSelectorGridItem *self = GTD_LIST_SELECTOR_GRID_ITEM (object);

  g_clear_object (&self->css_provider);

  G_OBJECT_CLASS (gtd_list_selector_grid_item_parent_class)->finalize (object);
}

static void
gtd_list_selector_grid_item_dispose (GObject *object)
{
  GtdListSelectorGridItem *self = GTD_LIST_SELECTOR_GRID_ITEM (object);

  if (self->list)
    {
      g_signal_handlers_disconnect_by_func (self->list,
                                            gtd_list_selector_grid_item__notify_ready,
                                            self);
      g_signal_handlers_disconnect_by_func (self->list,
                                            color_changed,
                                            self);
      g_signal_handlers_disconnect_by_func (self->list,
                                            gtd_list_selector_grid_item__task_changed,
                                            self);
      g_clear_object (&self->list);
    }


  G_OBJECT_CLASS (gtd_list_selector_grid_item_parent_class)->dispose (object);
}

static void
gtd_list_selector_grid_item_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  GtdListSelectorGridItem *self = GTD_LIST_SELECTOR_GRID_ITEM (object);

  switch (prop_id)
    {
    case PROP_MODE:
      g_value_set_enum (value, self->mode);
      break;

    case PROP_SELECTED:
      g_value_set_boolean (value, self->selected);
      break;

    case PROP_TASK_LIST:
      g_value_set_object (value, self->list);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_list_selector_grid_item_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  GtdListSelectorGridItem *self = GTD_LIST_SELECTOR_GRID_ITEM (object);

  switch (prop_id)
    {
    case PROP_MODE:
      self->mode = g_value_get_enum (value);
      gtd_list_selector_grid_item__update_thumbnail (self);
      g_object_notify (object, "mode");
      break;

    case PROP_SELECTED:
      gtd_list_selector_item_set_selected (GTD_LIST_SELECTOR_ITEM (self),
                                           g_value_get_boolean (value));
      break;

    case PROP_TASK_LIST:
      self->list = g_value_dup_object (value);
      g_object_bind_property (self->list,
                              "name",
                              self->title_label,
                              "label",
                              G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

      g_object_bind_property (gtd_task_list_get_provider (self->list),
                              "description",
                              self->subtitle_label,
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
                                "notify::ready",
                                G_CALLBACK (gtd_list_selector_grid_item__notify_ready),
                                self);
      g_signal_connect_swapped (self->list,
                                "notify::color",
                                G_CALLBACK (color_changed),
                                self);
      g_signal_connect (self->list,
                       "task-added",
                        G_CALLBACK (gtd_list_selector_grid_item__task_changed),
                        self);
      g_signal_connect (self->list,
                       "task-removed",
                        G_CALLBACK (gtd_list_selector_grid_item__task_changed),
                        self);
      g_signal_connect (self->list,
                       "task-updated",
                        G_CALLBACK (gtd_list_selector_grid_item__task_changed),
                        self);

      color_changed (self);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_list_selector_grid_item_class_init (GtdListSelectorGridItemClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gtd_list_selector_grid_item_finalize;
  object_class->dispose = gtd_list_selector_grid_item_dispose;
  object_class->get_property = gtd_list_selector_grid_item_get_property;
  object_class->set_property = gtd_list_selector_grid_item_set_property;

  widget_class->state_flags_changed = gtd_list_selector_grid_item_state_flags_changed;

  /**
   * GtdListSelectorGridItem::mode:
   *
   * The parent source of the list.
   */
  g_object_class_override_property (object_class,
                                    PROP_MODE,
                                    "mode");

  /**
   * GtdListSelectorGridItem::selected:
   *
   * Whether this item is selected when in %GTD_WINDOW_MODE_SELECTION.
   */
  g_object_class_override_property (object_class,
                                    PROP_SELECTED,
                                    "selected");

  /**
   * GtdListSelectorGridItem::list:
   *
   * The parent source of the list.
   */
  g_object_class_override_property (object_class,
                                    PROP_TASK_LIST,
                                    "task-list");

  /* template class */
  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/todo/ui/list-selector-grid-item.ui");

  gtk_widget_class_bind_template_child (widget_class, GtdListSelectorGridItem, icon_image);
  gtk_widget_class_bind_template_child (widget_class, GtdListSelectorGridItem, spinner);
  gtk_widget_class_bind_template_child (widget_class, GtdListSelectorGridItem, subtitle_label);
  gtk_widget_class_bind_template_child (widget_class, GtdListSelectorGridItem, title_label);

  gtk_widget_class_bind_template_callback (widget_class, gtd_list_selector_grid_item__button_press_event_cb);

  gtk_widget_class_set_css_name (widget_class, "grid-item");
}

static void
gtd_list_selector_grid_item_init (GtdListSelectorGridItem *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  /* CSS provider */
  self->css_provider = gtk_css_provider_new ();

  gtk_style_context_add_provider (gtk_widget_get_style_context (GTK_WIDGET (self->icon_image)),
                                  GTK_STYLE_PROVIDER (self->css_provider),
                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 2);
}
