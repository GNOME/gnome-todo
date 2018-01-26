/* gtd-window.c
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

#define G_LOG_DOMAIN "GtdWindow"

#include "interfaces/gtd-activatable.h"
#include "interfaces/gtd-provider.h"
#include "interfaces/gtd-panel.h"
#include "views/gtd-list-selector-panel.h"
#include "gtd-application.h"
#include "gtd-enum-types.h"
#include "gtd-task-list-view.h"
#include "gtd-manager.h"
#include "gtd-manager-protected.h"
#include "gtd-notification.h"
#include "gtd-notification-widget.h"
#include "gtd-plugin-manager.h"
#include "gtd-task.h"
#include "gtd-task-list.h"
#include "gtd-window.h"

#include <glib/gi18n.h>

/**
 * SECTION:gtd-window
 * @short_description:main window
 * @title:GtdWindow
 * @stability:Unstable
 * @see_also:#GtdNotification
 *
 * The #GtdWindow is the main application window of GNOME To Do. Objects should
 * use this class to send notifications (see gtd_window_notify()), cancel notifications
 * (see gtd_window_cancel_notification()), change between selection and normal mode
 * and fine-tune the headerbar.
 */

typedef struct
{
  GtkWidget                     *cancel_selection_button;
  GtkWidget                     *gear_menu_button;
  GtkHeaderBar                  *headerbar;
  GtdNotificationWidget         *notification_widget;
  GtkStack                      *stack;
  GtkStackSwitcher              *stack_switcher;

  /* boxes */
  GtkWidget                     *extension_box_end;
  GtkWidget                     *extension_box_start;
  GtkWidget                     *panel_box_end;
  GtkWidget                     *panel_box_start;

  GtdPanel                      *active_panel;

  /* mode */
  GtdWindowMode                  mode;

  /* loading notification */
  GtdNotification               *loading_notification;

  guint                          save_geometry_timeout_id;
  GtdManager                    *manager;
} GtdWindowPrivate;

struct _GtdWindow
{
  GtkApplicationWindow  application;

  /*< private >*/
  GtdWindowPrivate     *priv;
};

#define              SAVE_GEOMETRY_ID_TIMEOUT                    100 /* ms */

G_DEFINE_TYPE_WITH_PRIVATE (GtdWindow, gtd_window, GTK_TYPE_APPLICATION_WINDOW)

enum {
  PROP_0,
  PROP_MANAGER,
  PROP_MODE,
  LAST_PROP
};

/* GtdManager's error notifications */
typedef struct
{
  GtdWindow *window;
  gchar     *primary_text;
  gchar     *secondary_text;
} ErrorData;

static void
add_widgets (GtdWindow *window,
             GtkWidget *container_start,
             GtkWidget *container_end,
             GList     *widgets)
{
  GtdWindowPrivate *priv = gtd_window_get_instance_private (window);
  GList *l;

  for (l = widgets; l != NULL; l = l->next)
    {
      switch (gtk_widget_get_halign (l->data))
        {
        case GTK_ALIGN_START:
          gtk_box_pack_start (GTK_BOX (container_start),
                              l->data,
                              FALSE,
                              FALSE,
                              0);
          break;

        case GTK_ALIGN_CENTER:
          gtk_header_bar_set_custom_title (priv->headerbar, l->data);
          break;

        case GTK_ALIGN_END:
          gtk_box_pack_end (GTK_BOX (container_end),
                            l->data,
                            FALSE,
                            FALSE,
                            0);
          break;

        case GTK_ALIGN_BASELINE:
        case GTK_ALIGN_FILL:
        default:
          gtk_box_pack_start (GTK_BOX (container_start),
                              l->data,
                              FALSE,
                              FALSE,
                              0);
          break;
        }
    }
}

static void
remove_widgets (GtdWindow *window,
                GtkWidget *container_start,
                GtkWidget *container_end,
                GList     *widgets)
{
  GtdWindowPrivate *priv = gtd_window_get_instance_private (window);
  GList *l;

  for (l = widgets; l != NULL; l = l->next)
    {
      GtkWidget *container;

      if (gtk_widget_get_halign (l->data) == GTK_ALIGN_END)
        container = container_end;
      else if (gtk_widget_get_halign (l->data) == GTK_ALIGN_CENTER)
        container = GTK_WIDGET (priv->headerbar);
      else
        container = container_start;

      g_object_ref (l->data);
      gtk_container_remove (GTK_CONTAINER (container), l->data);
    }
}

static void
plugin_loaded (GtdWindow      *window,
               gpointer        unused_field,
               GtdActivatable *activatable)
{
  GtdWindowPrivate *priv;
  GList *header_widgets;

  priv = gtd_window_get_instance_private (window);
  header_widgets = gtd_activatable_get_header_widgets (activatable);

  add_widgets (window,
               priv->extension_box_start,
               priv->extension_box_end,
               header_widgets);

  g_list_free (header_widgets);
}

static void
plugin_unloaded (GtdWindow      *window,
                 gpointer        unused_field,
                 GtdActivatable *activatable)
{
  GtdWindowPrivate *priv;
  GList *header_widgets;

  priv = gtd_window_get_instance_private (window);
  header_widgets = gtd_activatable_get_header_widgets (activatable);

  remove_widgets (window,
                  priv->extension_box_start,
                  priv->extension_box_end,
                  header_widgets);
}

static void
update_panel_menu (GtdWindow *window)
{
  GtdWindowPrivate *priv = gtd_window_get_instance_private (window);
  const GMenu *menu;

  menu = gtd_panel_get_menu (priv->active_panel);

  gtk_menu_button_set_menu_model (GTK_MENU_BUTTON (priv->gear_menu_button), G_MENU_MODEL (menu));
}

static void
gtd_window__panel_menu_changed (GObject    *object,
                                GParamSpec *pspec,
                                GtdWindow  *window)
{
  GtdWindowPrivate *priv = gtd_window_get_instance_private (window);

  if (GTD_PANEL (object) != priv->active_panel)
    return;

  update_panel_menu (window);
}

static void
gtd_window__panel_title_changed (GObject    *object,
                                 GParamSpec *pspec,
                                 GtdWindow  *window)
{
  GtdWindowPrivate *priv = gtd_window_get_instance_private (window);

  gtk_container_child_set (GTK_CONTAINER (priv->stack),
                           GTK_WIDGET (object),
                           "title", gtd_panel_get_panel_title (GTD_PANEL (object)),
                           NULL);
}

static void
gtd_window__panel_added (GtdManager *manager,
                         GtdPanel   *panel,
                         GtdWindow  *window)
{
  GtdWindowPrivate *priv = gtd_window_get_instance_private (window);

  gtk_stack_add_titled (priv->stack,
                        GTK_WIDGET (panel),
                        gtd_panel_get_panel_name (panel),
                        gtd_panel_get_panel_title (panel));

  g_signal_connect (panel,
                    "notify::title",
                    G_CALLBACK (gtd_window__panel_title_changed),
                    window);
}

static void
gtd_window__panel_removed (GtdManager *manager,
                           GtdPanel   *panel,
                           GtdWindow  *window)
{
  GtdWindowPrivate *priv = gtd_window_get_instance_private (window);

  gtk_container_remove (GTK_CONTAINER (priv->stack), GTK_WIDGET (panel));
}

static void
error_data_free (ErrorData *error_data)
{
  g_free (error_data->primary_text);
  g_free (error_data->secondary_text);
  g_free (error_data);
}

static void
error_message_notification_primary_action (GtdNotification *notification,
                                           gpointer         user_data)
{
  error_data_free (user_data);
}

static void
error_message_notification_secondary_action (GtdNotification *notification,
                                             gpointer         user_data)
{
  GtkWidget *message_dialog;
  ErrorData *data;

  data = user_data;
  message_dialog = gtk_message_dialog_new (GTK_WINDOW (data->window),
                                           GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                           GTK_MESSAGE_WARNING,
                                           GTK_BUTTONS_CLOSE,
                                           "%s",
                                           data->primary_text);

  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (message_dialog),
                                            "%s",
                                            data->secondary_text);

  g_signal_connect (message_dialog,
                    "response",
                    G_CALLBACK (gtk_widget_destroy),
                    NULL);

  gtk_widget_show (message_dialog);

  error_data_free (data);
}

static void
gtd_window__load_geometry (GtdWindow *window)
{
  GSettings *settings;
  GVariant *variant;
  gboolean maximized;
  const gint32 *position;
  const gint32 *size;
  gsize n_elements;

  settings = gtd_manager_get_settings (window->priv->manager);

  /* load window settings: size */
  variant = g_settings_get_value (settings,
                                  "window-size");
  size = g_variant_get_fixed_array (variant,
                                    &n_elements,
                                    sizeof (gint32));
  if (n_elements == 2)
    gtk_window_set_default_size (GTK_WINDOW (window),
                                 size[0],
                                 size[1]);
  g_variant_unref (variant);

  /* load window settings: position */
  variant = g_settings_get_value (settings,
                                  "window-position");
  position = g_variant_get_fixed_array (variant,
                                        &n_elements,
                                        sizeof (gint32));
  if (n_elements == 2)
    gtk_window_move (GTK_WINDOW (window),
                     position[0],
                     position[1]);

  g_variant_unref (variant);

  /* load window settings: state */
  maximized = g_settings_get_boolean (settings,
                                      "window-maximized");
  if (maximized)
    gtk_window_maximize (GTK_WINDOW (window));
}

static gboolean
gtd_window__save_geometry (gpointer user_data)
{
  GtdWindowPrivate *priv;
  GdkWindowState state;
  GdkWindow *window;
  GtkWindow *self;
  GSettings *settings;
  gboolean maximized;
  GVariant *variant;
  gint32 size[2];
  gint32 position[2];

  self = GTK_WINDOW (user_data);

  window = gtk_widget_get_window (GTK_WIDGET (self));
  state = gdk_window_get_state (window);
  priv = GTD_WINDOW (self)->priv;

  settings = gtd_manager_get_settings (priv->manager);

  /* save window's state */
  maximized = state & GDK_WINDOW_STATE_MAXIMIZED;
  g_settings_set_boolean (settings,
                          "window-maximized",
                          maximized);

  if (maximized)
    {
      priv->save_geometry_timeout_id = 0;
      return FALSE;
    }

  /* save window's size */
  gtk_window_get_size (self,
                       (gint *) &size[0],
                       (gint *) &size[1]);
  variant = g_variant_new_fixed_array (G_VARIANT_TYPE_INT32,
                                       size,
                                       2,
                                       sizeof (size[0]));
  g_settings_set_value (settings,
                        "window-size",
                        variant);

  /* save windows's position */
  gtk_window_get_position (self,
                           (gint *) &position[0],
                           (gint *) &position[1]);
  variant = g_variant_new_fixed_array (G_VARIANT_TYPE_INT32,
                                       position,
                                       2,
                                       sizeof (position[0]));
  g_settings_set_value (settings,
                        "window-position",
                        variant);

  priv->save_geometry_timeout_id = 0;

  return FALSE;
}

static void
gtd_window__cancel_selection_button_clicked (GtkWidget *button,
                                             GtdWindow *window)
{
  gtd_window_set_mode (window, GTD_WINDOW_MODE_NORMAL);
}

static void
gtd_window__stack_visible_child_cb (GtdWindow  *window,
                                    GParamSpec *pspec,
                                    GtkStack   *stack)
{
  GtdWindowPrivate *priv;
  GtkWidget *visible_child;
  GtdPanel *panel;
  GList *header_widgets;

  priv = gtd_window_get_instance_private (window);
  visible_child = gtk_stack_get_visible_child (stack);
  panel = GTD_PANEL (visible_child);

  /* Remove previous panel's widgets */
  if (priv->active_panel)
    {
      header_widgets = gtd_panel_get_header_widgets (priv->active_panel);

      /* Disconnect signals */
      g_signal_handlers_disconnect_by_func (priv->active_panel,
                                            gtd_window__panel_menu_changed,
                                            window);

      remove_widgets (window,
                      priv->panel_box_start,
                      priv->panel_box_end,
                      header_widgets);

      g_list_free (header_widgets);
    }

  /* Add current panel's header widgets */
  header_widgets = gtd_panel_get_header_widgets (panel);

  add_widgets (window,
               priv->panel_box_start,
               priv->panel_box_end,
               header_widgets);

  g_list_free (header_widgets);

  g_signal_connect (panel,
                    "notify::menu",
                    G_CALLBACK (gtd_window__panel_menu_changed),
                    window);

  /* Set panel as the new active panel */
  g_set_object (&priv->active_panel, panel);

  /* Setup the panel's menu */
  update_panel_menu (window);
}

/*
static void
gtd_window__create_new_list (GSimpleAction *simple,
                             GVariant      *parameter,
                             gpointer       user_data)
{
  GtdWindowPrivate *priv;

  g_return_if_fail (GTD_IS_WINDOW (user_data));

  priv = GTD_WINDOW (user_data)->priv;

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->new_list_button), TRUE);
}
 */

static void
gtd_window__manager_ready_changed (GObject    *source,
                                   GParamSpec *spec,
                                   gpointer    user_data)
{
  GtdWindowPrivate *priv = GTD_WINDOW (user_data)->priv;

  g_return_if_fail (GTD_IS_WINDOW (user_data));

  if (gtd_object_get_ready (GTD_OBJECT (source)))
    {
      gtd_notification_widget_cancel (priv->notification_widget, priv->loading_notification);
    }
  else
    {
      gtd_notification_widget_notify (priv->notification_widget, priv->loading_notification);
    }
}

static void
gtd_window__show_error_message (GtdManager                *manager,
                                const gchar               *primary_text,
                                const gchar               *secondary_text,
                                GtdNotificationActionFunc  function,
                                gpointer                   user_data,
                                GtdWindow                 *window)
{
  GtdNotification *notification;
  ErrorData *error_data;

  error_data = g_new0 (ErrorData, 1);
  notification = gtd_notification_new (primary_text, 7500);

  error_data->window = window;
  error_data->primary_text = g_strdup (primary_text);
  error_data->secondary_text = g_strdup (secondary_text);

  gtd_notification_set_primary_action (notification,
                                       error_message_notification_primary_action,
                                       error_data);

  if (function == NULL)
    gtd_notification_set_secondary_action (notification,
                                           _("Details"),
                                           error_message_notification_secondary_action,
                                           error_data);
  else
    gtd_notification_set_secondary_action (notification,
                                           secondary_text,
                                           function,
                                           user_data);


  gtd_window_notify (window, notification);
}

static gboolean
gtd_window_configure_event (GtkWidget         *widget,
                            GdkEventConfigure *event)
{
  GtdWindowPrivate *priv;
  GtdWindow *window;
  gboolean retval;

  window = GTD_WINDOW (widget);
  priv = window->priv;

  if (priv->save_geometry_timeout_id != 0)
    {
      g_source_remove (priv->save_geometry_timeout_id);
      priv->save_geometry_timeout_id = 0;
    }

  priv->save_geometry_timeout_id = g_timeout_add (SAVE_GEOMETRY_ID_TIMEOUT,
                                                  gtd_window__save_geometry,
                                                  window);

  retval = GTK_WIDGET_CLASS (gtd_window_parent_class)->configure_event (widget, event);

  return retval;
}

static gboolean
gtd_window_state_event (GtkWidget           *widget,
                        GdkEventWindowState *event)
{
  GtdWindowPrivate *priv;
  GtdWindow *window;
  gboolean retval;

  window = GTD_WINDOW (widget);
  priv = window->priv;

  if (priv->save_geometry_timeout_id != 0)
    {
      g_source_remove (priv->save_geometry_timeout_id);
      priv->save_geometry_timeout_id = 0;
    }

  priv->save_geometry_timeout_id = g_timeout_add (SAVE_GEOMETRY_ID_TIMEOUT,
                                                  gtd_window__save_geometry,
                                                  window);

  retval = GTK_WIDGET_CLASS (gtd_window_parent_class)->window_state_event (widget, event);

  return retval;
}

static void
gtd_window_constructed (GObject *object)
{
  GtdWindowPrivate *priv = GTD_WINDOW (object)->priv;
  GtkApplication *app;
  GMenu *menu;

  G_OBJECT_CLASS (gtd_window_parent_class)->constructed (object);

  /* load stored size */
  gtd_window__load_geometry (GTD_WINDOW (object));

  /* gear menu */
  app = GTK_APPLICATION (g_application_get_default ());
  menu = gtk_application_get_menu_by_id (app, "gear-menu");
  gtk_menu_button_set_menu_model (GTK_MENU_BUTTON (priv->gear_menu_button), G_MENU_MODEL (menu));
}

GtkWidget*
gtd_window_new (GtdApplication *application)
{
  return g_object_new (GTD_TYPE_WINDOW,
                       "application", application,
                       "manager", gtd_manager_get_default (),
                       NULL);
}

static void
gtd_window_finalize (GObject *object)
{
  GtdWindowPrivate *priv = GTD_WINDOW (object)->priv;

  g_clear_object (&priv->loading_notification);
  G_OBJECT_CLASS (gtd_window_parent_class)->finalize (object);
}

static void
gtd_window_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  GtdWindow *self = GTD_WINDOW (object);

  switch (prop_id)
    {
    case PROP_MANAGER:
      g_value_set_object (value, self->priv->manager);
      break;

    case PROP_MODE:
      g_value_set_enum (value, self->priv->mode);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_window_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  GtdWindow *self = GTD_WINDOW (object);
  GList *lists;
  GList *l;

  switch (prop_id)
    {
    case PROP_MANAGER:
      self->priv->manager = g_value_get_object (value);

      /* Add plugins' header widgets, and setup for new plugins */
      {
        GtdPluginManager *plugin_manager;
        GList *plugins;

        plugin_manager = gtd_manager_get_plugin_manager (self->priv->manager);
        plugins = gtd_plugin_manager_get_loaded_plugins (plugin_manager);

        for (l = plugins; l != NULL; l = l->next)
          plugin_loaded (self, NULL, l->data);

        g_signal_connect_swapped (plugin_manager,
                                  "plugin-loaded",
                                  G_CALLBACK (plugin_loaded),
                                  self);

        g_signal_connect_swapped (plugin_manager,
                                  "plugin-unloaded",
                                  G_CALLBACK (plugin_unloaded),
                                  self);

        g_list_free (plugins);
      }

      g_signal_connect (self->priv->manager,
                        "notify::ready",
                        G_CALLBACK (gtd_window__manager_ready_changed),
                        self);
      g_signal_connect (self->priv->manager,
                        "panel-added",
                        G_CALLBACK (gtd_window__panel_added),
                        self);
      g_signal_connect (self->priv->manager,
                        "panel-removed",
                        G_CALLBACK (gtd_window__panel_removed),
                        self);
      g_signal_connect (self->priv->manager,
                        "show-error-message",
                        G_CALLBACK (gtd_window__show_error_message),
                        self);

      /* Add loaded panels */
      lists = gtd_manager_get_panels (self->priv->manager);

      for (l = lists; l != NULL; l = l->next)
        {
          gtd_window__panel_added (self->priv->manager,
                                   l->data,
                                   GTD_WINDOW (object));
        }

      g_list_free (lists);

      g_object_notify (object, "manager");
      break;

    case PROP_MODE:
      gtd_window_set_mode (self, g_value_get_enum (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_window_class_init (GtdWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gtd_window_finalize;
  object_class->constructed = gtd_window_constructed;
  object_class->get_property = gtd_window_get_property;
  object_class->set_property = gtd_window_set_property;

  widget_class->configure_event = gtd_window_configure_event;
  widget_class->window_state_event = gtd_window_state_event;

  /**
   * GtdWindow::manager:
   *
   * A weak reference to the application's #GtdManager instance.
   */
  g_object_class_install_property (
        object_class,
        PROP_MANAGER,
        g_param_spec_object ("manager",
                             "Manager of this window's application",
                             "The manager of the window's application",
                             GTD_TYPE_MANAGER,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

  /**
   * GtdWindow::mode:
   *
   * The current interaction mode of the window.
   */
  g_object_class_install_property (
        object_class,
        PROP_MODE,
        g_param_spec_enum ("mode",
                           "Mode of this window",
                           "The interaction mode of the window",
                           GTD_TYPE_WINDOW_MODE,
                           GTD_WINDOW_MODE_NORMAL,
                           G_PARAM_READWRITE));

  g_type_ensure (GTD_TYPE_NOTIFICATION_WIDGET);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/todo/ui/window.ui");

  gtk_widget_class_bind_template_child_private (widget_class, GtdWindow, cancel_selection_button);
  gtk_widget_class_bind_template_child_private (widget_class, GtdWindow, gear_menu_button);
  gtk_widget_class_bind_template_child_private (widget_class, GtdWindow, headerbar);
  gtk_widget_class_bind_template_child_private (widget_class, GtdWindow, notification_widget);
  gtk_widget_class_bind_template_child_private (widget_class, GtdWindow, stack);
  gtk_widget_class_bind_template_child_private (widget_class, GtdWindow, stack_switcher);

  gtk_widget_class_bind_template_child_private (widget_class, GtdWindow, extension_box_end);
  gtk_widget_class_bind_template_child_private (widget_class, GtdWindow, extension_box_start);
  gtk_widget_class_bind_template_child_private (widget_class, GtdWindow, panel_box_end);
  gtk_widget_class_bind_template_child_private (widget_class, GtdWindow, panel_box_start);

  gtk_widget_class_bind_template_callback (widget_class, gtd_window__cancel_selection_button_clicked);
  gtk_widget_class_bind_template_callback (widget_class, gtd_window__stack_visible_child_cb);
}

static void
gtd_window_init (GtdWindow *self)
{
  GtkWidget *panel;

  self->priv = gtd_window_get_instance_private (self);

  self->priv->loading_notification = gtd_notification_new (_("Loading your task lists…"), 0);
  gtd_object_set_ready (GTD_OBJECT (self->priv->loading_notification), FALSE);

  gtk_widget_init_template (GTK_WIDGET (self));

  /* Add the default 'Lists' panel before everything else */
  panel = gtd_list_selector_panel_new ();

  gtd_window__panel_added (gtd_manager_get_default (),
                           GTD_PANEL (panel),
                           self);

  g_object_bind_property (self,
                          "mode",
                          panel,
                          "mode",
                          G_BINDING_BIDIRECTIONAL);
}

/**
 * gtd_window_get_manager:
 * @window: a #GtdWindow
 *
 * Retrieves a weak reference for the @window's #GtdManager.
 *
 * Returns: (transfer none): the #GtdManager of @window.
 */
GtdManager*
gtd_window_get_manager (GtdWindow *window)
{
  g_return_val_if_fail (GTD_IS_WINDOW (window), NULL);

  return window->priv->manager;
}

/**
 * gtd_window_notify:
 * @window: a #GtdWindow
 * @notification: a #GtdNotification
 *
 * Shows a notification on the top of the main window.
 */
void
gtd_window_notify (GtdWindow       *window,
                   GtdNotification *notification)
{
  GtdWindowPrivate *priv;

  g_return_if_fail (GTD_IS_WINDOW (window));

  priv = window->priv;

  gtd_notification_widget_notify (priv->notification_widget, notification);
}

/**
 * gtd_window_cancel_notification:
 * @window: a #GtdManager
 * @notification: a #GtdNotification
 *
 * Cancels @notification.
 */
void
gtd_window_cancel_notification (GtdWindow       *window,
                                GtdNotification *notification)
{
  GtdWindowPrivate *priv;

  g_return_if_fail (GTD_IS_WINDOW (window));

  priv = window->priv;

  gtd_notification_widget_cancel (priv->notification_widget, notification);
}

/**
 * gtd_window_get_mode:
 * @window: a #GtdWindow
 *
 * Retrieves the current mode of @window.
 *
 * Returns: the #GtdWindow::mode property value
 */
GtdWindowMode
gtd_window_get_mode (GtdWindow *window)
{
  g_return_val_if_fail (GTD_IS_WINDOW (window), GTD_WINDOW_MODE_NORMAL);

  return window->priv->mode;
}

/**
 * gtd_window_set_mode:
 * @window: a #GtdWindow
 * @mode: a #GtdWindowMode
 *
 * Sets the current window mode to @mode.
 */
void
gtd_window_set_mode (GtdWindow     *window,
                     GtdWindowMode  mode)
{
  GtdWindowPrivate *priv;

  g_return_if_fail (GTD_IS_WINDOW (window));

  priv = window->priv;

  if (priv->mode != mode)
    {
      GtkStyleContext *context;
      gboolean is_selection_mode;

      priv->mode = mode;
      context = gtk_widget_get_style_context (GTK_WIDGET (priv->headerbar));
      is_selection_mode = (mode == GTD_WINDOW_MODE_SELECTION);

      gtk_widget_set_visible (priv->gear_menu_button, !is_selection_mode);
      gtk_widget_set_visible (priv->cancel_selection_button, is_selection_mode);
      gtk_header_bar_set_show_close_button (priv->headerbar, !is_selection_mode);
      gtk_header_bar_set_subtitle (priv->headerbar, NULL);

      if (is_selection_mode)
        {
          gtk_style_context_add_class (context, "selection-mode");
          gtk_header_bar_set_custom_title (priv->headerbar, NULL);
          gtk_header_bar_set_title (priv->headerbar, _("Click a task list to select"));
        }
      else
        {
          gtk_style_context_remove_class (context, "selection-mode");
          gtk_header_bar_set_custom_title (priv->headerbar, GTK_WIDGET (priv->stack_switcher));
          gtk_header_bar_set_title (priv->headerbar, _("To Do"));
        }

      g_object_notify (G_OBJECT (window), "mode");
    }
}

/**
 * gtd_window_set_custom_title:
 * @window: a #GtdWindow
 * @title: (nullable): the #GtkHeaderBar title
 * @subtitle: (nullable): the #GtkHeaderBar subtitle
 *
 * Sets the #GtdWindow's headerbar title and subtitle. If @title is %NULL,
 * the header will be set to the stack switcher.
 */
void
gtd_window_set_custom_title (GtdWindow   *window,
                             const gchar *title,
                             const gchar *subtitle)
{
  GtdWindowPrivate *priv;

  g_return_if_fail (GTD_IS_WINDOW (window));

  priv = window->priv;

  if (title)
    {
      gtk_header_bar_set_custom_title (priv->headerbar, NULL);
      gtk_header_bar_set_title (priv->headerbar, title);
      gtk_header_bar_set_subtitle (window->priv->headerbar, subtitle);
    }
  else
    {
      gtk_header_bar_set_title (priv->headerbar, _("To Do"));
      gtk_header_bar_set_custom_title (priv->headerbar, GTK_WIDGET (priv->stack_switcher));
    }
}
