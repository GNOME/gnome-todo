/* gtd-application.c
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

#define G_LOG_DOMAIN "GtdApplication"

#include "config.h"

#include "gtd-application.h"
#include "gtd-debug.h"
#include "gtd-initial-setup-window.h"
#include "gtd-log.h"
#include "gtd-manager.h"
#include "gtd-manager-protected.h"
#include "gtd-theme-manager.h"
#include "gtd-vcs.h"
#include "gtd-window.h"

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <girepository.h>
#include <glib/gi18n.h>


struct _GtdApplication
{
  GtkApplication         application;

  GtdThemeManager       *theme_manager;

  GtkWindow             *window;
  GtkWidget             *initial_setup;
};

static void           gtd_application_activate_action             (GSimpleAction        *simple,
                                                                   GVariant             *parameter,
                                                                   gpointer              user_data);

static void           gtd_application_start_client                (GSimpleAction        *simple,
                                                                   GVariant             *parameter,
                                                                   gpointer              user_data);

static void           gtd_application_show_about                  (GSimpleAction        *simple,
                                                                   GVariant             *parameter,
                                                                   gpointer              user_data);

static void           gtd_application_quit                        (GSimpleAction        *simple,
                                                                   GVariant             *parameter,
                                                                   gpointer              user_data);

G_DEFINE_TYPE (GtdApplication, gtd_application, GTK_TYPE_APPLICATION)

static GOptionEntry cmd_options[] = {
  { "quit", 'q', 0, G_OPTION_ARG_NONE, NULL, N_("Quit GNOME To Do"), NULL },
  { "debug", 'd', 0, G_OPTION_ARG_NONE, NULL, N_("Enable debug messages"), NULL },
  { NULL }
};

static const GActionEntry gtd_application_entries[] = {
  { "activate", gtd_application_activate_action },
  { "start-client", gtd_application_start_client },
  { "about",  gtd_application_show_about },
  { "quit",   gtd_application_quit }
};

static void
gtd_application_activate_action (GSimpleAction *simple,
                                 GVariant      *parameter,
                                 gpointer       user_data)
{
  GtdApplication *self = GTD_APPLICATION (user_data);

  gtk_window_present (GTK_WINDOW (self->window));
}

static void
gtd_application_start_client (GSimpleAction *simple,
                              GVariant      *parameter,
                              gpointer       user_data)
{
  /* TODO */
  g_debug ("Starting up client");
}

static void
gtd_application_show_about (GSimpleAction *simple,
                            GVariant      *parameter,
                            gpointer       user_data)
{
  GtdApplication *self;

  static const gchar *authors[] = {
    "Emmanuele Bassi <ebassi@gnome.org>",
    "Georges Basile Stavracas Neto <georges.stavracas@gmail.com>",
    "Isaque Galdino <igaldino@gmail.com>",
    "Patrick Griffis <tingping@tingping.se>",
    "Saiful B. Khan <saifulbkhan@gmail.com>",
    NULL
  };

  static const gchar *artists[] = {
    "Allan Day <allanpday@gmail.com>",
    "Jakub Steiner <jimmac@gmail.com>",
    "Tobias Bernard <tbernard@gnome.org>",
    NULL
  };

  self = GTD_APPLICATION (user_data);

  g_autofree gchar *program_name = g_strconcat (_("To Do"), NAME_SUFFIX, NULL);
  gtk_show_about_dialog (GTK_WINDOW (self->window),
                         "program-name", program_name,
                         "version", GTD_VCS_TAG,
                         "copyright", _("Copyright \xC2\xA9 2015–2020 The To Do authors"),
                         "license-type", GTK_LICENSE_GPL_3_0,
                         "authors", authors,
                         "artists", artists,
                         "logo-icon-name", APPLICATION_ID,
                         "translator-credits", _("translator-credits"),
                         NULL);
}

static void
gtd_application_quit (GSimpleAction *simple,
                      GVariant      *parameter,
                      gpointer       user_data)
{
  GtdApplication *self = GTD_APPLICATION (user_data);

  gtk_window_destroy (self->window);
}

GtdApplication *
gtd_application_new (void)
{
  return g_object_new (GTD_TYPE_APPLICATION,
                       "application-id", APPLICATION_ID,
                       "flags", G_APPLICATION_HANDLES_COMMAND_LINE,
                       "resource-base-path", "/org/gnome/todo",
                       NULL);
}

static void
run_window (GtdApplication *self)
{
  gtk_window_present (GTK_WINDOW (self->window));
}

/*
static void
finish_initial_setup (GtdApplication *application)
{
  g_return_if_fail (GTD_IS_APPLICATION (application));

  run_window (application);

  gtd_manager_set_is_first_run (application->priv->manager, FALSE);

  g_clear_pointer (&application->priv->initial_setup, gtk_widget_destroy);
}

static void
run_initial_setup (GtdApplication *application)
{
  GtdApplicationPrivate *priv;

  g_return_if_fail (GTD_IS_APPLICATION (application));

  priv = application->priv;

  if (!priv->initial_setup)
    {
      priv->initial_setup = gtd_initial_setup_window_new (application);

      g_signal_connect (priv->initial_setup,
                        "cancel",
                        G_CALLBACK (gtk_widget_destroy),
                        application);

      g_signal_connect_swapped (priv->initial_setup,
                                "done",
                                G_CALLBACK (finish_initial_setup),
                                application);
    }

  gtk_widget_show (priv->initial_setup);
}
*/

static void
gtd_application_finalize (GObject *object)
{
  GtdApplication *self = (GtdApplication *)object;

  g_clear_object (&self->theme_manager);

  G_OBJECT_CLASS (gtd_application_parent_class)->finalize (object);
}

static void
gtd_application_activate (GApplication *application)
{
  GTD_ENTRY;

  /* FIXME: the initial setup is disabled for the 3.18 release because
   * we can't create tasklists on GOA accounts.
   */
  run_window (GTD_APPLICATION (application));

  GTD_EXIT;
}

static void
gtd_application_startup (GApplication *application)
{
  GtdApplication *self;

  GTD_ENTRY;

  self = GTD_APPLICATION (application);

  /* add actions */
  g_action_map_add_action_entries (G_ACTION_MAP (self),
                                   gtd_application_entries,
                                   G_N_ELEMENTS (gtd_application_entries),
                                   self);

  G_APPLICATION_CLASS (gtd_application_parent_class)->startup (application);

  adw_init ();

  /* CSS provider */
  gtd_theme_manager_add_resources (self->theme_manager, "resource:///org/gnome/todo");

  /* window */
  gtk_window_set_default_icon_name (APPLICATION_ID);
  self->window = GTK_WINDOW (gtd_window_new (self));

  /* Load the plugins */
  gtd_manager_load_plugins (gtd_manager_get_default ());

  GTD_EXIT;
}

static gint
gtd_application_command_line (GApplication            *app,
                              GApplicationCommandLine *command_line)
{
  GVariantDict *options;

  options = g_application_command_line_get_options_dict (command_line);

  if (g_variant_dict_contains (options, "quit"))
    {
      g_application_quit (app);
      return 0;
    }

  g_application_activate (app);

  return 0;
}

static gboolean
gtd_application_local_command_line (GApplication   *application,
                                    gchar        ***arguments,
                                    gint           *exit_status)
{
  g_application_add_option_group (application, g_irepository_get_option_group());

  return G_APPLICATION_CLASS (gtd_application_parent_class)->local_command_line (application,
                                                                                 arguments,
                                                                                 exit_status);
}

static gint
gtd_application_handle_local_options (GApplication *application,
                                      GVariantDict *options)
{
  if (g_variant_dict_contains (options, "debug"))
    gtd_log_init ();

  return -1;
}

static void
gtd_application_class_init (GtdApplicationClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GApplicationClass *application_class = G_APPLICATION_CLASS (klass);

  object_class->finalize = gtd_application_finalize;

  application_class->activate = gtd_application_activate;
  application_class->startup = gtd_application_startup;
  application_class->command_line = gtd_application_command_line;
  application_class->local_command_line = gtd_application_local_command_line;
  application_class->handle_local_options = gtd_application_handle_local_options;
}

static void
gtd_application_init (GtdApplication *self)
{
  self->theme_manager = gtd_theme_manager_new ();

  g_application_add_main_option_entries (G_APPLICATION (self), cmd_options);
}
