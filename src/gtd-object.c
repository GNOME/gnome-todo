/* gtd-object.c
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

#define G_LOG_DOMAIN "GtdObject"

#include "gtd-object.h"

#include <glib/gi18n.h>

/**
 * SECTION:gtd-object
 * @Short_description: base class for loadable and uniquely identifiable objects
 * @Title: GtdObject
 *
 * #GtdObject is the base class of many object in GNOME To Do, and it useful for
 * when a given object is loadable and/or uniquely identifiable. Some examples of
 * it are #GtdTask, #GtdTaskList and #GtdNotification.
 *
 */

typedef struct
{
  gboolean       ready;
  gchar         *uid;
} GtdObjectPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GtdObject, gtd_object, G_TYPE_OBJECT)

enum
{
  PROP_0,
  PROP_READY,
  PROP_UID,
  LAST_PROP
};

static const gchar*
gtd_object_real_get_uid (GtdObject *object)
{
  GtdObjectPrivate *priv;

  g_return_val_if_fail (GTD_IS_OBJECT (object), NULL);

  priv = gtd_object_get_instance_private (object);

  return priv->uid;
}

static void
gtd_object_real_set_uid (GtdObject   *object,
                         const gchar *uid)
{
  GtdObjectPrivate *priv;

  g_assert (GTD_IS_OBJECT (object));

  priv = gtd_object_get_instance_private (object);

  if (g_strcmp0 (priv->uid, uid) != 0)
    {
      if (priv->uid)
        g_free (priv->uid);

      priv->uid = g_strdup (uid);

      g_object_notify (G_OBJECT (object), "uid");
    }
}

static void
gtd_object_finalize (GObject *object)
{
  GtdObject *self = GTD_OBJECT (object);
  GtdObjectPrivate *priv = gtd_object_get_instance_private (self);

  if (priv->uid)
    g_free (priv->uid);

  G_OBJECT_CLASS (gtd_object_parent_class)->finalize (object);
}

static void
gtd_object_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  GtdObject *self = GTD_OBJECT (object);
  GtdObjectPrivate *priv = gtd_object_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_READY:
      g_value_set_boolean (value, priv->ready);
      break;

    case PROP_UID:
      g_value_set_string (value, GTD_OBJECT_GET_CLASS (self)->get_uid (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_object_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  GtdObject *self = GTD_OBJECT (object);

  switch (prop_id)
    {
    case PROP_READY:
      gtd_object_set_ready (self, g_value_get_boolean (value));
      break;

    case PROP_UID:
      GTD_OBJECT_GET_CLASS (self)->set_uid (self, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_object_class_init (GtdObjectClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  klass->get_uid = gtd_object_real_get_uid;
  klass->set_uid = gtd_object_real_set_uid;

  object_class->finalize = gtd_object_finalize;
  object_class->get_property = gtd_object_get_property;
  object_class->set_property = gtd_object_set_property;

  /**
   * GtdObject::uid:
   *
   * The unique identified of the object, set by the backend.
   */
  g_object_class_install_property (
        object_class,
        PROP_UID,
        g_param_spec_string ("uid",
                             "Unique identifier of the object",
                             "The unique identifier of the object, defined by the backend",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  /**
   * GtdObject::ready:
   *
   * Whether the object is ready or not.
   */
  g_object_class_install_property (
        object_class,
        PROP_READY,
        g_param_spec_boolean ("ready",
                              "Ready state of the object",
                              "Whether the object is marked as ready or not",
                              TRUE,
                              G_PARAM_READWRITE));
}

static void
gtd_object_init (GtdObject *self)
{
  GtdObjectPrivate *priv = gtd_object_get_instance_private (self);

  priv->ready = TRUE;
}

/**
 * gtd_object_new:
 * @uid: unique identifier of the object
 *
 * Creates a new #GtdObject object.
 *
 * Returns: (transfer full): a new #GtdObject
 */
GtdObject*
gtd_object_new (const gchar *uid)
{
  return g_object_new (GTD_TYPE_OBJECT, "uid", uid, NULL);
}

/**
 * gtd_object_get_uid:
 * @object: a #GtdObject
 *
 * Retrieves the internal unique identifier of @object.
 *
 * Returns: (transfer none): the unique identifier of @object. Do
 * not free after usage.
 */
const gchar*
gtd_object_get_uid (GtdObject *object)
{
  GtdObjectClass *class;

  g_return_val_if_fail (GTD_IS_OBJECT (object), NULL);

  class = GTD_OBJECT_GET_CLASS (object);

  g_assert (class);
  return class->get_uid (object);
}

/**
 * gtd_object_set_uid:
 * @object: a #GtdObject
 * @uid: the unique identifier of @object
 *
 * Sets the unique identifier of @object to @uid. Only
 * a #GtdBackend should do it.
 */
void
gtd_object_set_uid (GtdObject   *object,
                    const gchar *uid)
{
  GtdObjectClass *class;

  g_return_if_fail (GTD_IS_OBJECT (object));

  class = GTD_OBJECT_GET_CLASS (object);

  g_assert (class);
  class->set_uid (object, uid);
}

/**
 * gtd_object_get_ready:
 * @object: a #GtdObject
 *
 * Whether @object is ready.
 *
 * Returns: %TRUE if @object is ready, %FALSE otherwise.
 */
gboolean
gtd_object_get_ready (GtdObject *object)
{
  GtdObjectPrivate *priv;

  g_return_val_if_fail (GTD_IS_OBJECT (object), FALSE);

  priv = gtd_object_get_instance_private (object);

  return priv->ready;
}

/**
 * gtd_object_set_ready:
 * @object: a #GtdObject
 * @ready: whether @object is ready or not
 *
 * Sets the GtdObject::ready property to @ready.
 */
void
gtd_object_set_ready (GtdObject *object,
                      gboolean   ready)
{
  GtdObjectPrivate *priv;

  g_assert (GTD_IS_OBJECT (object));

  priv = gtd_object_get_instance_private (object);

  if (priv->ready != ready)
    {
      priv->ready = ready;

      g_object_notify (G_OBJECT (object), "ready");
    }
}
