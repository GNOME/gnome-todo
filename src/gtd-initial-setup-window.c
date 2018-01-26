/* gtd-initial-setup-window.c
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

#define G_LOG_DOMAIN "GtdInitialSetupWindow"

#include "interfaces/gtd-provider.h"
#include "gtd-application.h"
#include "gtd-provider-selector.h"
#include "gtd-initial-setup-window.h"
#include "gtd-manager.h"

#include <glib/gi18n.h>

typedef struct
{
  GtkWidget                 *cancel_button;
  GtkWidget                 *done_button;
  GtkWidget                 *storage_selector;

  GtdManager                *manager;
} GtdInitialSetupWindowPrivate;

struct _GtdInitialSetupWindow
{
  GtkApplicationWindow          parent;

  /*<private>*/
  GtdInitialSetupWindowPrivate *priv;
};

G_DEFINE_TYPE_WITH_PRIVATE (GtdInitialSetupWindow, gtd_initial_setup_window, GTK_TYPE_APPLICATION_WINDOW)

enum {
  PROP_0,
  PROP_MANAGER,
  LAST_PROP
};

enum {
  CANCEL,
  DONE,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

static void
gtd_initial_setup_window__location_selected (GtdInitialSetupWindow *window,
                                             GtdProvider           *provider)
{
  GtdInitialSetupWindowPrivate *priv;

  g_return_if_fail (GTD_IS_INITIAL_SETUP_WINDOW (window));

  priv = window->priv;

  gtk_widget_set_sensitive (priv->done_button, provider != NULL);

  if (provider)
    gtd_manager_set_default_provider (priv->manager, provider);
}

static void
gtd_initial_setup_window__button_clicked (GtdInitialSetupWindow *window,
                                          GtkWidget             *button)
{
  GtdInitialSetupWindowPrivate *priv;

  g_return_if_fail (GTD_IS_INITIAL_SETUP_WINDOW (window));

  priv = window->priv;

  if (button == priv->cancel_button)
    {
      g_signal_emit (window,
                     signals[CANCEL],
                     0);
    }
  else if (button == priv->done_button)
    {
      g_signal_emit (window,
                     signals[DONE],
                     0);
    }
}

static void
gtd_initial_setup_window_finalize (GObject *object)
{
  G_OBJECT_CLASS (gtd_initial_setup_window_parent_class)->finalize (object);
}

static void
gtd_initial_setup_window_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  GtdInitialSetupWindow *self = GTD_INITIAL_SETUP_WINDOW (object);

  switch (prop_id)
    {
    case PROP_MANAGER:
      g_value_set_object (value, self->priv->manager);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_initial_setup_window_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
  GtdInitialSetupWindow *self = GTD_INITIAL_SETUP_WINDOW (object);

  switch (prop_id)
    {
    case PROP_MANAGER:
      self->priv->manager = g_value_get_object (value);
      g_object_notify (object, "manager");
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_initial_setup_window_constructed (GObject *object)
{
  GtdInitialSetupWindowPrivate *priv;

  G_OBJECT_CLASS (gtd_initial_setup_window_parent_class)->constructed (object);

  priv = GTD_INITIAL_SETUP_WINDOW (object)->priv;

  g_object_bind_property (object,
                          "manager",
                          priv->storage_selector,
                          "manager",
                          G_BINDING_DEFAULT);
}

static void
gtd_initial_setup_window_class_init (GtdInitialSetupWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gtd_initial_setup_window_finalize;
  object_class->constructed = gtd_initial_setup_window_constructed;
  object_class->get_property = gtd_initial_setup_window_get_property;
  object_class->set_property = gtd_initial_setup_window_set_property;

  /**
   * GtdInitialSetupWindow::cancel:
   *
   * Emitted when the Cancel button is clicked. Should quit the
   * application.
   */
  signals[CANCEL] = g_signal_new ("cancel",
                                  GTD_TYPE_INITIAL_SETUP_WINDOW,
                                  G_SIGNAL_RUN_LAST,
                                  0,
                                  NULL,
                                  NULL,
                                  NULL,
                                  G_TYPE_NONE,
                                  0);

  /**
   * GtdInitialSetupWindow::done:
   *
   * Emitted when the Done button is clicked. Should start the
   * application.
   */
  signals[DONE] = g_signal_new ("done",
                                GTD_TYPE_INITIAL_SETUP_WINDOW,
                                G_SIGNAL_RUN_LAST,
                                0,
                                NULL,
                                NULL,
                                NULL,
                                G_TYPE_NONE,
                                0);

  /**
   * GtdInitialSetupWindow::manager:
   *
   * Manager of the application.
   */
  g_object_class_install_property (
        object_class,
        PROP_MANAGER,
        g_param_spec_object ("manager",
                             "Manager of the task",
                             "The singleton manager instance of the task",
                             GTD_TYPE_MANAGER,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/todo/ui/initial-setup.ui");

  gtk_widget_class_bind_template_child_private (widget_class, GtdInitialSetupWindow, cancel_button);
  gtk_widget_class_bind_template_child_private (widget_class, GtdInitialSetupWindow, done_button);
  gtk_widget_class_bind_template_child_private (widget_class, GtdInitialSetupWindow, storage_selector);

  gtk_widget_class_bind_template_callback (widget_class, gtd_initial_setup_window__button_clicked);
  gtk_widget_class_bind_template_callback (widget_class, gtd_initial_setup_window__location_selected);
}

static void
gtd_initial_setup_window_init (GtdInitialSetupWindow *self)
{
  self->priv = gtd_initial_setup_window_get_instance_private (self);

  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget*
gtd_initial_setup_window_new (GtdApplication *application)
{
  g_return_val_if_fail (GTD_IS_APPLICATION (application), NULL);

  return g_object_new (GTD_TYPE_INITIAL_SETUP_WINDOW,
                       "application", application,
                       "manager", gtd_manager_get_default (),
                       NULL);
}
