/* gtd-provider-row.c
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

#define G_LOG_DOMAIN "GtdProviderRow"

#include "interfaces/gtd-provider.h"
#include "gtd-provider-row.h"

#include <glib/gi18n.h>

typedef struct
{
  GtkImage        *icon;
  GtkLabel        *name;
  GtkLabel        *description;
  GtkLabel        *enabled;
  GtkImage        *selected;

  GtdProvider     *provider;
} GtdProviderRowPrivate;

struct _GtdProviderRow
{
  GtkListBoxRow         parent;

  /*< private >*/
  GtdProviderRowPrivate *priv;
};

G_DEFINE_TYPE_WITH_PRIVATE (GtdProviderRow, gtd_provider_row, GTK_TYPE_LIST_BOX_ROW)

enum {
  PROP_0,
  PROP_PROVIDER,
  LAST_PROP
};

static void
gtd_provider_row_finalize (GObject *object)
{
  GtdProviderRow *self = (GtdProviderRow *)object;
  GtdProviderRowPrivate *priv = gtd_provider_row_get_instance_private (self);

  g_clear_object (&priv->provider);

  G_OBJECT_CLASS (gtd_provider_row_parent_class)->finalize (object);
}

static void
gtd_provider_row_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  GtdProviderRow *self = GTD_PROVIDER_ROW (object);

  switch (prop_id)
    {
    case PROP_PROVIDER:
      g_value_set_object (value, self->priv->provider);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_provider_row_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  GtdProviderRow *self = GTD_PROVIDER_ROW (object);

  switch (prop_id)
    {
    case PROP_PROVIDER:
      self->priv->provider = g_value_get_object (value);

      if (!self->priv->provider)
        break;

      g_object_ref (self->priv->provider);

      g_object_bind_property (self->priv->provider,
                              "name",
                              self->priv->name,
                              "label",
                              G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

      g_object_bind_property (self->priv->provider,
                              "description",
                              self->priv->description,
                              "label",
                              G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

      g_object_bind_property (self->priv->provider,
                              "enabled",
                              self->priv->enabled,
                              "visible",
                              G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE | G_BINDING_INVERT_BOOLEAN);

      g_object_bind_property (self->priv->provider,
                              "icon",
                              self->priv->icon,
                              "gicon",
                              G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_provider_row_class_init (GtdProviderRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gtd_provider_row_finalize;
  object_class->get_property = gtd_provider_row_get_property;
  object_class->set_property = gtd_provider_row_set_property;

  /**
   * GtdProviderRow::provider:
   *
   * #GtdProvider related to this row.
   */
  g_object_class_install_property (
        object_class,
        PROP_PROVIDER,
        g_param_spec_object ("provider",
                             "Provider of the row",
                             "The provider that this row holds",
                             GTD_TYPE_PROVIDER,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));


  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/todo/ui/provider-row.ui");

  gtk_widget_class_bind_template_child_private (widget_class, GtdProviderRow, icon);
  gtk_widget_class_bind_template_child_private (widget_class, GtdProviderRow, name);
  gtk_widget_class_bind_template_child_private (widget_class, GtdProviderRow, description);
  gtk_widget_class_bind_template_child_private (widget_class, GtdProviderRow, enabled);
  gtk_widget_class_bind_template_child_private (widget_class, GtdProviderRow, selected);
}

static void
gtd_provider_row_init (GtdProviderRow *self)
{
  self->priv = gtd_provider_row_get_instance_private (self);

  gtk_widget_init_template (GTK_WIDGET (self));
}


/**
 * gtd_provider_row_new:
 * @provider: a #GtdProvider
 *
 * Created a new #GtdProviderRow with @account information.
 *
 * Returns: (transfer full): a new #GtdProviderRow
 */
GtkWidget*
gtd_provider_row_new (GtdProvider *provider)
{
  return g_object_new (GTD_TYPE_PROVIDER_ROW,
                       "provider", provider,
                       NULL);
}

/**
 * gtd_provider_row_get_provider:
 * @row: a #GtdProviderRow
 *
 * Retrieves the #GtdProvider associated with @row.
 *
 * Returns: (transfer none): the #GtdProvider associated with @row.
 */
GtdProvider*
gtd_provider_row_get_provider (GtdProviderRow *row)
{
  g_return_val_if_fail (GTD_IS_PROVIDER_ROW (row), NULL);

  return row->priv->provider;
}

/**
 * gtd_provider_row_get_selected:
 * @row: a #GtdProviderRow
 *
 * Whether @row is the currently selected row or not.
 *
 * Returns: %TRUE if the row is currently selected, %FALSE otherwise.
 */
gboolean
gtd_provider_row_get_selected (GtdProviderRow *row)
{
  g_return_val_if_fail (GTD_IS_PROVIDER_ROW (row), FALSE);

  return gtk_widget_get_visible (GTK_WIDGET (row->priv->selected));
}

/**
 * gtd_provider_row_set_selected:
 * @row: a #GtdProviderRow
 * @selected: %TRUE if row is selected (i.e. the selection
 * mark is visible)
 *
 * Sets @row as the currently selected row.
 */
void
gtd_provider_row_set_selected (GtdProviderRow *row,
                              gboolean       selected)
{
  g_return_if_fail (GTD_IS_PROVIDER_ROW (row));

  if (selected != gtk_widget_get_visible (GTK_WIDGET (row->priv->selected)))
    {
      gtk_widget_set_visible (GTK_WIDGET (row->priv->selected), selected);
    }
}
