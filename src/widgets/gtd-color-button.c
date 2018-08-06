/* gtd-color-button.c
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

#define G_LOG_DOMAIN "GtdColorButton"

#include "gtd-color-button.h"
#include "gtd-utils.h"

#define INTENSITY(r, g, b) ((r) * 0.30 + (g) * 0.59 + (b) * 0.11)

struct _GtdColorButton
{
  GtkWidget           parent;

  GdkRGBA             color;

  GtkWidget          *selected_icon;
};

G_DEFINE_TYPE (GtdColorButton, gtd_color_button, GTK_TYPE_WIDGET)

enum
{
  PROP_0,
  PROP_COLOR,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];


/*
 * GtkWidget overrides
 */

static void
gtd_color_button_measure (GtkWidget      *widget,
                          GtkOrientation  orientation,
                          gint            for_size,
                          gint           *minimum,
                          gint           *natural,
                          gint           *minimum_baseline,
                          gint           *natural_baseline)
{
  GtdColorButton *self = GTD_COLOR_BUTTON (widget);

  gtk_widget_measure (self->selected_icon,
                      orientation,
                      for_size,
                      minimum,
                      natural,
                      NULL,
                      NULL);
}

static void
gtd_color_button_size_allocate (GtkWidget           *widget,
                                const GtkAllocation *allocation,
                                gint                 baseline)
{
  GtdColorButton *self = GTD_COLOR_BUTTON (widget);

  gtk_widget_size_allocate (self->selected_icon, allocation, baseline);
}

static void
gtd_color_button_snapshot (GtkWidget   *widget,
                           GtkSnapshot *snapshot)
{
  GtdColorButton *self;
  gint height;
  gint width;

  self = GTD_COLOR_BUTTON (widget);
  width = gtk_widget_get_width (widget);
  height = gtk_widget_get_height (widget);

  /* XXX: This API doesn't exist yet:
   *
   * gtk_snapshot_push_content_clip (snapshot, 0, 0, width, height);
   */

  gtk_snapshot_append_color (snapshot,
                             &self->color,
                             &GRAPHENE_RECT_INIT (0, 0, width, height));

  gtk_widget_snapshot_child (widget, self->selected_icon, snapshot);
}

static void
gtd_color_button_state_flags_changed (GtkWidget     *widget,
                                      GtkStateFlags  previous_state)
{
  GtdColorButton *self;
  gboolean selected;

  self = GTD_COLOR_BUTTON (widget);
  selected = gtk_widget_get_state_flags (widget) & GTK_STATE_FLAG_SELECTED;

  if (selected)
    gtk_image_set_from_icon_name (GTK_IMAGE (self->selected_icon), "object-select-symbolic");
  else
    gtk_image_clear (GTK_IMAGE (self->selected_icon));

  GTK_WIDGET_CLASS (gtd_color_button_parent_class)->state_flags_changed (widget, previous_state);
}


/*
 * GObject overrides
 */

static void
gtd_color_button_finalize (GObject *object)
{
  GtdColorButton *self = (GtdColorButton *)object;

  g_clear_pointer (&self->selected_icon, gtk_widget_unparent);

  G_OBJECT_CLASS (gtd_color_button_parent_class)->finalize (object);
}

static void
gtd_color_button_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  GtdColorButton *self = GTD_COLOR_BUTTON (object);

  switch (prop_id)
    {
    case PROP_COLOR:
      g_value_set_boxed (value, &self->color);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_color_button_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  GtdColorButton *self = GTD_COLOR_BUTTON (object);

  switch (prop_id)
    {
    case PROP_COLOR:
      gtd_color_button_set_color (self, g_value_get_boxed (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_color_button_class_init (GtdColorButtonClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gtd_color_button_finalize;
  object_class->get_property = gtd_color_button_get_property;
  object_class->set_property = gtd_color_button_set_property;

  widget_class->measure = gtd_color_button_measure;
  widget_class->size_allocate = gtd_color_button_size_allocate;
  widget_class->snapshot = gtd_color_button_snapshot;
  widget_class->state_flags_changed = gtd_color_button_state_flags_changed;

  properties[PROP_COLOR] = g_param_spec_boxed ("color",
                                               "Color",
                                               "Color of the button",
                                               GDK_TYPE_RGBA,
                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_css_name (widget_class, "colorbutton");
}

static void
gtd_color_button_init (GtdColorButton *self)
{
  self->selected_icon = gtk_image_new ();
  gtk_widget_set_parent (self->selected_icon, GTK_WIDGET (self));

  gtk_widget_set_can_focus (GTK_WIDGET (self), TRUE);
  gtk_widget_set_has_surface (GTK_WIDGET (self), FALSE);
}

GtkWidget*
gtd_color_button_new (const GdkRGBA *color)
{
  return g_object_new (GTD_TYPE_COLOR_BUTTON,
                       "color", color,
                       NULL);
}

const GdkRGBA*
gtd_color_button_get_color (GtdColorButton *self)
{
  g_return_val_if_fail (GTD_IS_COLOR_BUTTON (self), NULL);

  return &self->color;
}

void
gtd_color_button_set_color (GtdColorButton *self,
                            const GdkRGBA  *color)
{
  GtkStyleContext *context;

  g_return_if_fail (GTD_IS_COLOR_BUTTON (self));

  if (gdk_rgba_equal (&self->color, color))
    return;

  self->color = *color;
  self->color.alpha = 1.0;

  context = gtk_widget_get_style_context (GTK_WIDGET (self));

  if (INTENSITY (self->color.red, self->color.green, self->color.blue) > 0.5)
    {
      gtk_style_context_add_class (context, "light");
      gtk_style_context_remove_class (context, "dark");
    }
  else
    {
      gtk_style_context_add_class (context, "dark");
      gtk_style_context_remove_class (context, "light");
    }

  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLOR]);
}

