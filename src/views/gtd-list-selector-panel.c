/* gtd-list-selector-panel.c
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

#define G_LOG_DOMAIN "GtdListSelectorPanel"

#include "gtd-enum-types.h"
#include "gtd-list-selector.h"
#include "gtd-list-selector-grid.h"
#include "gtd-list-selector-item.h"
#include "gtd-list-selector-list.h"
#include "gtd-list-selector-panel.h"
#include "gtd-manager.h"
#include "gtd-panel.h"
#include "gtd-provider.h"
#include "gtd-provider-popover.h"
#include "gtd-task-list.h"
#include "gtd-task-list-view.h"
#include "gtd-window.h"

#include <glib/gi18n.h>

struct _GtdListSelectorPanel
{
  GtkBox              parent;

  GtkWidget          *stack;
  GtkWidget          *tasklist_view;

  GtkWidget          *grid_selector;
  GtkWidget          *list_selector;
  GMenu              *menu;

  GtdListSelector    *active_selector;
  GtdListSelectorViewType active_view;

  /* Action bar widgets */
  GtkWidget          *actionbar;
  GtkWidget          *delete_button;
  GtkWidget          *rename_button;

  /* Header widgets */
  GtkWidget          *back_button;
  GtkWidget          *color_button;
  GtkWidget          *new_list_button;
  GtkWidget          *search_button;
  GtkWidget          *selection_button;
  GtkWidget          *view_button;
  GtkWidget          *view_button_image;

  /* Rename widgets */
  GtkWidget          *rename_entry;
  GtkWidget          *rename_popover;
  GtkWidget          *save_rename_button;

  /* Search bar widgets */
  GtkWidget          *search_bar;
  GtkWidget          *search_entry;

  GtdWindowMode       mode;
};

static void          gtd_panel_iface_init                        (GtdPanelInterface  *iface);

G_DEFINE_TYPE_EXTENDED (GtdListSelectorPanel, gtd_list_selector_panel, GTK_TYPE_STACK,
                        0,
                        G_IMPLEMENT_INTERFACE (GTD_TYPE_PANEL,
                                               gtd_panel_iface_init))

enum {
  PROP_0,
  PROP_MODE,
  PROP_MENU,
  PROP_NAME,
  PROP_TITLE,
  PROP_VIEW,
  N_PROPS
};

static void
gtd_list_selector_panel_set_view (GtdListSelectorPanel    *self,
                                  GtdListSelectorViewType  view)
{
  GSettings *settings;

  /* Load last active view */
  settings = gtd_manager_get_settings (gtd_manager_get_default ());

  switch (view)
    {
    case GTD_LIST_SELECTOR_VIEW_GRID:
      self->active_selector = GTD_LIST_SELECTOR (self->grid_selector);
      gtk_image_set_from_icon_name (GTK_IMAGE (self->view_button_image),
                                    "view-list-symbolic",
                                    GTK_ICON_SIZE_BUTTON);
      break;

    case GTD_LIST_SELECTOR_VIEW_LIST:
      self->active_selector = GTD_LIST_SELECTOR (self->list_selector);
      gtk_image_set_from_icon_name (GTK_IMAGE (self->view_button_image),
                                    "view-grid-symbolic",
                                    GTK_ICON_SIZE_BUTTON);
      break;

    default:
      self->active_selector = GTD_LIST_SELECTOR (self->grid_selector);
      g_warning ("Couldn't detect stored view, defaulting to 'grid'");
    }

  gtk_stack_set_visible_child (GTK_STACK (self->stack), GTK_WIDGET (self->active_selector));

  /* Save the current view */
  g_settings_set_enum (settings,
                       "view",
                       view);

  self->active_view = view;
  g_object_notify (G_OBJECT (self), "view");
}

static void
gtd_list_selector_panel_switch_view (GtdListSelectorPanel *panel)
{
  GtdListSelectorViewType next_view;

  if (GTK_WIDGET (panel->active_selector) == panel->grid_selector)
    next_view = GTD_LIST_SELECTOR_VIEW_LIST;
  else
    next_view = GTD_LIST_SELECTOR_VIEW_GRID;

  gtd_list_selector_panel_set_view (panel, next_view);
}

static void
gtd_list_selector_panel_select_button_toggled (GtkToggleButton      *button,
                                               GtdListSelectorPanel *panel)
{
  GtdWindowMode mode;
  GtdWindow *window;

  window = GTD_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (panel)));
  mode = gtk_toggle_button_get_active (button) ? GTD_WINDOW_MODE_SELECTION : GTD_WINDOW_MODE_NORMAL;

  gtd_window_set_mode (window, mode);
}


static gboolean
gtd_list_selector_panel_on_key_press_event (GtdListSelectorPanel *panel,
                                            GdkEvent             *event,
                                            GtkSearchBar         *bar)
{
  if (g_strcmp0 (gtk_stack_get_visible_child_name (GTK_STACK (panel)), "lists") == 0)
    {
      return gtk_search_bar_handle_event (bar, event);
    }

  return FALSE;
}

static void
gtd_list_selector_panel_list_color_set (GtkColorChooser      *button,
                                        GtdListSelectorPanel *panel)
{
  GtdTaskList *list;
  GtdManager *manager;
  GdkRGBA new_color;

  manager = gtd_manager_get_default ();
  list = gtd_task_list_view_get_task_list (GTD_TASK_LIST_VIEW (panel->tasklist_view));

  g_debug ("%s: %s: %s",
           G_STRFUNC,
           _("Setting new color for task list"),
           gtd_task_list_get_name (list));

  gtk_color_chooser_get_rgba (button, &new_color);
  gtd_task_list_set_color (list, &new_color);

  gtd_manager_save_task_list (manager, list);
}

static void
update_action_bar_buttons (GtdListSelectorPanel *panel)
{
  GList *selection;
  GList *l;
  gboolean all_lists_removable;
  gint selected_lists;

  selection = gtd_list_selector_get_selected_lists (panel->active_selector);
  selected_lists = g_list_length (selection);
  all_lists_removable = TRUE;

  for (l = selection; l != NULL; l = l->next)
    {
      GtdTaskList *list;

      list = gtd_list_selector_item_get_list (l->data);

      if (!gtd_task_list_is_removable (list))
        {
          all_lists_removable = FALSE;
          break;
        }
    }

  gtk_widget_set_sensitive (panel->delete_button, selected_lists > 0 && all_lists_removable);
  gtk_widget_set_sensitive (panel->rename_button, selected_lists == 1);
}

static void
gtd_list_selector_panel_list_selected (GtdListSelector      *selector,
                                       GtdTaskList          *list,
                                       GtdListSelectorPanel *panel)
{
  GtdWindow *window;
  GdkRGBA *list_color;

  window = GTD_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (panel)));

  switch (gtd_window_get_mode (window))
    {
    case GTD_WINDOW_MODE_SELECTION:
      update_action_bar_buttons (panel);
      break;

    case GTD_WINDOW_MODE_NORMAL:
      list_color = gtd_task_list_get_color (list);

      g_signal_handlers_block_by_func (panel->color_button,
                                       gtd_list_selector_panel_list_color_set,
                                       panel);

      gtk_color_chooser_set_rgba (GTK_COLOR_CHOOSER (panel->color_button), list_color);

      gtk_stack_set_visible_child_name (GTK_STACK (panel), "tasks");
      gtk_search_bar_set_search_mode (GTK_SEARCH_BAR (panel->search_bar), FALSE);
      gtd_task_list_view_set_task_list (GTD_TASK_LIST_VIEW (panel->tasklist_view), list);
      gtd_task_list_view_set_show_completed (GTD_TASK_LIST_VIEW (panel->tasklist_view), FALSE);
      gtk_widget_hide (panel->search_button);
      gtk_widget_hide (panel->selection_button);
      gtk_widget_hide (panel->view_button);
      gtk_widget_show (panel->back_button);
      gtk_widget_show (panel->color_button);

      gtd_window_set_custom_title (window,
                                   gtd_task_list_get_name (list),
                                   gtd_provider_get_description (gtd_task_list_get_provider (list)));

      g_signal_handlers_unblock_by_func (panel->color_button,
                                         gtd_list_selector_panel_list_color_set,
                                         panel);

      gdk_rgba_free (list_color);

      g_object_notify (G_OBJECT (panel), "menu");
      break;

    default:
      g_assert_not_reached ();
    }
}

static void
gtd_list_selector_panel_back_button_clicked (GtkButton            *button,
                                             GtdListSelectorPanel *panel)
{
  GtdWindow *window;

  window = GTD_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (panel)));

  gtk_stack_set_visible_child_name (GTK_STACK (panel), "lists");
  gtk_widget_show (panel->search_button);
  gtk_widget_show (panel->selection_button);
  gtk_widget_show (panel->view_button);
  gtk_widget_hide (panel->back_button);
  gtk_widget_hide (panel->color_button);

  gtd_window_set_custom_title (window, NULL, NULL);

  gtd_task_list_view_set_task_list (GTD_TASK_LIST_VIEW (panel->tasklist_view), NULL);

  g_object_notify (G_OBJECT (panel), "menu");
}

static void
gtd_list_selector_panel_rename_entry_text_changed (GObject              *object,
                                                   GParamSpec           *pspec,
                                                   GtdListSelectorPanel *panel)
{
  gtk_widget_set_sensitive (panel->save_rename_button,
                            gtk_entry_get_text_length (GTK_ENTRY (object)) > 0);
}

static void
gtd_list_selector_panel_rename_task_list (GtdListSelectorPanel *panel)
{
  GList *selection;
  GtdManager *manager;

  /*
   * If the save_rename_button is insensitive, the list name is
   * empty and cannot be saved.
   */
  if (!gtk_widget_get_sensitive (panel->save_rename_button))
    return;
  
  manager = gtd_manager_get_default ();
  selection = gtd_list_selector_get_selected_lists (panel->active_selector);

  if (selection && selection->data)
    {
      GtdListSelectorItem *item;
      GtdTaskList *list;
      GtdWindow *window;

      item = selection->data;
      list = gtd_list_selector_item_get_list (item);
      window = GTD_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (panel)));

      gtd_task_list_set_name (list, gtk_entry_get_text (GTK_ENTRY (panel->rename_entry)));
      gtd_window_set_mode (window, GTD_WINDOW_MODE_NORMAL);
      gtd_manager_save_task_list (manager, list);

      gtk_widget_hide (panel->rename_popover);
    }

  g_list_free (selection);
}

static void
gtd_list_selector_panel_rename_button_clicked (GtdListSelectorPanel *panel)
{
  GList *selection;

  selection = gtd_list_selector_get_selected_lists (panel->active_selector);

  if (selection && selection->data)
    {
      GtdListSelectorItem *item;
      GtdTaskList *list;

      item = selection->data;
      list = gtd_list_selector_item_get_list (item);

      gtk_popover_set_relative_to (GTK_POPOVER (panel->rename_popover), GTK_WIDGET (item));
      gtk_entry_set_text (GTK_ENTRY (panel->rename_entry), gtd_task_list_get_name (list));
      gtk_widget_show (panel->rename_popover);

      gtk_widget_grab_focus (panel->rename_entry);
    }

  g_list_free (selection);
}

static void
gtd_list_selector_panel_delete_button_clicked (GtdListSelectorPanel *panel)
{
  GtkWindow *toplevel;
  GtkWidget *dialog;
  GtkWidget *button;
  GList *children;
  GList *l;
  gint response;

  toplevel = GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (panel)));

  dialog = gtk_message_dialog_new (toplevel,
                                   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_USE_HEADER_BAR,
                                   GTK_MESSAGE_QUESTION,
                                   GTK_BUTTONS_NONE,
                                   _("Remove the selected task lists?"));

  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                            _("Once removed, the task lists cannot be recovered."));

  /* Focus the Cancel button by default */
  gtk_dialog_add_button (GTK_DIALOG (dialog),
                         _("Cancel"),
                         GTK_RESPONSE_CANCEL);

  button = gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);
  gtk_widget_grab_focus (button);

  /* Make the Remove button visually destructive */
  gtk_dialog_add_button (GTK_DIALOG (dialog),
                         _("Remove task lists"),
                         GTK_RESPONSE_ACCEPT);

  button = gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);
  gtk_style_context_add_class (gtk_widget_get_style_context (button), "destructive-action");

  response = gtk_dialog_run (GTK_DIALOG (dialog));

  /* Remove selected lists */
  if (response == GTK_RESPONSE_ACCEPT)
    {
      children = gtd_list_selector_get_selected_lists (panel->active_selector);

      for (l = children; l != NULL; l = l->next)
        {
          GtdTaskList *list;

          list = gtd_list_selector_item_get_list (l->data);

          if (gtd_task_list_is_removable (list))
            gtd_manager_remove_task_list (gtd_manager_get_default (), list);
        }

      g_list_free (children);
    }

  gtk_widget_destroy (dialog);

  /* After removing the lists, exit SELECTION mode */
  gtd_window_set_mode (GTD_WINDOW (toplevel), GTD_WINDOW_MODE_NORMAL);
}

/***********************
 * GtdPanel iface init *
 ***********************/
static GList*
gtd_list_selector_panel_get_header_widgets (GtdPanel *panel)
{
  GtdListSelectorPanel *self = GTD_LIST_SELECTOR_PANEL (panel);
  GList *widgets;

  widgets = g_list_append (NULL, self->search_button);
  widgets = g_list_append (widgets, self->selection_button);
  widgets = g_list_append (widgets, self->color_button);
  widgets = g_list_append (widgets, self->view_button);
  widgets = g_list_append (widgets, self->back_button);
  widgets = g_list_append (widgets, self->new_list_button);

  return widgets;
}

static const GMenu*
gtd_list_selector_panel_get_menu (GtdPanel *panel)
{
  if (g_strcmp0 (gtk_stack_get_visible_child_name (GTK_STACK (panel)), "lists") == 0)
    {
      return NULL;
    }

  return GTD_LIST_SELECTOR_PANEL (panel)->menu;
}

static const gchar*
gtd_list_selector_panel_get_panel_name (GtdPanel *panel)
{
  return "panel-lists";
}

static const gchar*
gtd_list_selector_panel_get_panel_title (GtdPanel *panel)
{
  return _("Lists");
}

static void
gtd_panel_iface_init (GtdPanelInterface *iface)
{
  iface->get_header_widgets = gtd_list_selector_panel_get_header_widgets;
  iface->get_menu = gtd_list_selector_panel_get_menu;
  iface->get_panel_name = gtd_list_selector_panel_get_panel_name;
  iface->get_panel_title = gtd_list_selector_panel_get_panel_title;
}

static void
gtd_list_selector_panel_finalize (GObject *object)
{
  GtdListSelectorPanel *self = GTD_LIST_SELECTOR_PANEL (object);

  g_clear_object (&self->menu);

  G_OBJECT_CLASS (gtd_list_selector_panel_parent_class)->finalize (object);
}

static void
gtd_list_selector_panel_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  GtdPanel *self = GTD_PANEL (object);

  switch (prop_id)
    {
    case PROP_MODE:
      g_value_set_enum (value, GTD_LIST_SELECTOR_PANEL (object)->mode);
      break;

    case PROP_MENU:
      g_value_set_object (value, (GMenu*) gtd_list_selector_panel_get_menu (self));
      break;

    case PROP_NAME:
      g_value_set_string (value, gtd_list_selector_panel_get_panel_name (self));
      break;

    case PROP_TITLE:
      g_value_set_string (value, gtd_list_selector_panel_get_panel_title (self));
      break;

    case PROP_VIEW:
      g_value_set_enum (value, GTD_LIST_SELECTOR_PANEL (self)->active_view);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_list_selector_panel_set_mode (GtdListSelectorPanel *panel,
                                  GtdWindowMode         mode)
{
  if (panel->mode != mode)
    {
      gboolean is_selection;

      panel->mode = mode;

      is_selection = mode == GTD_WINDOW_MODE_SELECTION;
      gtk_widget_set_visible (panel->actionbar, is_selection);
      gtk_widget_set_visible (panel->new_list_button, !is_selection);
      gtk_widget_set_visible (panel->view_button, !is_selection);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (panel->selection_button), is_selection);

      update_action_bar_buttons (panel);

      g_object_notify (G_OBJECT (panel), "mode");
    }
}

static void
gtd_list_selector_panel_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  GtdListSelectorPanel *self = GTD_LIST_SELECTOR_PANEL (object);

  switch (prop_id)
    {
    case PROP_MODE:
      gtd_list_selector_panel_set_mode (self, g_value_get_enum (value));
      break;

    case PROP_VIEW:
      gtd_list_selector_panel_set_view (self, g_value_get_enum (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_list_selector_panel_class_init (GtdListSelectorPanelClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = gtd_list_selector_panel_get_property;
  object_class->set_property = gtd_list_selector_panel_set_property;
  object_class->finalize = gtd_list_selector_panel_finalize;

  g_object_class_override_property (object_class, PROP_MENU, "menu");
  g_object_class_override_property (object_class, PROP_NAME, "name");
  g_object_class_override_property (object_class, PROP_TITLE, "title");

  /**
   * GtdListSelectorPanel::mode:
   *
   * This is a utility property so we can chain the "mode"
   * property all the way up/down from window to the list
   * items.
   */
  g_object_class_install_property (object_class,
                                   PROP_MODE,
                                   g_param_spec_enum ("mode",
                                                      "Mode of the selector",
                                                      "The mode of the selector",
                                                      GTD_TYPE_WINDOW_MODE,
                                                      GTD_WINDOW_MODE_NORMAL,
                                                      G_PARAM_READWRITE));

  /**
   * GtdListSelectorPanel::view:
   *
   * Which view is the current view (list or grid).
   */
  g_object_class_install_property (object_class,
                                   PROP_VIEW,
                                   g_param_spec_enum ("view",
                                                      "View of the selector",
                                                      "The current view of the selector",
                                                      GTD_TYPE_LIST_SELECTOR_VIEW_TYPE,
                                                      GTD_LIST_SELECTOR_VIEW_GRID,
                                                      G_PARAM_READWRITE));

  g_type_ensure (GTD_TYPE_TASK_LIST_VIEW);
  g_type_ensure (GTD_TYPE_PROVIDER_POPOVER);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/todo/ui/list-selector-panel.ui");

  gtk_widget_class_bind_template_child (widget_class, GtdListSelectorPanel, actionbar);
  gtk_widget_class_bind_template_child (widget_class, GtdListSelectorPanel, back_button);
  gtk_widget_class_bind_template_child (widget_class, GtdListSelectorPanel, color_button);
  gtk_widget_class_bind_template_child (widget_class, GtdListSelectorPanel, delete_button);
  gtk_widget_class_bind_template_child (widget_class, GtdListSelectorPanel, new_list_button);
  gtk_widget_class_bind_template_child (widget_class, GtdListSelectorPanel, rename_button);
  gtk_widget_class_bind_template_child (widget_class, GtdListSelectorPanel, rename_entry);
  gtk_widget_class_bind_template_child (widget_class, GtdListSelectorPanel, rename_popover);
  gtk_widget_class_bind_template_child (widget_class, GtdListSelectorPanel, save_rename_button);
  gtk_widget_class_bind_template_child (widget_class, GtdListSelectorPanel, search_bar);
  gtk_widget_class_bind_template_child (widget_class, GtdListSelectorPanel, search_button);
  gtk_widget_class_bind_template_child (widget_class, GtdListSelectorPanel, search_entry);
  gtk_widget_class_bind_template_child (widget_class, GtdListSelectorPanel, selection_button);
  gtk_widget_class_bind_template_child (widget_class, GtdListSelectorPanel, stack);
  gtk_widget_class_bind_template_child (widget_class, GtdListSelectorPanel, tasklist_view);
  gtk_widget_class_bind_template_child (widget_class, GtdListSelectorPanel, view_button);
  gtk_widget_class_bind_template_child (widget_class, GtdListSelectorPanel, view_button_image);

  gtk_widget_class_bind_template_callback (widget_class, gtd_list_selector_panel_back_button_clicked);
  gtk_widget_class_bind_template_callback (widget_class, gtd_list_selector_panel_delete_button_clicked);
  gtk_widget_class_bind_template_callback (widget_class, gtd_list_selector_panel_list_color_set);
  gtk_widget_class_bind_template_callback (widget_class, gtd_list_selector_panel_on_key_press_event);
  gtk_widget_class_bind_template_callback (widget_class, gtd_list_selector_panel_rename_button_clicked);
  gtk_widget_class_bind_template_callback (widget_class, gtd_list_selector_panel_rename_entry_text_changed);
  gtk_widget_class_bind_template_callback (widget_class, gtd_list_selector_panel_rename_task_list);
  gtk_widget_class_bind_template_callback (widget_class, gtd_list_selector_panel_select_button_toggled);
  gtk_widget_class_bind_template_callback (widget_class, gtd_list_selector_panel_switch_view);
}

static void
setup_panel (GtdListSelectorPanel *self,
             GtkWidget            *selector,
             const gchar          *stack_name,
             const gchar          *stack_title)
{

  g_object_bind_property (self,
                          "mode",
                          selector,
                          "mode",
                          G_BINDING_BIDIRECTIONAL);

  g_object_bind_property (self->search_entry,
                          "text",
                          selector,
                          "search-query",
                          G_BINDING_BIDIRECTIONAL);

  g_signal_connect (selector,
                    "list-selected",
                    G_CALLBACK (gtd_list_selector_panel_list_selected),
                    self);

  gtk_stack_add_titled (GTK_STACK (self->stack),
                        selector,
                        stack_name,
                        stack_title);
}

static void
gtd_list_selector_panel_init (GtdListSelectorPanel *self)
{
  GSettings *settings;

  gtk_widget_init_template (GTK_WIDGET (self));

  /* Grid selector */
  self->grid_selector = gtd_list_selector_grid_new ();

  setup_panel (self,
               self->grid_selector,
               "grid",
               "Grid");

  /* List selector */
  self->list_selector = gtd_list_selector_list_new ();

  setup_panel (self,
               self->list_selector,
               "list",
               "List");

  /* Load last active view */
  settings = gtd_manager_get_settings (gtd_manager_get_default ());

  gtd_list_selector_panel_set_view (self, g_settings_get_enum (settings, "view"));

  /* Menu */
  self->menu = g_menu_new ();
  g_menu_append (self->menu,
                 _("Clear completed tasks…"),
                 "list.clear-completed-tasks");
}

GtkWidget*
gtd_list_selector_panel_new (void)
{
  return g_object_new (GTD_TYPE_LIST_SELECTOR_PANEL, NULL);
}

