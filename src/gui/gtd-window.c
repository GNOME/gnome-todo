/* gtd-window.c
 *
 * Copyright (C) 2015-2020 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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

#include "gtd-activatable.h"
#include "gtd-application.h"
#include "gtd-debug.h"
#include "gtd-task-list-view.h"
#include "gtd-manager.h"
#include "gtd-manager-protected.h"
#include "gtd-menu-button.h"
#include "gtd-notification.h"
#include "gtd-notification-widget.h"
#include "gtd-omni-area.h"
#include "gtd-plugin-manager.h"
#include "gtd-provider.h"
#include "gtd-panel.h"
#include "gtd-task.h"
#include "gtd-task-list.h"
#include "gtd-window.h"
#include "gtd-workspace.h"

#include <glib/gi18n.h>
#include <libpeas/peas.h>

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
  AdwApplicationWindow application;

  AdwHeaderBar       *headerbar;
  GtkBox             *headerbar_box;
  GtkStack           *stack;
  GtkMenuButton      *primary_menu_button;
  GtkBox             *workspace_box_end;
  GtkBox             *workspace_box_start;
  GtkListBox         *workspaces_listbox;
  GtdMenuButton      *workspaces_menu_button;

  GtdNotificationWidget *notification_widget;

  GPtrArray          *workspace_header_widgets;

  GtdWorkspace       *current_workspace;
  GListStore         *workspaces;
  GVariant           *parameters;

  PeasExtensionSet   *workspaces_set;

  guint               toggle_headerbar_revealer_id;
};

typedef struct
{
  GtdWindow          *window;
  gchar              *primary_text;
  gchar              *secondary_text;
} ErrorData;

enum
{
  PROP_0,
  PROP_CURRENT_WORKSPACE,
  N_PROPS,
};

static GParamSpec *properties[N_PROPS] = { NULL, };

G_DEFINE_TYPE (GtdWindow, gtd_window, ADW_TYPE_APPLICATION_WINDOW)

static gint             compare_workspaced_func                  (gconstpointer      a,
                                                                  gconstpointer      b,
                                                                  gpointer           user_data);

static void
setup_development_build (GtdWindow *self)
{
  GtkStyleContext *context;

  g_message (_("This is a development build of To Do. You may experience errors, wrong behaviors, "
               "and data loss."));

  context = gtk_widget_get_style_context (GTK_WIDGET (self));

  gtk_style_context_add_class (context, "devel");
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
                    G_CALLBACK (gtk_window_destroy),
                    NULL);

  gtk_widget_show (message_dialog);

  error_data_free (data);
}

static void
load_geometry (GtdWindow *self)
{
  GSettings *settings;
  GtkWindow *window;
  gboolean maximized;
  gint height;
  gint width;

  window = GTK_WINDOW (self);
  settings = gtd_manager_get_settings (gtd_manager_get_default ());

  maximized = g_settings_get_boolean (settings, "window-maximized");
  g_settings_get (settings, "window-size", "(ii)", &width, &height);

  gtk_window_set_default_size (window, width, height);

  if (maximized)
    gtk_window_maximize (window);
}

static void
add_workspace (GtdWindow    *self,
               GtdWorkspace *workspace)
{
  const gchar *workspace_id;

  workspace_id = gtd_workspace_get_id (workspace);

  gtk_stack_add_named (self->stack, GTK_WIDGET (workspace), workspace_id);
  g_list_store_insert_sorted (self->workspaces, workspace, compare_workspaced_func, self);
}

static void
remove_workspace (GtdWindow    *self,
                  GtdWorkspace *workspace)
{
  guint position;

  if (!g_list_store_find (self->workspaces, workspace, &position))
    return;

  gtk_stack_remove (self->stack, GTK_WIDGET (workspace));
  g_list_store_remove (self->workspaces, position);
}

static void
remove_all_workspace_header_widgets (GtdWindow *self)
{
  GtkWidget *parent;
  GtkWidget *widget;
  guint i;

  GTD_ENTRY;

  /* remove from the header */
  for (i = 0; i < self->workspace_header_widgets->len; i++)
    {
      widget = g_ptr_array_index (self->workspace_header_widgets, i);
      parent = gtk_widget_get_parent (widget);

      g_assert (parent == GTK_WIDGET (self->workspace_box_start) ||
                parent == GTK_WIDGET (self->workspace_box_end));
      gtk_box_remove (GTK_BOX (parent), widget);
    }

  g_ptr_array_set_size (self->workspace_header_widgets, 0);

  GTD_EXIT;
}


/*
 * Callbacks
 */


static void
on_action_activate_workspace_activated_cb (GSimpleAction *simple,
                                           GVariant      *state,
                                           gpointer       user_data)
{
  g_autofree gchar *workspace_id = NULL;
  GtdWindow *self;

  self = GTD_WINDOW (user_data);
  g_variant_get (state,
                 "(sv)",
                 &workspace_id,
                 &self->parameters,
                 NULL);

  gtk_stack_set_visible_child_name (self->stack, workspace_id);
}

static gint
compare_workspaced_func (gconstpointer a,
                         gconstpointer b,
                         gpointer      user_data)
{
  gint a_priority;
  gint b_priority;

  a_priority = gtd_workspace_get_priority ((GtdWorkspace *)a);
  b_priority = gtd_workspace_get_priority ((GtdWorkspace *)b);

  return b_priority - a_priority;
}

static void
on_stack_visible_child_cb (GtkStack   *stack,
                           GParamSpec *pspec,
                           GtdWindow  *self)
{
  g_autoptr (GVariant) parameters = NULL;
  g_autoptr (GIcon) workspace_icon = NULL;
  GtdWorkspace *new_workspace;

  GTD_ENTRY;

  remove_all_workspace_header_widgets (self);

  if (self->current_workspace)
    gtd_workspace_deactivate (self->current_workspace);

  new_workspace = GTD_WORKSPACE (gtk_stack_get_visible_child (stack));
  self->current_workspace = new_workspace;

  if (!new_workspace)
    GTD_GOTO (out);

  parameters = g_steal_pointer (&self->parameters);
  gtd_workspace_activate (new_workspace, parameters);

  workspace_icon = gtd_workspace_get_icon (new_workspace);
  gtd_menu_button_set_gicon (self->workspaces_menu_button, workspace_icon);

out:
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURRENT_WORKSPACE]);
  GTD_EXIT;
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

static void
on_workspace_added_cb (PeasExtensionSet *extension_set,
                       PeasPluginInfo   *plugin_info,
                       GtdWorkspace     *workspace,
                       GtdWindow        *self)
{
  GTD_ENTRY;

  add_workspace (self, g_object_ref_sink (workspace));

  GTD_EXIT;
}

static void
on_workspace_removed_cb (PeasExtensionSet *extension_set,
                         PeasPluginInfo   *plugin_info,
                         GtdWorkspace     *workspace,
                         GtdWindow        *self)
{
  GTD_ENTRY;

  remove_workspace (self, workspace);

  GTD_EXIT;
}

static void
on_workspaces_listbox_row_activated_cb (GtkListBox    *workspaces_listbox,
                                        GtkListBoxRow *row,
                                        GtdWindow     *self)
{
  g_autoptr (GtdWorkspace) workspace = NULL;

  workspace = g_list_model_get_item (G_LIST_MODEL (self->workspaces),
                                     gtk_list_box_row_get_index (row));

  gtk_stack_set_visible_child (self->stack, GTK_WIDGET (workspace));

  gtd_menu_button_popdown (self->workspaces_menu_button);
}

static GtkWidget*
create_workspace_row_func (gpointer item,
                           gpointer user_data)
{
  GtkWidget *label;
  GtkWidget *image;
  GtkWidget *box;

  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_set_margin_start (box, 6);
  gtk_widget_set_margin_end (box, 6);
  gtk_widget_set_margin_top (box, 3);
  gtk_widget_set_margin_bottom (box, 3);

  image = gtk_image_new ();
  g_object_bind_property (item, "icon", image, "gicon", G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

  label = gtk_label_new ("");
  gtk_widget_set_hexpand (label, TRUE);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  g_object_bind_property (item, "title", label, "label", G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

  gtk_box_append (GTK_BOX (box), image);
  gtk_box_append (GTK_BOX (box), label);

  return box;
}


/*
 * GtkWindow overrides
 */

static void
gtd_window_unmap (GtkWidget *widget)
{
  GSettings *settings;
  GtkWindow *window;
  gboolean maximized;

  window = GTK_WINDOW (widget);
  settings = gtd_manager_get_settings (gtd_manager_get_default ());
  maximized = gtk_window_is_maximized (window);

  g_settings_set_boolean (settings, "window-maximized", maximized);

  if (!maximized)
    {
      gint height;
      gint width;

      gtk_window_get_default_size (window, &width, &height);
      g_settings_set (settings, "window-size", "(ii)", width, height);
    }

  GTK_WIDGET_CLASS (gtd_window_parent_class)->unmap (widget);
}

/*
 * GObject overrides
 */

static void
gtd_window_dispose (GObject *object)
{
  GtdWindow *self = GTD_WINDOW (object);

  g_clear_object (&self->workspaces_set);

  G_OBJECT_CLASS (gtd_window_parent_class)->dispose (object);
}

static void
gtd_window_finalize (GObject *object)
{
  GtdWindow *self = GTD_WINDOW (object);

  g_clear_handle_id (&self->toggle_headerbar_revealer_id, g_source_remove);
  g_clear_object (&self->workspaces);

  G_OBJECT_CLASS (gtd_window_parent_class)->finalize (object);
}

static void
gtd_window_constructed (GObject *object)
{
  GtdManager *manager;
  GtdWindow *self;

  self = GTD_WINDOW (object);

  G_OBJECT_CLASS (gtd_window_parent_class)->constructed (object);

  /* Load stored size */
  load_geometry (GTD_WINDOW (object));

  manager = gtd_manager_get_default ();
  g_signal_connect (manager, "show-error-message", G_CALLBACK (on_show_error_message_cb), self);
  g_signal_connect (manager, "show-notification", G_CALLBACK (on_show_notification_cb), self);

  /* Workspaces */
  self->workspaces_set = peas_extension_set_new (peas_engine_get_default (),
                                                 GTD_TYPE_WORKSPACE,
                                                 NULL);

  peas_extension_set_foreach (self->workspaces_set,
                              (PeasExtensionSetForeachFunc) on_workspace_added_cb,
                              self);

  g_object_connect (self->workspaces_set,
                    "signal::extension-added", G_CALLBACK (on_workspace_added_cb), self,
                    "signal::extension-removed", G_CALLBACK (on_workspace_removed_cb), self,
                    NULL);
}

static void
gtd_window_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  GtdWindow *self = (GtdWindow *) object;

  switch (prop_id)
    {
    case PROP_CURRENT_WORKSPACE:
      g_value_set_object (value, self->current_workspace);
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

  object_class->dispose = gtd_window_dispose;
  object_class->finalize = gtd_window_finalize;
  object_class->constructed = gtd_window_constructed;
  object_class->get_property = gtd_window_get_property;

  widget_class->unmap = gtd_window_unmap;

  properties[PROP_CURRENT_WORKSPACE] = g_param_spec_object ("current-workspace",
                                                            "Current workspace",
                                                            "Current workspace",
                                                            GTD_TYPE_WORKSPACE,
                                                            G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);

  g_type_ensure (GTD_TYPE_MENU_BUTTON);
  g_type_ensure (GTD_TYPE_NOTIFICATION_WIDGET);
  g_type_ensure (GTD_TYPE_OMNI_AREA);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/todo/ui/gtd-window.ui");

  gtk_widget_class_bind_template_child (widget_class, GtdWindow, headerbar);
  gtk_widget_class_bind_template_child (widget_class, GtdWindow, headerbar_box);
  gtk_widget_class_bind_template_child (widget_class, GtdWindow, notification_widget);
  gtk_widget_class_bind_template_child (widget_class, GtdWindow, primary_menu_button);
  gtk_widget_class_bind_template_child (widget_class, GtdWindow, stack);
  gtk_widget_class_bind_template_child (widget_class, GtdWindow, workspace_box_end);
  gtk_widget_class_bind_template_child (widget_class, GtdWindow, workspace_box_start);
  gtk_widget_class_bind_template_child (widget_class, GtdWindow, workspaces_menu_button);
  gtk_widget_class_bind_template_child (widget_class, GtdWindow, workspaces_listbox);

  gtk_widget_class_bind_template_callback (widget_class, on_stack_visible_child_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_workspaces_listbox_row_activated_cb);
}

static void
gtd_window_init (GtdWindow *self)
{
  GtkApplication *application;
  GMenu *primary_menu;

  static const GActionEntry entries[] = {
    { "activate-workspace", on_action_activate_workspace_activated_cb, "(sv)" },
  };

  g_action_map_add_action_entries (G_ACTION_MAP (self),
                                   entries,
                                   G_N_ELEMENTS (entries),
                                   self);

  self->workspace_header_widgets = g_ptr_array_new_with_free_func (g_object_unref);
  self->workspaces = g_list_store_new (GTD_TYPE_WORKSPACE);

  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_list_box_bind_model (self->workspaces_listbox,
                           G_LIST_MODEL (self->workspaces),
                           create_workspace_row_func,
                           self,
                           NULL);

  /* Fancy primary menu */
  application = GTK_APPLICATION (g_application_get_default ());
  primary_menu = gtk_application_get_menu_by_id (application, "primary-menu");
  gtk_menu_button_set_menu_model (self->primary_menu_button, G_MENU_MODEL (primary_menu));

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
 * gtd_window_embed_widget_in_header:
 * @self: a #GtdWindow
 * @widget: a #GtkWidget
 * @position: either @GTK_POS_LEFT or @GTK_POS_RIGHT
 *
 * Embeds @widget into @self's header bar.
 */
void
gtd_window_embed_widget_in_header (GtdWindow       *self,
                                   GtkWidget       *widget,
                                   GtkPositionType  position)
{
  g_return_if_fail (GTD_IS_WINDOW (self));
  g_return_if_fail (GTK_IS_WIDGET (widget));

  GTD_ENTRY;

  /* add to header */
  switch (position)
    {
    case GTK_POS_RIGHT:
      gtk_box_append (self->workspace_box_end, widget);
      break;

    case GTK_POS_LEFT:
      gtk_box_append (self->workspace_box_start, widget);
      break;

    case GTK_POS_TOP:
    case GTK_POS_BOTTOM:
    default:
      g_warning ("Invalid position passed");
      return;
    }

  g_ptr_array_add (self->workspace_header_widgets, g_object_ref (widget));

  GTD_EXIT;

}

GtdWorkspace*
gtd_window_get_current_workspace (GtdWindow *self)
{
  g_return_val_if_fail (GTD_IS_WINDOW (self), NULL);

  return self->current_workspace;
}
