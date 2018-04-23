/* gtd-expandable-entry.c
 *
 * Copyright (C) 2017 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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

#define G_LOG_DOMAIN "GtdExpandableEntry"

#include "gtd-expandable-entry.h"

struct _GtdExpandableEntry
{
  GtkEntry            parent;

  gboolean            propagate_natural_width;
};

G_DEFINE_TYPE (GtdExpandableEntry, gtd_expandable_entry, GTK_TYPE_ENTRY)

enum
{
  PROP_0,
  PROP_PROPAGATE_NATURAL_WIDTH,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];


/*
 * GtkWidget overrides
 */

static gboolean
gtd_expandable_entry_event (GtkWidget *widget,
                            GdkEvent  *event)
{
  if (gdk_event_get_event_type (event) == GDK_ENTER_NOTIFY)
    {
      gtk_widget_set_state_flags (widget, GTK_STATE_FLAG_PRELIGHT, FALSE);
      gtk_widget_queue_draw (widget);
    }
  else if (gdk_event_get_event_type (event) == GDK_LEAVE_NOTIFY)
    {
      gtk_widget_unset_state_flags (widget, GTK_STATE_FLAG_PRELIGHT);
      gtk_widget_queue_draw (widget);
    }

  return GDK_EVENT_PROPAGATE;
}

static void
gtd_expandable_entry_measure (GtkWidget      *widget,
                              GtkOrientation  orientation,
                              gint            for_size,
                              gint           *minimum,
                              gint           *natural,
                              gint           *minimum_baseline,
                              gint           *natural_baseline)
{
  GtdExpandableEntry *self;
  GtkStyleContext *context;
  PangoLayout *layout;
  GtkBorder padding;
  GtkBorder border;
  GtkBorder margin;
  gint local_minimum;
  gint local_natural;
  gint text_width;

  self = GTD_EXPANDABLE_ENTRY (widget);

  /* Just chain up when calculating the vertical size */
  if (orientation == GTK_ORIENTATION_VERTICAL)
    {
      GTK_WIDGET_CLASS (gtd_expandable_entry_parent_class)->measure (widget,
                                                                     orientation,
                                                                     for_size,
                                                                     minimum,
                                                                     natural,
                                                                     minimum_baseline,
                                                                     natural_baseline);
      return;
    }

  context = gtk_widget_get_style_context (widget);
  gtk_style_context_get_border (context, &border);
  gtk_style_context_get_margin (context, &margin);
  gtk_style_context_get_padding (context, &padding);

  /* Query the text width */
  layout = gtk_entry_get_layout (GTK_ENTRY (widget));
  pango_layout_get_pixel_size (layout, &text_width, NULL);

  GTK_WIDGET_CLASS (gtd_expandable_entry_parent_class)->measure (widget,
                                                                 orientation,
                                                                 for_size,
                                                                 &local_minimum,
                                                                 &local_natural,
                                                                 NULL, NULL);

  if (self->propagate_natural_width)
    {
      local_natural = MAX (local_minimum, text_width);
      local_natural += margin.left + margin.right;
      local_natural += border.left + border.right;
      local_natural += padding.left + padding.right;
    }

  if (minimum)
    *minimum = local_minimum;

  if (natural)
    *natural = local_natural;
}


/*
 * GObject overrides
 */

static void
gtd_expandable_entry_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  GtdExpandableEntry *self = GTD_EXPANDABLE_ENTRY (object);

  switch (prop_id)
    {
    case PROP_PROPAGATE_NATURAL_WIDTH:
      g_value_set_boolean (value, self->propagate_natural_width);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_expandable_entry_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  GtdExpandableEntry *self = GTD_EXPANDABLE_ENTRY (object);

  switch (prop_id)
    {
    case PROP_PROPAGATE_NATURAL_WIDTH:
      gtd_expandable_entry_set_propagate_natural_width (self, g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_expandable_entry_class_init (GtdExpandableEntryClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->get_property = gtd_expandable_entry_get_property;
  object_class->set_property = gtd_expandable_entry_set_property;

  widget_class->event = gtd_expandable_entry_event;
  widget_class->measure = gtd_expandable_entry_measure;

  properties[PROP_PROPAGATE_NATURAL_WIDTH] = g_param_spec_boolean ("propagate-natural-width",
                                                                   "Propagate natural width",
                                                                   "Propagate natural width",
                                                                   FALSE,
                                                                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
gtd_expandable_entry_init (GtdExpandableEntry *self)
{
  self->propagate_natural_width = FALSE;

  g_signal_connect_object (self,
                           "notify::text",
                           G_CALLBACK (gtk_widget_queue_resize),
                           self,
                           G_CONNECT_SWAPPED);
}

GtdExpandableEntry *
gtd_expandable_entry_new (void)
{
  return g_object_new (GTD_TYPE_EXPANDABLE_ENTRY, NULL);
}

gboolean
gtd_expandable_entry_get_propagate_natural_width (GtdExpandableEntry *self)
{
  g_return_val_if_fail (GTD_IS_EXPANDABLE_ENTRY (self), FALSE);

  return self->propagate_natural_width;
}

void
gtd_expandable_entry_set_propagate_natural_width (GtdExpandableEntry *self,
                                                  gboolean            propagate_natural_width)
{
  g_return_if_fail (GTD_IS_EXPANDABLE_ENTRY (self));

  if (self->propagate_natural_width == propagate_natural_width)
    return;

  self->propagate_natural_width = propagate_natural_width;

  gtk_widget_queue_resize (GTK_WIDGET (self));
}
