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
#define CLIENT_SECRET "abfa818fbba14c9195f1b6d7d534ba5b"
#define AUTH_URL      "https://todoist.com/oauth/permission_request"
#define REDIRECT_URL  "wiki.gnome.org/Apps/Todo"
#define ACCESS_URL    "https://todoist.com/oauth/access_token"

#include "gtd-todoist-preferences-panel.h"

#include <rest/oauth2-proxy.h>
#include <webkit2/webkit2.h>
#include <json-glib/json-glib.h>
#include <libsecret/secret.h>
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

enum
{
  ACCOUNT_ADDED,
  ACCOUNT_REMOVED,
  NUM_SIGNALS
};

static guint signals[NUM_SIGNALS] = { 0, };

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
parse_json_from_response (JsonParser    *parser,
                          RestProxyCall *call)
{
  g_autoptr (GError) error = NULL;
  const gchar *payload;
  gsize payload_length;

  /* Parse the JSON response */
  payload = rest_proxy_call_get_payload (call);
  payload_length = rest_proxy_call_get_payload_length (call);

  if (!json_parser_load_from_data (parser, payload, payload_length, &error))
    {
      gtd_manager_emit_error_message (gtd_manager_get_default (),
                                      _("An error occurred while updating a Todoist task"),
                                      error->message,
                                      NULL,
                                      NULL);

      g_warning ("Error executing request: %s", error->message);
      return;
    }

  /* Print the response if tracing is enabled */
#ifdef GTD_ENABLE_TRACE
  {
    g_autoptr (JsonGenerator) generator;

    generator = g_object_new (JSON_TYPE_GENERATOR,
                              "root", json_parser_get_root (parser),
                              "pretty", TRUE,
                              NULL);

    g_debug ("Response: \n%s", json_generator_to_data (generator, NULL));
  }
#endif
}

static void
identify_user (GtdTodoistPreferencesPanel *self,
               const gchar                *access_token)
{
  g_autoptr (JsonParser) parser = NULL;
  g_autoptr (JsonObject) object = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree gchar *user_name;
  RestProxyCall *call;
  RestProxy *proxy;
  gint user_id;


  proxy = rest_proxy_new (ACCESS_URL, FALSE);
  call = rest_proxy_new_call (proxy);
  parser = json_parser_new ();

  rest_proxy_call_set_method (call, "POST");
  rest_proxy_call_add_header (call, "content-type", "application/x-www-form-urlencoded");

  rest_proxy_call_add_param (call, "token", access_token);
  rest_proxy_call_add_param (call, "sync_token", "*");
  rest_proxy_call_add_param (call, "resource_types", "[\"user\"]");

  rest_proxy_call_sync (call, &error);

  parse_json_from_response (parser, call);
  object = json_node_get_object (json_parser_get_root (parser));

  user_id = json_object_get_int_member (object, "id");
  user_name = json_object_get_string_member (object, "full_name");
}

static void
store_token (GtdTodoistPreferencesPanel *self,
             const gchar                *authorization_code,
             const gchar                *access_token)
{
  g_autofree gchar *format_password;
  g_autoptr (GError) error = NULL;

  static const SecretSchema todoist_schema = {
        "org.example.Password", SECRET_SCHEMA_NONE,
        {
            {  "type", SECRET_SCHEMA_ATTRIBUTE_STRING },
            {  "id", SECRET_SCHEMA_ATTRIBUTE_STRING },
            {  "name", SECRET_SCHEMA_ATTRIBUTE_STRING },
            {  "NULL", 0 },
        }
    };

  format_password = g_strdup_printf ("{'authorization_code':<'%s'>, 'access_token':<'%s'>}", authorization_code, access_token);

  secret_password_store_sync (&todoist_schema, SECRET_COLLECTION_DEFAULT,
                              "The label", "the password", NULL, &error,
                              "type", "Todoist",
                              "id", "123",
                              "name", "rohit",
                              NULL);
}

static void
get_access_token (GtdTodoistPreferencesPanel *self,
                  const gchar                *authorization_code)
{
  g_autoptr (JsonParser) parser = NULL;
  g_autoptr (JsonObject) object = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree gchar *access_token;
  RestProxyCall *call;
  RestProxy *proxy;

  proxy = rest_proxy_new (ACCESS_URL, FALSE);
  call = rest_proxy_new_call (proxy);
  parser = json_parser_new ();

  rest_proxy_call_set_method (call, "POST");
  rest_proxy_call_add_header (call, "content-type", "application/x-www-form-urlencoded");

  rest_proxy_call_add_param (call, "client_id", CLIENT_ID);
  rest_proxy_call_add_param (call, "client_secret", CLIENT_SECRET);
  rest_proxy_call_add_param (call, "code", authorization_code);

  rest_proxy_call_sync (call, &error);

  parse_json_from_response (parser, call);
  object = json_node_get_object (json_parser_get_root (parser));

  if (json_object_has_member (object, "access_token"))
    access_token = json_object_get_string_member (object, "access_token");

  store_token (self, authorization_code, access_token);

  gtk_stack_set_visible_child (GTK_STACK (self), self->empty_page);
}

static void
web_view_load_changed (WebKitWebView  *web_view,
                       WebKitLoadEvent load_event,
                       gpointer        user_data)
{
  GtdTodoistPreferencesPanel *self = user_data;
  GHashTable *key_value_pairs;
  SoupURI *uri;
  const gchar *fragment;
  const gchar *query;
  gchar *url;

  if (load_event != WEBKIT_LOAD_FINISHED)
    return;

  uri = soup_uri_new (webkit_web_view_get_uri (web_view));
  fragment = soup_uri_get_fragment (uri);
  query = soup_uri_get_query (uri);

  /* Waiting for user to authorize To Do */
  if (g_strcmp0 (soup_uri_to_string (uri, FALSE), AUTH_URL) == 0)
    return;


  url = g_strdup_printf ("%s%s", soup_uri_get_host (uri), soup_uri_get_path (uri));

  /* Handle url to get authorization code and exchange for access token */
  if (g_strcmp0 (url, REDIRECT_URL) == 0)
    {
      if (query != NULL)
        {
          gchar *code;
          gchar *state;

          key_value_pairs = soup_form_decode (query);

          code = g_strdup (g_hash_table_lookup (key_value_pairs, "code"));
          state = g_strdup (g_hash_table_lookup (key_value_pairs, "state"));

          g_warning ("%s", code);

          get_access_token (self, code);
        }
    }
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

}

void
gtd_todoist_preferences_panel_set_client (GtdTodoistPreferencesPanel *self,
                                          GoaClient                  *client)
{
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

  signals[ACCOUNT_ADDED] = g_signal_new ("account-added",
                                         GTD_TYPE_TODOIST_PREFERENCES_PANEL,
                                         G_SIGNAL_RUN_LAST,
                                         0,
                                         NULL,
                                         NULL,
                                         NULL,
                                         G_TYPE_NONE,
                                         1,
                                         GTD_TYPE_TASK);

  signals[ACCOUNT_REMOVED] = g_signal_new ("account-removed",
                                           GTD_TYPE_TODOIST_PREFERENCES_PANEL,
                                           G_SIGNAL_RUN_LAST,
                                           0,
                                           NULL,
                                           NULL,
                                           NULL,
                                           G_TYPE_NONE,
                                           1,
                                           GTD_TYPE_TASK);

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
  g_signal_connect (self->browser, "load-changed", G_CALLBACK (web_view_load_changed), self);
}
