/* gtd-empty-list-widget.c
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

#define G_LOG_DOMAIN "GtdEmptyListWidget"

#include "gtd-empty-list-widget.h"

#include <glib/gi18n.h>

struct _GtdEmptyListWidget
{
  GtkBox              parent;

  GtkWidget          *icon;
  GtkWidget          *subtitle_label;
  GtkWidget          *title_label;

  gboolean            is_empty : 1;
};

G_DEFINE_TYPE (GtdEmptyListWidget, gtd_empty_list_widget, GTK_TYPE_BOX)

/* Icons that will be randomly picked */
const gchar *icons[] =
{
  "face-cool-symbolic",
  "face-laugh-symbolic",
  "face-smirk-symbolic",
  "face-smile-symbolic",
  "face-smile-big-symbolic",
  "face-wink-symbolic"
};

const gchar *messages[] =
{
  N_("No more tasks left"),
  N_("Nothing else to do here"),
  N_("You made it!"),
  N_("Looks like there’s nothing else left here")
};

const gchar *subtitles[] =
{
  N_("Get some rest now"),
  N_("Enjoy the rest of your day"),
  N_("Good job!"),
  N_("Meanwhile, spread the love"),
  N_("Working hard is always rewarded")
};

static void
update_message (GtdEmptyListWidget *self)
{
  const gchar *icon_name, *title_text, *subtitle_text;

  if (self->is_empty)
    {
      icon_name = "checkbox-checked-symbolic";
      title_text = _("No tasks found");
      subtitle_text = _("You can add tasks using the <b>+</b> above");
    }
  else
    {
      gint icon_index, message_index, subtitle_index;

      icon_index = g_random_int_range (0, G_N_ELEMENTS (icons));
      message_index = g_random_int_range (0, G_N_ELEMENTS (messages));
      subtitle_index = g_random_int_range (0, G_N_ELEMENTS (subtitles));

      icon_name = icons[icon_index];
      title_text = gettext (messages[message_index]);
      subtitle_text = gettext (subtitles[subtitle_index]);
    }

  gtk_image_set_from_icon_name (GTK_IMAGE (self->icon),
                                icon_name,
                                -1);

  gtk_label_set_markup (GTK_LABEL (self->title_label), title_text);
  gtk_label_set_markup (GTK_LABEL (self->subtitle_label), subtitle_text);
}

static void
gtd_empty_list_widget_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
gtd_empty_list_widget_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
gtd_empty_list_widget_class_init (GtdEmptyListWidgetClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->get_property = gtd_empty_list_widget_get_property;
  object_class->set_property = gtd_empty_list_widget_set_property;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/todo/ui/empty-list.ui");

  gtk_widget_class_bind_template_child (widget_class, GtdEmptyListWidget, icon);
  gtk_widget_class_bind_template_child (widget_class, GtdEmptyListWidget, subtitle_label);
  gtk_widget_class_bind_template_child (widget_class, GtdEmptyListWidget, title_label);

  gtk_widget_class_set_css_name (widget_class, "emptylistwidget");
}

static void
gtd_empty_list_widget_init (GtdEmptyListWidget *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

void
gtd_empty_list_widget_set_is_empty (GtdEmptyListWidget *self,
                                    gboolean            is_empty)
{
  g_return_if_fail (GTD_IS_EMPTY_LIST_WIDGET (self));

  self->is_empty = is_empty;
  update_message (self);
}
