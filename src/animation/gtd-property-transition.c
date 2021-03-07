/* gtd-property-transition.c
 *
 * Copyright 2020 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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


/**
 * SECTION:gtd-property-transition
 * @Title: GtdPropertyTransition
 * @Short_Description: Property transitions
 *
 * #GtdPropertyTransition is a specialized #GtdTransition that
 * can be used to tween a property of a #GtdAnimatable instance.
 */

#include "gtd-property-transition.h"

#include "gtd-animatable.h"
#include "gtd-debug.h"
#include "gtd-interval.h"
#include "gtd-transition.h"

typedef struct
{
  gchar              *property_name;

  GParamSpec         *pspec;
} GtdPropertyTransitionPrivate;

enum
{
  PROP_0,
  PROP_PROPERTY_NAME,
  PROP_LAST,
};

static GParamSpec *obj_props[PROP_LAST] = { NULL, };

G_DEFINE_TYPE_WITH_PRIVATE (GtdPropertyTransition, gtd_property_transition, GTD_TYPE_TRANSITION)

static inline void
gtd_property_transition_ensure_interval (GtdPropertyTransition *self,
                                         GtdAnimatable         *animatable,
                                         GtdInterval           *interval)
{
  GtdPropertyTransitionPrivate *priv = gtd_property_transition_get_instance_private (self);
  GValue *value_p;

  if (gtd_interval_is_valid (interval))
    return;

  /* if no initial value has been set, use the current value */
  value_p = gtd_interval_peek_initial_value (interval);
  if (!G_IS_VALUE (value_p))
    {
      g_value_init (value_p, gtd_interval_get_value_type (interval));
      gtd_animatable_get_initial_state (animatable,
                                            priv->property_name,
                                            value_p);
    }

  /* if no final value has been set, use the current value */
  value_p = gtd_interval_peek_final_value (interval);
  if (!G_IS_VALUE (value_p))
    {
      g_value_init (value_p, gtd_interval_get_value_type (interval));
      gtd_animatable_get_initial_state (animatable,
                                            priv->property_name,
                                            value_p);
    }
}

static void
gtd_property_transition_attached (GtdTransition *transition,
                                  GtdAnimatable *animatable)
{
  GtdPropertyTransition *self = GTD_PROPERTY_TRANSITION (transition);
  GtdPropertyTransitionPrivate *priv = gtd_property_transition_get_instance_private (self);
  GtdInterval *interval;

  if (priv->property_name == NULL)
    return;

  priv->pspec =
    gtd_animatable_find_property (animatable, priv->property_name);

  if (priv->pspec == NULL)
    return;

  interval = gtd_transition_get_interval (transition);
  if (interval == NULL)
    return;

  gtd_property_transition_ensure_interval (self, animatable, interval);
}

static void
gtd_property_transition_detached (GtdTransition *transition,
                                  GtdAnimatable *animatable)
{
  GtdPropertyTransition *self = GTD_PROPERTY_TRANSITION (transition);
  GtdPropertyTransitionPrivate *priv = gtd_property_transition_get_instance_private (self);

  priv->pspec = NULL;
}

static void
gtd_property_transition_compute_value (GtdTransition *transition,
                                       GtdAnimatable *animatable,
                                       GtdInterval   *interval,
                                       gdouble        progress)
{
  GtdPropertyTransition *self = GTD_PROPERTY_TRANSITION (transition);
  GtdPropertyTransitionPrivate *priv = gtd_property_transition_get_instance_private (self);
  GValue value = G_VALUE_INIT;
  GType p_type, i_type;
  gboolean res;

  /* if we have a GParamSpec we also have an animatable instance */
  if (priv->pspec == NULL)
    return;

  gtd_property_transition_ensure_interval (self, animatable, interval);

  p_type = G_PARAM_SPEC_VALUE_TYPE (priv->pspec);
  i_type = gtd_interval_get_value_type (interval);

  g_value_init (&value, i_type);

  res = gtd_animatable_interpolate_value (animatable,
                                              priv->property_name,
                                              interval,
                                              progress,
                                              &value);

  if (res)
    {
      if (i_type != p_type || g_type_is_a (i_type, p_type))
        {
          if (g_value_type_transformable (i_type, p_type))
            {
              GValue transform = G_VALUE_INIT;

              g_value_init (&transform, p_type);

              if (g_value_transform (&value, &transform))
                {
                  gtd_animatable_set_final_state (animatable,
                                                      priv->property_name,
                                                      &transform);
                }
              else
                g_warning ("%s: Unable to convert a value of type '%s' from "
                           "the value type '%s' of the interval.",
                           G_STRLOC,
                           g_type_name (p_type),
                           g_type_name (i_type));

              g_value_unset (&transform);
            }
        }
      else
        gtd_animatable_set_final_state (animatable,
                                            priv->property_name,
                                            &value);
    }

  g_value_unset (&value);
}

static void
gtd_property_transition_set_property (GObject      *gobject,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  GtdPropertyTransition *self = GTD_PROPERTY_TRANSITION (gobject);

  switch (prop_id)
    {
    case PROP_PROPERTY_NAME:
      gtd_property_transition_set_property_name (self, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
    }
}

static void
gtd_property_transition_get_property (GObject    *gobject,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  GtdPropertyTransition *self = GTD_PROPERTY_TRANSITION (gobject);
  GtdPropertyTransitionPrivate *priv = gtd_property_transition_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_PROPERTY_NAME:
      g_value_set_string (value, priv->property_name);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
    }
}

static void
gtd_property_transition_finalize (GObject *gobject)
{
  GtdPropertyTransition *self = GTD_PROPERTY_TRANSITION (gobject);
  GtdPropertyTransitionPrivate *priv = gtd_property_transition_get_instance_private (self);

  g_free (priv->property_name);

  G_OBJECT_CLASS (gtd_property_transition_parent_class)->finalize (gobject);
}

static void
gtd_property_transition_class_init (GtdPropertyTransitionClass *klass)
{
  GtdTransitionClass *transition_class = GTD_TRANSITION_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  transition_class->attached = gtd_property_transition_attached;
  transition_class->detached = gtd_property_transition_detached;
  transition_class->compute_value = gtd_property_transition_compute_value;

  gobject_class->set_property = gtd_property_transition_set_property;
  gobject_class->get_property = gtd_property_transition_get_property;
  gobject_class->finalize = gtd_property_transition_finalize;

  /**
   * GtdPropertyTransition:property-name:
   *
   * The name of the property of a #GtdAnimatable to animate.
   */
  obj_props[PROP_PROPERTY_NAME] =
    g_param_spec_string ("property-name",
                         "Property Name",
                         "The name of the property to animate",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, PROP_LAST, obj_props);
}

static void
gtd_property_transition_init (GtdPropertyTransition *self)
{
}

/**
 * gtd_property_transition_new_for_actor:
 * @actor: a #GtdActor
 * @property_name: (allow-none): a property of @animatable, or %NULL
 *
 * Creates a new #GtdPropertyTransition.
 *
 * Return value: (transfer full): the newly created #GtdPropertyTransition.
 *   Use g_object_unref() when done
 */
GtdTransition *
gtd_property_transition_new_for_actor (GtdWidget  *widget,
                                       const char *property_name)
{
  return g_object_new (GTD_TYPE_PROPERTY_TRANSITION,
                       "widget", widget,
                       "property-name", property_name,
                       NULL);
}

/**
 * gtd_property_transition_new:
 * @property_name: (allow-none): a property of @animatable, or %NULL
 *
 * Creates a new #GtdPropertyTransition.
 *
 * Return value: (transfer full): the newly created #GtdPropertyTransition.
 *   Use g_object_unref() when done
 */
GtdTransition *
gtd_property_transition_new (const char *property_name)
{
  return g_object_new (GTD_TYPE_PROPERTY_TRANSITION,
                       "property-name", property_name,
                       NULL);
}

/**
 * gtd_property_transition_set_property_name:
 * @transition: a #GtdPropertyTransition
 * @property_name: (allow-none): a property name
 *
 * Sets the #GtdPropertyTransition:property-name property of @transition.
 */
void
gtd_property_transition_set_property_name (GtdPropertyTransition *self,
                                           const gchar           *property_name)
{
  GtdPropertyTransitionPrivate *priv;
  GtdAnimatable *animatable;

  g_return_if_fail (GTD_IS_PROPERTY_TRANSITION (self));

  priv = gtd_property_transition_get_instance_private (self);

  if (g_strcmp0 (priv->property_name, property_name) == 0)
    return;

  g_free (priv->property_name);
  priv->property_name = g_strdup (property_name);
  priv->pspec = NULL;

  animatable = gtd_transition_get_animatable (GTD_TRANSITION (self));
  if (animatable)
    priv->pspec = gtd_animatable_find_property (animatable, priv->property_name);

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_PROPERTY_NAME]);
}

/**
 * gtd_property_transition_get_property_name:
 * @transition: a #GtdPropertyTransition
 *
 * Retrieves the value of the #GtdPropertyTransition:property-name
 * property.
 *
 * Return value: the name of the property being animated, or %NULL if
 *   none is set. The returned string is owned by the @transition and
 *   it should not be freed.
 */
const char *
gtd_property_transition_get_property_name (GtdPropertyTransition *self)
{
  GtdPropertyTransitionPrivate *priv;

  g_return_val_if_fail (GTD_IS_PROPERTY_TRANSITION (self), NULL);

  priv = gtd_property_transition_get_instance_private (self);
  return priv->property_name;
}
