/* gtd-sidebar-provider-row.c
 *
 * Copyright 2018 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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

#define G_LOG_DOMAIN "GtdSidebarProviderRow"

#include "gtd-manager.h"
#include "gtd-provider.h"
#include "gtd-sidebar-provider-row.h"

struct _GtdSidebarProviderRow
{
  GtkListBoxRow       parent;

  GtkWidget          *loading_label;
  GtkLabel           *provider_label;
  GtkStack           *stack;

  GtdProvider        *provider;
};

static void          on_provider_changed_cb                      (GtdManager            *manager,
                                                                  GtdProvider           *provider,
                                                                  GtdSidebarProviderRow *self);

static void          on_provider_lists_changed_cb                (GtdProvider           *provider,
                                                                  GtdTaskList           *list,
                                                                  GtdSidebarProviderRow *self);

static void          on_provider_notify_loading_cb               (GtdProvider           *provider,
                                                                  GParamSpec            *pspec,
                                                                  GtdSidebarProviderRow *self);

G_DEFINE_TYPE (GtdSidebarProviderRow, gtd_sidebar_provider_row, GTK_TYPE_LIST_BOX_ROW)

enum
{
  PROP_0,
  PROP_PROVIDER,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];


/*
 * Auxiliary methods
 */

static void
update_provider_label (GtdSidebarProviderRow *self)
{
  g_autoptr (GList) providers = NULL;
  GtdManager *manager;
  GList *l;
  gboolean is_unique;
  const gchar *title;

  manager = gtd_manager_get_default ();
  providers = gtd_manager_get_providers (manager);
  is_unique = TRUE;

  /*
   * We need to check if there is another provider with
   * the same GType of the provider.
   */
  for (l = providers; l; l = l->next)
    {
      GtdProvider *provider = l->data;

      if (self->provider != provider &&
          g_str_equal (gtd_provider_get_provider_type (self->provider), gtd_provider_get_provider_type (provider)))
        {
          is_unique = FALSE;
          break;
        }
    }

  if (is_unique)
    title = gtd_provider_get_name (self->provider);
  else
    title = gtd_provider_get_description (self->provider);

  gtk_label_set_label (self->provider_label, title);
}

static void
update_loading_state (GtdSidebarProviderRow *self)
{
  g_autoptr (GList) lists = NULL;
  gboolean is_loading;
  gboolean has_lists;

  g_assert (self->provider != NULL);

  lists = gtd_provider_get_task_lists (self->provider);
  is_loading = gtd_object_get_loading (GTD_OBJECT (self->provider));
  has_lists = lists != NULL;

  gtk_stack_set_visible_child_name (self->stack, is_loading ? "spinner" : "icon");
  gtk_widget_set_visible (self->loading_label, is_loading && !has_lists);
}

static void
set_provider (GtdSidebarProviderRow *self,
              GtdProvider           *provider)
{
  GtdManager *manager;

  g_assert (provider != NULL);
  g_assert (self->provider == NULL);

  self->provider = g_object_ref (provider);

  /* Setup the title label */
  manager = gtd_manager_get_default ();

  g_signal_connect_object (manager, "provider-added", G_CALLBACK (on_provider_changed_cb), self, 0);
  g_signal_connect_object (manager, "provider-removed", G_CALLBACK (on_provider_changed_cb), self, 0);

  update_provider_label (self);

  /* And the icon */
  g_signal_connect_object (provider, "notify::loading", G_CALLBACK (on_provider_notify_loading_cb), self, 0);
  g_signal_connect_object (provider, "list-added", G_CALLBACK (on_provider_lists_changed_cb), self, 0);
  g_signal_connect_object (provider, "list-removed", G_CALLBACK (on_provider_lists_changed_cb), self, 0);

  update_loading_state (self);
}


/*
 * Callbacks
 */

static void
on_provider_changed_cb (GtdManager            *manager,
                        GtdProvider           *provider,
                        GtdSidebarProviderRow *self)
{
  update_provider_label (self);
}

static void
on_provider_lists_changed_cb (GtdProvider           *provider,
                              GtdTaskList           *list,
                              GtdSidebarProviderRow *self)
{
  update_provider_label (self);
}

static void
on_provider_notify_loading_cb (GtdProvider           *provider,
                               GParamSpec            *pspec,
                               GtdSidebarProviderRow *self)
{
  update_loading_state (self);
}


/*
 * GObject overrides
 */

static void
gtd_sidebar_provider_row_finalize (GObject *object)
{
  GtdSidebarProviderRow *self = (GtdSidebarProviderRow *)object;

  g_clear_object (&self->provider);

  G_OBJECT_CLASS (gtd_sidebar_provider_row_parent_class)->finalize (object);
}

static void
gtd_sidebar_provider_row_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  GtdSidebarProviderRow *self = GTD_SIDEBAR_PROVIDER_ROW (object);

  switch (prop_id)
    {
    case PROP_PROVIDER:
      g_value_set_object (value, self->provider);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_sidebar_provider_row_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
  GtdSidebarProviderRow *self = GTD_SIDEBAR_PROVIDER_ROW (object);

  switch (prop_id)
    {
    case PROP_PROVIDER:
      set_provider (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}


/*
 * GtkWidget overrides
 */

static void
gtd_sidebar_provider_row_class_init (GtdSidebarProviderRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gtd_sidebar_provider_row_finalize;
  object_class->get_property = gtd_sidebar_provider_row_get_property;
  object_class->set_property = gtd_sidebar_provider_row_set_property;

  properties[PROP_PROVIDER] = g_param_spec_object ("provider",
                                                   "Provider",
                                                   "Provider of the row",
                                                   GTD_TYPE_PROVIDER,
                                                   G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/todo/ui/sidebar-provider-row.ui");

  gtk_widget_class_bind_template_child (widget_class, GtdSidebarProviderRow, loading_label);
  gtk_widget_class_bind_template_child (widget_class, GtdSidebarProviderRow, provider_label);
  gtk_widget_class_bind_template_child (widget_class, GtdSidebarProviderRow, stack);
}

static void
gtd_sidebar_provider_row_init (GtdSidebarProviderRow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget*
gtd_sidebar_provider_row_new (GtdProvider *provider)
{
  return g_object_new (GTD_TYPE_SIDEBAR_PROVIDER_ROW,
                       "provider", provider,
                       NULL);
}

GtdProvider*
gtd_sidebar_provider_row_get_provider (GtdSidebarProviderRow *self)
{
  g_return_val_if_fail (GTD_IS_SIDEBAR_PROVIDER_ROW (self), NULL);

  return self->provider;
}

void
gtd_sidebar_provider_row_popup_menu (GtdSidebarProviderRow *self)
{
  g_assert (GTD_IS_SIDEBAR_PROVIDER_ROW (self));

  /* TODO: Implement me */
}
