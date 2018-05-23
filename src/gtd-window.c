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

#include "config.h"

#include "interfaces/gtd-activatable.h"
#include "interfaces/gtd-provider.h"
#include "interfaces/gtd-panel.h"
#include "gtd-application.h"
#include "gtd-enum-types.h"
#include "gtd-task-list-view.h"
#include "gtd-manager.h"
#include "gtd-manager-protected.h"
#include "gtd-notification.h"
#include "gtd-notification-widget.h"
#include "gtd-plugin-manager.h"
#include "gtd-sidebar.h"
#include "gtd-task.h"
#include "gtd-task-list.h"
#include "gtd-task-list-panel.h"
#include "gtd-window.h"
#include "gtd-window-private.h"

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

struct _GtdWindow
{
  GtkApplicationWindow application;

  GtkWidget          *cancel_selection_button;
  GtkWidget          *gear_menu_button;
  GtkHeaderBar       *headerbar;
  GtkStack           *stack;
  GtdSidebar         *sidebar;

  GtdNotificationWidget *notification_widget;

  /* boxes */
  GtkWidget          *extension_box_end;
  GtkWidget          *extension_box_start;
  GtkWidget          *panel_box_end;
  GtkWidget          *panel_box_start;

  GtdPanel           *active_panel;
  GtdPanel           *task_list_panel;

  /* mode */
  GtdWindowMode       mode;

  guint               save_geometry_timeout_id;
};

typedef struct
{
  GtdWindow          *window;
  gchar              *primary_text;
  gchar              *secondary_text;
} ErrorData;


#define              SAVE_GEOMETRY_ID_TIMEOUT                    100 /* ms */


G_DEFINE_TYPE (GtdWindow, gtd_window, GTK_TYPE_APPLICATION_WINDOW)


enum
{
  PROP_0,
  PROP_MODE,
  LAST_PROP
};


static void
setup_development_build (GtdWindow *self)
{
  GtkStyleContext *context;

  g_message (_("This is a development build of To Do. You may experience errors, wrong behaviors, "
               "and data loss."));

  context = gtk_widget_get_style_context (GTK_WIDGET (self));

  gtk_style_context_add_class (context, "development-version");
}

static gboolean
is_development_build (void)
{
#ifdef DEVELOPMENT_BUILD
  return TRUE;
#else
  return FALSE;
#endif
}

static void
add_widgets (GtdWindow *self,
             GtkWidget *container_start,
             GtkWidget *container_end,
             GList     *widgets)
{
  GList *l;

  for (l = widgets; l; l = l->next)
    {
      switch (gtk_widget_get_halign (l->data))
        {
        case GTK_ALIGN_START:
          gtk_box_pack_start (GTK_BOX (container_start), l->data, FALSE, FALSE, 0);
          break;

        case GTK_ALIGN_CENTER:
          gtk_header_bar_set_custom_title (self->headerbar, l->data);
          break;

        case GTK_ALIGN_END:
          gtk_box_pack_end (GTK_BOX (container_end), l->data, FALSE, FALSE, 0);
          break;

        case GTK_ALIGN_BASELINE:
        case GTK_ALIGN_FILL:
        default:
          gtk_box_pack_start (GTK_BOX (container_start), l->data, FALSE, FALSE, 0);
          break;
        }
    }
}

static void
remove_widgets (GtdWindow *self,
                GtkWidget *container_start,
                GtkWidget *container_end,
                GList     *widgets)
{
  GList *l;

  for (l = widgets; l; l = l->next)
    {
      GtkWidget *container;

      if (gtk_widget_get_halign (l->data) == GTK_ALIGN_END)
        container = container_end;
      else if (gtk_widget_get_halign (l->data) == GTK_ALIGN_CENTER)
        container = GTK_WIDGET (self->headerbar);
      else
        container = container_start;

      g_object_ref (l->data);
      gtk_container_remove (GTK_CONTAINER (container), l->data);
    }
}

static void
on_plugin_loaded_cb (GtdWindow      *self,
                     gpointer        unused_field,
                     GtdActivatable *activatable)
{
  g_autoptr (GList) header_widgets = gtd_activatable_get_header_widgets (activatable);

  add_widgets (self,
               self->extension_box_start,
               self->extension_box_end,
               header_widgets);
}

static void
on_plugin_unloaded_cb (GtdWindow      *self,
                       gpointer        unused_field,
                       GtdActivatable *activatable)
{
  GList *header_widgets = gtd_activatable_get_header_widgets (activatable);

  remove_widgets (self,
                  self->extension_box_start,
                  self->extension_box_end,
                  header_widgets);
}

static void
update_panel_menu (GtdWindow *self)
{
  const GMenu *menu = gtd_panel_get_menu (self->active_panel);

  gtk_menu_button_set_menu_model (GTK_MENU_BUTTON (self->gear_menu_button), G_MENU_MODEL (menu));
}

static void
gtd_window__panel_menu_changed (GObject    *object,
                                GParamSpec *pspec,
                                GtdWindow  *self)
{
  if (GTD_PANEL (object) != self->active_panel)
    return;

  update_panel_menu (self);
}

static void
on_panel_added_cb (GtdManager *manager,
                   GtdPanel   *panel,
                   GtdWindow  *self)
{
  gtk_stack_add_titled (self->stack,
                        GTK_WIDGET (panel),
                        gtd_panel_get_panel_name (panel),
                        gtd_panel_get_panel_title (panel));
}

static void
on_panel_removed_cb (GtdManager *manager,
                     GtdPanel   *panel,
                     GtdWindow  *self)
{
  gtk_container_remove (GTK_CONTAINER (self->stack), GTK_WIDGET (panel));
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
load_geometry (GtdWindow *self)
{
  g_autoptr (GVariant) position_variant = NULL;
  g_autoptr (GVariant) size_variant = NULL;
  GSettings *settings;
  gboolean maximized;
  const gint32 *position;
  const gint32 *size;
  gsize n_elements;

  settings = gtd_manager_get_settings (gtd_manager_get_default ());

  /* load window settings: size */
  size_variant = g_settings_get_value (settings, "window-size");
  size = g_variant_get_fixed_array (size_variant, &n_elements, sizeof (gint32));

  if (n_elements == 2)
    {
      gtk_window_set_default_size (GTK_WINDOW (self),
                                   CLAMP (size[0], 1, G_MAXINT),
                                   CLAMP (size[1], 1, G_MAXINT));
    }

  /* load window settings: position */
  position_variant = g_settings_get_value (settings, "window-position");
  position = g_variant_get_fixed_array (position_variant, &n_elements, sizeof (gint32));

  if (n_elements == 2)
    {
      gtk_window_move (GTK_WINDOW (self),
                       CLAMP (position[0], 0, G_MAXINT),
                       CLAMP (position[1], 0, G_MAXINT));
    }

  /* load window settings: state */
  maximized = g_settings_get_boolean (settings, "window-maximized");

  if (maximized)
    gtk_window_maximize (GTK_WINDOW (self));
}

static gboolean
save_geometry (gpointer user_data)
{
  GdkWindowState state;
  GtdWindow *self;
  GdkWindow *gdk_window;
  GtkWindow *window;
  GSettings *settings;
  gboolean maximized;
  GVariant *variant;
  gint32 size[2];
  gint32 position[2];

  self = GTD_WINDOW (user_data);
  window = GTK_WINDOW (user_data);
  gdk_window = gtk_widget_get_window (GTK_WIDGET (self));
  state = gdk_window_get_state (gdk_window);

  settings = gtd_manager_get_settings (gtd_manager_get_default ());

  /* save window's state */
  maximized = state & GDK_WINDOW_STATE_MAXIMIZED;

  g_settings_set_boolean (settings, "window-maximized", maximized);

  if (maximized)
    {
      self->save_geometry_timeout_id = 0;
      return FALSE;
    }

  /* save window's size */
  gtk_window_get_size (window, (gint*) &size[0], (gint*) &size[1]);
  size[0] = CLAMP (size[0], 1, G_MAXINT);
  size[1] = CLAMP (size[1], 1, G_MAXINT);

  variant = g_variant_new_fixed_array (G_VARIANT_TYPE_INT32, size, 2, sizeof (size[0]));
  g_settings_set_value (settings, "window-size", variant);

  /* save windows's position */
  gtk_window_get_position (window, (gint *) &position[0], (gint *) &position[1]);
  position[0] = CLAMP (position[0], 0, G_MAXINT);
  position[1] = CLAMP (position[1], 0, G_MAXINT);

  variant = g_variant_new_fixed_array (G_VARIANT_TYPE_INT32, position, 2, sizeof (position[0]));
  g_settings_set_value (settings, "window-position", variant);

  self->save_geometry_timeout_id = 0;

  return FALSE;
}

static void
on_cancel_selection_button_clicked (GtkWidget *button,
                                    GtdWindow *self)
{
  gtd_window_set_mode (self, GTD_WINDOW_MODE_NORMAL);
}

static void
on_stack_visible_child_cb (GtdWindow  *self,
                           GParamSpec *pspec,
                           GtkStack   *stack)
{
  GtkWidget *visible_child;
  GtdPanel *panel;
  GList *header_widgets;

  visible_child = gtk_stack_get_visible_child (stack);
  panel = GTD_PANEL (visible_child);

  /* Remove previous panel's widgets */
  if (self->active_panel)
    {
      header_widgets = gtd_panel_get_header_widgets (self->active_panel);

      /* Disconnect signals */
      g_signal_handlers_disconnect_by_func (self->active_panel,
                                            gtd_window__panel_menu_changed,
                                            self);

      remove_widgets (self,
                      self->panel_box_start,
                      self->panel_box_end,
                      header_widgets);

      g_list_free (header_widgets);
    }

  /* Add current panel's header widgets */
  header_widgets = gtd_panel_get_header_widgets (panel);

  add_widgets (self,
               self->panel_box_start,
               self->panel_box_end,
               header_widgets);

  g_list_free (header_widgets);

  g_signal_connect (panel,
                    "notify::menu",
                    G_CALLBACK (gtd_window__panel_menu_changed),
                    self);

  /* Set panel as the new active panel */
  g_set_object (&self->active_panel, panel);

  /* Setup the panel's menu */
  update_panel_menu (self);
}

static void
on_show_error_message_cb (GtdManager                *manager,
                          const gchar               *primary_text,
                          const gchar               *secondary_text,
                          GtdNotificationActionFunc  function,
                          gpointer                   user_data,
                          GtdWindow                 *self)
{
  GtdNotification *notification;
  ErrorData *error_data;

  error_data = g_new0 (ErrorData, 1);
  notification = gtd_notification_new (primary_text, 7500);

  error_data->window = self;
  error_data->primary_text = g_strdup (primary_text);
  error_data->secondary_text = g_strdup (secondary_text);

  gtd_notification_set_primary_action (notification,
                                       error_message_notification_primary_action,
                                       error_data);

  if (!function)
    {
      gtd_notification_set_secondary_action (notification,
                                             _("Details"),
                                             error_message_notification_secondary_action,
                                             error_data);
    }
  else
    {
      gtd_notification_set_secondary_action (notification, secondary_text, function, user_data);
    }


  gtd_window_notify (self, notification);
}

static void
on_show_notification_cb (GtdManager      *manager,
                         GtdNotification *notification,
                         GtdWindow       *self)
{
  gtd_window_notify (self, notification);
}


/*
 * GtkWindow overrides
 */

static gboolean
gtd_window_configure_event (GtkWidget         *widget,
                            GdkEventConfigure *event)
{
  GtdWindow *self;
  gboolean retval;

  self = GTD_WINDOW (widget);

  if (self->save_geometry_timeout_id != 0)
    {
      g_source_remove (self->save_geometry_timeout_id);
      self->save_geometry_timeout_id = 0;
    }

  self->save_geometry_timeout_id = g_timeout_add (SAVE_GEOMETRY_ID_TIMEOUT, save_geometry, self);

  retval = GTK_WIDGET_CLASS (gtd_window_parent_class)->configure_event (widget, event);

  return retval;
}

static gboolean
gtd_window_state_event (GtkWidget           *widget,
                        GdkEventWindowState *event)
{
  GtdWindow *self;
  gboolean retval;

  self = GTD_WINDOW (widget);

  if (self->save_geometry_timeout_id != 0)
    {
      g_source_remove (self->save_geometry_timeout_id);
      self->save_geometry_timeout_id = 0;
    }

  self->save_geometry_timeout_id = g_timeout_add (SAVE_GEOMETRY_ID_TIMEOUT, save_geometry, self);

  retval = GTK_WIDGET_CLASS (gtd_window_parent_class)->window_state_event (widget, event);

  return retval;
}


/*
 * GObject overrides
 */

static void
gtd_window_constructed (GObject *object)
{
  g_autoptr (GList) plugins = NULL;
  g_autoptr (GList) lists = NULL;
  g_autoptr (GList) l = NULL;
  GtdPluginManager *plugin_manager;
  GtkApplication *app;
  GtdWindow *self;
  GMenu *menu;

  self = GTD_WINDOW (object);

  G_OBJECT_CLASS (gtd_window_parent_class)->constructed (object);

  /* Load stored size */
  load_geometry (GTD_WINDOW (object));

  /* Gear menu */
  app = GTK_APPLICATION (g_application_get_default ());
  menu = gtk_application_get_menu_by_id (app, "gear-menu");
  gtk_menu_button_set_menu_model (GTK_MENU_BUTTON (self->gear_menu_button), G_MENU_MODEL (menu));

  /* Add plugins' header widgets, and setup for new plugins */
  plugin_manager = gtd_manager_get_plugin_manager (gtd_manager_get_default ());
  plugins = gtd_plugin_manager_get_loaded_plugins (plugin_manager);

  for (l = plugins; l; l = l->next)
    on_plugin_loaded_cb (self, NULL, l->data);

  g_signal_connect_swapped (plugin_manager, "plugin-loaded", G_CALLBACK (on_plugin_loaded_cb), self);
  g_signal_connect_swapped (plugin_manager, "plugin-unloaded", G_CALLBACK (on_plugin_unloaded_cb), self);

  /* Add loaded panels */
  lists = gtd_manager_get_panels (gtd_manager_get_default ());

  for (l = lists; l; l = l->next)
    on_panel_added_cb (NULL, l->data, self);

  g_signal_connect (gtd_manager_get_default (), "panel-added", G_CALLBACK (on_panel_added_cb), self);
  g_signal_connect (gtd_manager_get_default (), "panel-removed", G_CALLBACK (on_panel_removed_cb), self);

  g_signal_connect (gtd_manager_get_default (), "show-error-message", G_CALLBACK (on_show_error_message_cb), self);
  g_signal_connect (gtd_manager_get_default (), "show-notification", G_CALLBACK (on_show_notification_cb), self);
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
    case PROP_MODE:
      g_value_set_enum (value, self->mode);
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

  switch (prop_id)
    {
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

  object_class->constructed = gtd_window_constructed;
  object_class->get_property = gtd_window_get_property;
  object_class->set_property = gtd_window_set_property;

  widget_class->configure_event = gtd_window_configure_event;
  widget_class->window_state_event = gtd_window_state_event;

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
  g_type_ensure (GTD_TYPE_SIDEBAR);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/todo/ui/window.ui");

  gtk_widget_class_bind_template_child (widget_class, GtdWindow, cancel_selection_button);
  gtk_widget_class_bind_template_child (widget_class, GtdWindow, gear_menu_button);
  gtk_widget_class_bind_template_child (widget_class, GtdWindow, headerbar);
  gtk_widget_class_bind_template_child (widget_class, GtdWindow, notification_widget);
  gtk_widget_class_bind_template_child (widget_class, GtdWindow, sidebar);
  gtk_widget_class_bind_template_child (widget_class, GtdWindow, stack);

  gtk_widget_class_bind_template_child (widget_class, GtdWindow, extension_box_end);
  gtk_widget_class_bind_template_child (widget_class, GtdWindow, extension_box_start);
  gtk_widget_class_bind_template_child (widget_class, GtdWindow, panel_box_end);
  gtk_widget_class_bind_template_child (widget_class, GtdWindow, panel_box_start);

  gtk_widget_class_bind_template_callback (widget_class, on_cancel_selection_button_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_stack_visible_child_cb);
}

static void
gtd_window_init (GtdWindow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  /* Task list panel */
  self->task_list_panel = GTD_PANEL (gtd_task_list_panel_new ());
  on_panel_added_cb (gtd_manager_get_default (), self->task_list_panel, self);

  gtd_sidebar_set_panel_stack (self->sidebar, GTK_STACK (self->stack));
  gtd_sidebar_set_task_list_panel (self->sidebar, self->task_list_panel);

  /* Development build */
  if (is_development_build ())
    setup_development_build (self);
}

GtkWidget*
gtd_window_new (GtdApplication *application)
{
  return g_object_new (GTD_TYPE_WINDOW,
                       "application", application,
                       NULL);
}

/**
 * gtd_window_notify:
 * @window: a #GtdWindow
 * @notification: a #GtdNotification
 *
 * Shows a notification on the top of the main window.
 */
void
gtd_window_notify (GtdWindow       *self,
                   GtdNotification *notification)
{
  g_return_if_fail (GTD_IS_WINDOW (self));

  gtd_notification_widget_notify (self->notification_widget, notification);
}

/**
 * gtd_window_cancel_notification:
 * @window: a #GtdManager
 * @notification: a #GtdNotification
 *
 * Cancels @notification.
 */
void
gtd_window_cancel_notification (GtdWindow       *self,
                                GtdNotification *notification)
{
  g_return_if_fail (GTD_IS_WINDOW (self));

  gtd_notification_widget_cancel (self->notification_widget, notification);
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
gtd_window_get_mode (GtdWindow *self)
{
  g_return_val_if_fail (GTD_IS_WINDOW (self), GTD_WINDOW_MODE_NORMAL);

  return self->mode;
}

/**
 * gtd_window_set_mode:
 * @window: a #GtdWindow
 * @mode: a #GtdWindowMode
 *
 * Sets the current window mode to @mode.
 */
void
gtd_window_set_mode (GtdWindow     *self,
                     GtdWindowMode  mode)
{
  g_return_if_fail (GTD_IS_WINDOW (self));

  if (self->mode != mode)
    {
      GtkStyleContext *context;
      gboolean is_selection_mode;

      self->mode = mode;
      context = gtk_widget_get_style_context (GTK_WIDGET (self->headerbar));
      is_selection_mode = (mode == GTD_WINDOW_MODE_SELECTION);

      gtk_widget_set_visible (self->gear_menu_button, !is_selection_mode);
      gtk_widget_set_visible (self->cancel_selection_button, is_selection_mode);
      gtk_header_bar_set_show_close_button (self->headerbar, !is_selection_mode);
      gtk_header_bar_set_subtitle (self->headerbar, NULL);

      if (is_selection_mode)
        {
          gtk_style_context_add_class (context, "selection-mode");
          gtk_header_bar_set_custom_title (self->headerbar, NULL);
          gtk_header_bar_set_title (self->headerbar, _("Click a task list to select"));
        }
      else
        {
          gtk_style_context_remove_class (context, "selection-mode");
          gtk_header_bar_set_title (self->headerbar, _("To Do"));
        }

      g_object_notify (G_OBJECT (self), "mode");
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
gtd_window_set_custom_title (GtdWindow   *self,
                             const gchar *title,
                             const gchar *subtitle)
{
  g_return_if_fail (GTD_IS_WINDOW (self));

  if (title)
    {
      gtk_header_bar_set_custom_title (self->headerbar, NULL);
      gtk_header_bar_set_title (self->headerbar, title);
      gtk_header_bar_set_subtitle (self->headerbar, subtitle);
    }
  else
    {
      gtk_header_bar_set_title (self->headerbar, _("To Do"));
    }
}

/* Private functions */
void
_gtd_window_finish_startup (GtdWindow *self)
{
  g_return_if_fail (GTD_IS_WINDOW (self));

  gtd_sidebar_activate (self->sidebar);
}
