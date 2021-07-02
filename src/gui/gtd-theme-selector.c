/* gtd-theme-selector.c
 *
 * Copyright 2021 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "gtd-manager.h"
#include "gtd-theme-selector.h"

struct _GtdThemeSelector
{
  GtkWidget           parent;

  GtkToggleButton    *dark;
  GtkToggleButton    *light;

  gchar              *theme;
};

G_DEFINE_TYPE (GtdThemeSelector, gtd_theme_selector, GTK_TYPE_WIDGET)

enum
{
  PROP_0,
  PROP_THEME,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

/*
 * Auxiliary methods
 */

static gboolean
style_variant_to_boolean (GValue   *value,
                          GVariant *variant,
                          gpointer  user_data)
{
  gboolean is_dark = g_strcmp0 (g_variant_get_string (variant, NULL), "dark") == 0;

  g_value_set_boolean (value, is_dark);

  return TRUE;
}

static void
setup_action (GtdThemeSelector *self)
{
  g_autoptr (GSimpleActionGroup) group = NULL;
  g_autoptr (GAction) action = NULL;
  GtkSettings *gtk_settings;
  GtdManager *manager;
  GSettings *settings;
  gboolean is_dark;

  manager = gtd_manager_get_default ();
  settings = gtd_manager_get_settings (manager);

  self->theme = g_settings_get_string (settings, "style-variant");
  is_dark = g_strcmp0 (self->theme, "dark") == 0;
  gtk_settings = gtk_settings_get_default ();
  g_object_set (gtk_settings,
                "gtk-application-prefer-dark-theme", is_dark,
                NULL);

  group = g_simple_action_group_new ();
  action = g_settings_create_action (settings, "style-variant");
  g_action_map_add_action (G_ACTION_MAP (group), action);

  gtk_widget_insert_action_group (GTK_WIDGET (self),
                                  "settings",
                                  G_ACTION_GROUP (group));

  g_settings_bind_with_mapping (settings,
                                "style-variant",
                                gtk_settings,
                                "gtk-application-prefer-dark-theme",
                                G_SETTINGS_BIND_GET,
                                style_variant_to_boolean,
                                NULL, NULL, NULL);
}

/*
 * GObject overrides
 */
static void
gtd_theme_selector_dispose (GObject *object)
{
  GtdThemeSelector *self = (GtdThemeSelector *)object;

  g_clear_pointer (&self->theme, g_free);

  G_OBJECT_CLASS (gtd_theme_selector_parent_class)->dispose (object);
}

static void
gtd_theme_selector_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  GtdThemeSelector *self = GTD_THEME_SELECTOR (object);

  switch (prop_id)
    {
    case PROP_THEME:
      g_value_set_string (value, gtd_theme_selector_get_theme (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_theme_selector_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  GtdThemeSelector *self = GTD_THEME_SELECTOR (object);

  switch (prop_id)
    {
    case PROP_THEME:
      gtd_theme_selector_set_theme (self, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_theme_selector_class_init (GtdThemeSelectorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = gtd_theme_selector_dispose;
  object_class->get_property = gtd_theme_selector_get_property;
  object_class->set_property = gtd_theme_selector_set_property;

  properties [PROP_THEME] =
    g_param_spec_string ("theme",
                         "Theme",
                         "Theme",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_install_property_action (widget_class, "theme.mode", "theme");

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/todo/ui/gtd-theme-selector.ui");

  gtk_widget_class_bind_template_child (widget_class, GtdThemeSelector, dark);
  gtk_widget_class_bind_template_child (widget_class, GtdThemeSelector, light);

  gtk_widget_class_set_css_name (widget_class, "themeselector");

  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
}

static void
gtd_theme_selector_init (GtdThemeSelector *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  setup_action (self);
}

GtkWidget *
gtd_theme_selector_new (void)
{
  return g_object_new (GTD_TYPE_THEME_SELECTOR, NULL);
}

const gchar *
gtd_theme_selector_get_theme (GtdThemeSelector *self)
{
  g_return_val_if_fail (GTD_IS_THEME_SELECTOR (self), NULL);

  return self->theme;
}

void
gtd_theme_selector_set_theme (GtdThemeSelector *self,
                              const gchar      *theme)
{
  g_return_if_fail (GTD_IS_THEME_SELECTOR (self));

  if (g_strcmp0 (theme, self->theme) == 0)
    return;

  g_clear_pointer (&self->theme, g_free);
  self->theme = g_strdup (theme);
  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_THEME]);
}
