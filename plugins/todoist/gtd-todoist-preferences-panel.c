/* gtd-todoist-preferences-panel.c
 *
 * Copyright (C) 2017 Rohit Kaushik <kaushikrohit325@gmail.com>
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

#define G_LOG_DOMAIN  "GtdTodoistPreferencesPanel"
#define AUTH_ENDPOINT "https://todoist.com/oauth/authorize"
#define CLIENT_ID     "4071c73e4c494ade8112acd307d3efa5"

#include "gtd-todoist-preferences-panel.h"
#include <webkit2/webkit2.h>
#include <gtk/gtk.h>

#include <glib/gi18n.h>

struct _GtdTodoistPreferencesPanel
{
  GtkStack            parent;

  GoaClient          *client;
  GtkWidget          *accounts_listbox;
  GtkWidget          *add_button;
  GtkWidget          *browser;
  GtkWidget          *accounts_page;
  GtkWidget          *empty_page;
  GtkWidget          *login_page;
};

G_DEFINE_TYPE (GtdTodoistPreferencesPanel, gtd_todoist_preferences_panel, GTK_TYPE_STACK)

GtdTodoistPreferencesPanel*
gtd_todoist_preferences_panel_new (void)
{

  return g_object_new (GTD_TYPE_TODOIST_PREFERENCES_PANEL,
                       NULL);
}

static gchar *
build_authorization_uri ()
{
  gchar *state;
  gchar *uri;
  gchar *scope;

  scope = "data:read_write,data:delete,task:add,project:delete";
  state = g_uuid_string_random ();
  uri = g_strdup_printf ("%s"
                         "?response_type=token"
                         "&client_id=%s"
                         "&scope=%s"
                         "&state=%s",
                         AUTH_ENDPOINT,
                         CLIENT_ID,
                         scope,
                         state);

  g_free (state);
  return uri;
}

static void
add_account_button_clicked (GtdTodoistPreferencesPanel *self)
{
  gtk_stack_set_visible_child (GTK_STACK (self), self->login_page);

  webkit_web_view_load_uri (WEBKIT_WEB_VIEW (self->browser), build_authorization_uri ());
}

static void
account_row_clicked_cb (GtkListBox                 *box,
                        GtkListBoxRow              *row,
                        GtdTodoistPreferencesPanel *self)
{
  //spawn_goa_with_args (NULL, NULL);
}

static void
on_goa_account_added (GoaClient                   *client,
                      GoaObject                   *object,
                      GtdTodoistPreferencesPanel  *self)
{
  GoaAccount *goa_account;
  GtkWidget *row;
  GtkWidget *box;
  GtkWidget *logo;
  GtkWidget *desc;
  const gchar *provider_name;
  const gchar *identity;

  goa_account = goa_object_get_account (object);
  provider_name = goa_account_get_provider_name (goa_account);

  if (g_strcmp0 (provider_name, "Todoist") != 0)
    return;

  identity = goa_account_get_presentation_identity (goa_account);
  row = gtk_list_box_row_new ();

  g_object_set_data_full (G_OBJECT (row), "goa-object", g_object_ref (object), g_object_unref);

  logo = gtk_image_new_from_icon_name ("org.gnome.Todo", GTK_ICON_SIZE_LARGE_TOOLBAR);
  desc = gtk_label_new (identity);
  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);

  gtk_container_set_border_width (GTK_CONTAINER (box), 6);
  gtk_container_add (GTK_CONTAINER (box), logo);
  gtk_container_add (GTK_CONTAINER (box), desc);
  gtk_container_add (GTK_CONTAINER (row), box);

  gtk_widget_show_all (row);

  gtk_stack_set_visible_child (GTK_STACK (self), self->accounts_page);

  gtk_list_box_insert (GTK_LIST_BOX (self->accounts_listbox), GTK_WIDGET (row), -1);
}

static void
on_goa_account_removed (GoaClient                   *client,
                        GoaObject                   *object,
                        GtdTodoistPreferencesPanel  *self)
{
  GoaAccount *goa_account;
  GList *child;
  GList *l;
  const gchar *provider;
  guint todoist_accounts;

  goa_account = goa_object_get_account (object);
  provider = goa_account_get_provider_name (goa_account);
  child = NULL;
  l = NULL;

  if (g_strcmp0 (provider, "Todoist") != 0)
    return;

  child = gtk_container_get_children (GTK_CONTAINER (self->accounts_listbox));
  todoist_accounts = g_list_length (child);

  for (l = child; l != NULL; l = l->next)
    {
      GoaObject *row_account;

      row_account = GOA_OBJECT (g_object_get_data (G_OBJECT (l->data), "goa-object"));

      if (row_account == object)
        {
          gtk_container_remove (GTK_CONTAINER (self->accounts_listbox),l->data);
          todoist_accounts--;
          break;
        }
    }

  /* Change to empty_page if the listbox becomes empty after this removal */
  if (!todoist_accounts)
    gtk_stack_set_visible_child (GTK_STACK (self), self->empty_page);

  g_list_free (child);
}

void
gtd_todoist_preferences_panel_set_client (GtdTodoistPreferencesPanel *self,
                                          GoaClient                  *client)
{
  GList *accounts;
  GList *l;

  accounts = NULL;
  l = NULL;

  self->client = client;

  accounts = goa_client_get_accounts (self->client);

  for (l = accounts; l != NULL; l = l->next)
    on_goa_account_added (self->client, l->data, self);

  g_signal_connect (self->client,
                    "account-added",
                    G_CALLBACK (on_goa_account_added),
                    self);

  g_signal_connect (self->client,
                    "account-removed",
                    G_CALLBACK (on_goa_account_removed),
                    self);

  g_list_free_full (accounts,  g_object_unref);
}

static void
gtd_todoist_preferences_panel_finalize (GObject *object)
{
  GtdTodoistPreferencesPanel *self = (GtdTodoistPreferencesPanel *)object;

  g_object_unref (self->client);

  G_OBJECT_CLASS (gtd_todoist_preferences_panel_parent_class)->finalize (object);
}

static void
gtd_todoist_preferences_panel_get_property (GObject    *object,
                                            guint       prop_id,
                                            GValue     *value,
                                            GParamSpec *pspec)
{
  G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
gtd_todoist_preferences_panel_set_property (GObject      *object,
                                            guint         prop_id,
                                            const GValue *value,
                                            GParamSpec   *pspec)
{
  G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
gtd_todoist_preferences_panel_class_init (GtdTodoistPreferencesPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gtd_todoist_preferences_panel_finalize;
  object_class->get_property = gtd_todoist_preferences_panel_get_property;
  object_class->set_property = gtd_todoist_preferences_panel_set_property;

    /* template class */
  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/todo/ui/todoist/preferences.ui");

  gtk_widget_class_bind_template_child (widget_class, GtdTodoistPreferencesPanel, accounts_listbox);
  gtk_widget_class_bind_template_child (widget_class, GtdTodoistPreferencesPanel, add_button);
  gtk_widget_class_bind_template_child (widget_class, GtdTodoistPreferencesPanel, browser);
  gtk_widget_class_bind_template_child (widget_class, GtdTodoistPreferencesPanel, accounts_page);
  gtk_widget_class_bind_template_child (widget_class, GtdTodoistPreferencesPanel, empty_page);
  gtk_widget_class_bind_template_child (widget_class, GtdTodoistPreferencesPanel, login_page);

  gtk_widget_class_bind_template_callback (widget_class, account_row_clicked_cb);
}

static void
gtd_todoist_preferences_panel_init (GtdTodoistPreferencesPanel *self)
{
  GtkWidget *label;

  self->browser = webkit_web_view_new ();

  gtk_widget_init_template (GTK_WIDGET (self));

  /* Set Empty Page as the default initial preferences page */
  gtk_stack_set_visible_child (GTK_STACK (self), self->empty_page);

  label = gtk_label_new ("No Todoist account configured");
  gtk_widget_show (label);
  gtk_list_box_set_placeholder (GTK_LIST_BOX (self->accounts_listbox), GTK_WIDGET (label));

  g_signal_connect_swapped (self->add_button, "clicked", G_CALLBACK (add_account_button_clicked), self);
}
