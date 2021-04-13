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

  GtkWidget          *add_button;
  GtkWidget          *subtitle_label;
  GtkWidget          *title_label;

  gboolean            is_empty : 1;
};

G_DEFINE_TYPE (GtdEmptyListWidget, gtd_empty_list_widget, GTK_TYPE_BOX)

enum
{
  ADD_TASKS,
  N_SIGNALS,
};

static guint signals[N_SIGNALS] = { 0, };


/* Icons that will be randomly picked */
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
  const gchar *title_text, *subtitle_text, *button_text;

  if (self->is_empty)
    {
      title_text = _("Tasks Will Appear Here");
      subtitle_text = "";
      button_text = _("Add Tasks…");
    }
  else
    {
      gint message_index, subtitle_index;

      message_index = g_random_int_range (0, G_N_ELEMENTS (messages));
      subtitle_index = g_random_int_range (0, G_N_ELEMENTS (subtitles));

      title_text = gettext (messages[message_index]);
      subtitle_text = gettext (subtitles[subtitle_index]);
      button_text = _("Add More Tasks…");
    }

  gtk_label_set_markup (GTK_LABEL (self->title_label), title_text);
  gtk_label_set_markup (GTK_LABEL (self->subtitle_label), subtitle_text);
  gtk_button_set_label (GTK_BUTTON (self->add_button), button_text);

  gtk_widget_set_visible (self->subtitle_label, !self->is_empty);
}


/*
 * Callbacks
 */

static void
on_add_button_clicked_cb (GtkButton          *button,
                          GtdEmptyListWidget *self)
{
  g_signal_emit (self, signals[ADD_TASKS], 0);
}


/*
 * GObject overrides
 */

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

  signals[ADD_TASKS] = g_signal_new ("add-tasks",
                                     GTD_TYPE_EMPTY_LIST_WIDGET,
                                     G_SIGNAL_RUN_LAST,
                                     0, NULL, NULL, NULL,
                                     G_TYPE_NONE,
                                     0);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/todo/ui/gtd-empty-list-widget.ui");

  gtk_widget_class_bind_template_child (widget_class, GtdEmptyListWidget, add_button);
  gtk_widget_class_bind_template_child (widget_class, GtdEmptyListWidget, subtitle_label);
  gtk_widget_class_bind_template_child (widget_class, GtdEmptyListWidget, title_label);

  gtk_widget_class_bind_template_callback (widget_class, on_add_button_clicked_cb);

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
