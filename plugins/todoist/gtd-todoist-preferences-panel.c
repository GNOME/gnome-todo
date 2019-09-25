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

#define G_LOG_DOMAIN "GtdTodoistPreferencesPanel"

#include "gtd-todoist-preferences-panel.h"
#include <libsecret/secret.h>
#include <rest/oauth2-proxy.h>
#include <json-glib/json-glib.h>

#include "config.h"
#include <glib/gi18n.h>

struct _GtdTodoistPreferencesPanel
{
  GtkBin              parent;

  GtkStack           *stack;
  GoaClient          *client;
  GtkWidget          *accounts_listbox;
  GtkWidget          *add_button;
  GtkWidget          *accounts_page;
  GtkWidget          *empty_page;
};

G_DEFINE_TYPE (GtdTodoistPreferencesPanel, gtd_todoist_preferences_panel, GTK_TYPE_BIN)

GtdTodoistPreferencesPanel*
gtd_todoist_preferences_panel_new (void)
{

  return g_object_new (GTD_TYPE_TODOIST_PREFERENCES_PANEL,
                       NULL);
}

static gboolean
identify_todoist_user (GtdTodoistPreferencesPanel *self,
                       const gchar                *access_token,
                       gchar                     **email,
                       gchar                     **user_name,
                       gint                       *user_id)
{
  g_autoptr (GError) error = NULL;
  RestProxy *proxy = NULL;
  RestProxyCall *call = NULL;
  g_autoptr (JsonObject) json_object = NULL;
  g_autoptr (JsonObject) json_object_user = NULL;
  g_autoptr (JsonParser) parser = NULL;

  proxy = rest_proxy_new ("https://todoist.com/API/v7/sync", FALSE);
  call = rest_proxy_new_call (proxy);
  rest_proxy_call_set_method (call, "GET");
  rest_proxy_call_add_param (call, "token", access_token);
  rest_proxy_call_add_param (call, "sync_token", "*");
  rest_proxy_call_add_param (call, "resource_types", "[\"user\"]");

  if (!rest_proxy_call_sync (call, &error))
    return FALSE;

  if (rest_proxy_call_get_status_code (call) != 200)
    return FALSE;

  parser = json_parser_new ();

  json_object = json_node_get_object (json_parser_get_root (parser));

  json_object_user = json_object_get_object_member (json_object, "user");

  *email = g_strdup (json_object_get_string_member (json_object_user, "email"));
  *user_name = g_strdup (json_object_get_string_member (json_object_user, "user_name"));
  *user_id = json_object_get_int_member (json_object_user, "id");

  g_object_unref (proxy);
  g_object_unref (call);

  return TRUE;
}

static void
store_access_token_cb (GObject      *source,
                       GAsyncResult *result,
                       gpointer      user_data)
{
  GtdTodoistPreferencesPanel *self = user_data;
  g_autoptr (GError) error = NULL;

  secret_password_store_finish (result, &error);
}

static void
store_access_token (GtdTodoistPreferencesPanel *self,
                    const gchar                *authorization_code,
                    const gchar                *access_token)
{
  g_autofree gchar *format_password = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree gchar *email = NULL;
  g_autofree gchar *user_name = NULL;
  gint user_id;

  static const SecretSchema todoist_schema = {
        "org.example.Password", SECRET_SCHEMA_NONE,
        {
            {  "email", SECRET_SCHEMA_ATTRIBUTE_STRING },
            {  "id", SECRET_SCHEMA_ATTRIBUTE_STRING },
            {  "name", SECRET_SCHEMA_ATTRIBUTE_STRING },
            {  "NULL", 0 },
        }
    };

  format_password = g_strdup_printf ("{'authorization_code':<'%s'>, 'access_token':<'%s'>}", authorization_code, access_token);

  identify_todoist_user (self, access_token, &email, &user_name, &user_id);

  secret_password_store (&todoist_schema, SECRET_COLLECTION_DEFAULT,
                        "Todoist account for GNOME To Do", format_password, NULL, store_access_token_cb, NULL,
                        "email", email,
                        "id", user_id,
                        "name", user_name,
                        NULL);
}

static GVariant*
build_dbus_parameters (const gchar *action,
                       const gchar *arg)
{
  GVariantBuilder builder;
  GVariant *array[1], *params2[3];

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("av"));

  if (!action && !arg)
    {
      g_variant_builder_add (&builder, "v", g_variant_new_string (""));
    }
  else
    {
      if (action)
        g_variant_builder_add (&builder, "v", g_variant_new_string (action));

      if (arg)
        g_variant_builder_add (&builder, "v", g_variant_new_string (arg));
    }

  array[0] = g_variant_new ("v", g_variant_new ("(sav)", "online-accounts", &builder));

  params2[0] = g_variant_new_string ("launch-panel");
  params2[1] = g_variant_new_array (G_VARIANT_TYPE ("v"), array, 1);
  params2[2] = g_variant_new_array (G_VARIANT_TYPE ("{sv}"), NULL, 0);

  return g_variant_new_tuple (params2, 3);
}

static void
spawn_goa_with_args (const gchar *action,
                     const gchar *arg)
{
  GDBusProxy *proxy;

  proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                         G_DBUS_PROXY_FLAGS_NONE,
                                         NULL,
                                         "org.gnome.ControlCenter",
                                         "/org/gnome/ControlCenter",
                                         "org.gtk.Actions",
                                         NULL,
                                         NULL);

  if (!proxy)
    {
      g_warning ("Couldn't open Online Accounts panel");
      return;
    }

  g_dbus_proxy_call_sync (proxy,
                          "Activate",
                          build_dbus_parameters (action, arg),
                          G_DBUS_CALL_FLAGS_NONE,
                          -1,
                          NULL,
                          NULL);

  g_clear_object (&proxy);
}

static void
add_account_button_clicked (GtdTodoistPreferencesPanel *self)
{
  g_return_if_fail (GOA_IS_CLIENT (self->client));

  spawn_goa_with_args ("add", "todoist");
}

static void
account_row_clicked_cb (GtkListBox                 *box,
                        GtkListBoxRow              *row,
                        GtdTodoistPreferencesPanel *self)
{
  spawn_goa_with_args (NULL, NULL);
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

  logo = gtk_image_new_from_icon_name (APPLICATION_ID);
  desc = gtk_label_new (identity);
  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);

  gtk_container_add (GTK_CONTAINER (box), logo);
  gtk_container_add (GTK_CONTAINER (box), desc);
  gtk_container_add (GTK_CONTAINER (row), box);

  gtk_stack_set_visible_child (self->stack, self->accounts_page);

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
    gtk_stack_set_visible_child (self->stack, self->empty_page);

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
  gtk_widget_class_bind_template_child (widget_class, GtdTodoistPreferencesPanel, accounts_page);
  gtk_widget_class_bind_template_child (widget_class, GtdTodoistPreferencesPanel, empty_page);
  gtk_widget_class_bind_template_child (widget_class, GtdTodoistPreferencesPanel, stack);

  gtk_widget_class_bind_template_callback (widget_class, account_row_clicked_cb);
}

static void
gtd_todoist_preferences_panel_init (GtdTodoistPreferencesPanel *self)
{
  GtkWidget *label;

  gtk_widget_init_template (GTK_WIDGET (self));

  /* Set Empty Page as the default initial preferences page */
  gtk_stack_set_visible_child (self->stack, self->empty_page);

  label = gtk_label_new ("No Todoist account configured");
  gtk_widget_show (label);
  gtk_list_box_set_placeholder (GTK_LIST_BOX (self->accounts_listbox), GTK_WIDGET (label));

  g_signal_connect_swapped (self->add_button, "clicked", G_CALLBACK (add_account_button_clicked), self);
}
