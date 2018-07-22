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

#include "gtd-todoist-oauth2.h"
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include <glib/gi18n.h>

struct _GtdTodoistOAuth2
{
  GtkWindow            parent;
  GtkWidget           *browser;
};

G_DEFINE_TYPE (GtdTodoistOAuth2, gtd_todoist_oauth2, GTK_TYPE_WINDOW)

GtdTodoistOAuth2*
gtd_todoist_oauth2_new (void)
{

  return g_object_new (GTD_TYPE_TODOIST_OAUTH2,
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

/*
 * Callbacks
 */

static gboolean
on_browser_closed (WebKitWebView *webview,
                   GtkWindow     *self)
{
  gtk_widget_destroy(GTK_WIDGET (self));

  return TRUE;
}

static void
on_auth_destroy (GtkWidget *window,
                 GtkWidget *widget)
{
  //spawn_goa_with_args (NULL, NULL);
}

static void
web_view_load_changed (WebKitWebView  *web_view,
                       WebKitLoadEvent load_event,
                       gpointer        user_data)
{
    switch (load_event)
      {
        case WEBKIT_LOAD_STARTED:
            /* New load, we have now a provisional URI */
            //provisional_uri = webkit_web_view_get_uri (web_view);
            /* Here we could start a spinner or update the
             * location bar with the provisional URI */
            break;
        case WEBKIT_LOAD_REDIRECTED:
            //redirected_uri = webkit_web_view_get_uri (web_view);
            break;
        case WEBKIT_LOAD_COMMITTED:
            /* The load is being performed. Current URI is
             * the final one and it won't change unless a new
             * load is requested or a navigation within the
             * same page is performed */
            //uri = webkit_web_view_get_uri (web_view);
            break;
        case WEBKIT_LOAD_FINISHED:
            /* Load finished, we can now stop the spinner */
            break;
    }
}

static void
gtd_todoist_oauth2_finalize (GObject *object)
{
  GtdTodoistOAuth2 *self = (GtdTodoistOAuth2 *)object;

  g_object_unref (self->browser);

  G_OBJECT_CLASS (gtd_todoist_oauth2_parent_class)->finalize (object);
}

static void
gtd_todoist_oauth2_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
gtd_todoist_oauth2_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
gtd_todoist_oauth2_class_init (GtdTodoistOAuth2Class *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gtd_todoist_oauth2_finalize;
  object_class->get_property = gtd_todoist_oauth2_get_property;
  object_class->set_property = gtd_todoist_oauth2_set_property;

    /* template class */
  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/todo/ui/todoist/oauth.ui");

  gtk_widget_class_bind_template_child (widget_class, GtdTodoistOAuth2, browser);

  gtk_widget_class_bind_template_callback (widget_class, on_browser_closed);
  gtk_widget_class_bind_template_callback (widget_class, on_auth_destroy);
}

static void
gtd_todoist_oauth2_init (GtdTodoistOAuth2 *self)
{
  self->browser = webkit_web_view_new ();
  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_widget_grab_focus(GTK_WIDGET(self->browser));

  g_signal_connect (self->browser, "load-changed", G_CALLBACK (web_view_load_changed), self);

  webkit_web_view_load_uri (WEBKIT_WEB_VIEW (self->browser), build_authorization_uri ());
}
