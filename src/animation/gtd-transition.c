/* gtd-transition.c
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
 * SECTION:gtd-transition
 * @Title: GtdTransition
 * @Short_Description: Transition between two values
 *
 * #GtdTransition is an abstract subclass of #GtdTimeline that
 * computes the interpolation between two values, stored by a #GtdInterval.
 */

#include "gtd-transition.h"

#include "gtd-animatable.h"
#include "gtd-debug.h"
#include "gtd-interval.h"
#include "gtd-timeline.h"

#include <gobject/gvaluecollector.h>

typedef struct
{
  GtdInterval *interval;
  GtdAnimatable *animatable;

  guint remove_on_complete : 1;
} GtdTransitionPrivate;

enum
{
  PROP_0,
  PROP_INTERVAL,
  PROP_ANIMATABLE,
  PROP_REMOVE_ON_COMPLETE,
  PROP_LAST,
};

static GParamSpec *obj_props[PROP_LAST] = { NULL, };

static GQuark quark_animatable_set = 0;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GtdTransition, gtd_transition, GTD_TYPE_TIMELINE)

static void
gtd_transition_attach (GtdTransition *self,
                       GtdAnimatable *animatable)
{
  GTD_TRANSITION_GET_CLASS (self)->attached (self, animatable);
}

static void
gtd_transition_detach (GtdTransition *self,
                       GtdAnimatable *animatable)
{
  GTD_TRANSITION_GET_CLASS (self)->detached (self, animatable);
}

static void
gtd_transition_real_compute_value (GtdTransition *self,
                                   GtdAnimatable *animatable,
                                   GtdInterval   *interval,
                                   gdouble        progress)
{
}

static void
gtd_transition_real_attached (GtdTransition *self,
                              GtdAnimatable *animatable)
{
}

static void
gtd_transition_real_detached (GtdTransition *self,
                              GtdAnimatable *animatable)
{
}

static void
gtd_transition_new_frame (GtdTimeline *timeline,
                          gint         elapsed)
{
  GtdTransition *self = GTD_TRANSITION (timeline);
  GtdTransitionPrivate *priv = gtd_transition_get_instance_private (self);
  gdouble progress;

  if (!priv->interval || !priv->animatable)
    return;

  progress = gtd_timeline_get_progress (timeline);

  GTD_TRANSITION_GET_CLASS (timeline)->compute_value (self,
                                                      priv->animatable,
                                                      priv->interval,
                                                      progress);
}

static void
gtd_transition_stopped (GtdTimeline *timeline,
                        gboolean     is_finished)
{
  GtdTransition *self = GTD_TRANSITION (timeline);
  GtdTransitionPrivate *priv = gtd_transition_get_instance_private (self);

  if (is_finished &&
      priv->animatable != NULL &&
      priv->remove_on_complete)
    {
      gtd_transition_detach (GTD_TRANSITION (timeline),
                                 priv->animatable);
      g_clear_object (&priv->animatable);
    }
}

static void
gtd_transition_set_property (GObject      *gobject,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  GtdTransition *self = GTD_TRANSITION (gobject);

  switch (prop_id)
    {
    case PROP_INTERVAL:
      gtd_transition_set_interval (self, g_value_get_object (value));
      break;

    case PROP_ANIMATABLE:
      gtd_transition_set_animatable (self, g_value_get_object (value));
      break;

    case PROP_REMOVE_ON_COMPLETE:
      gtd_transition_set_remove_on_complete (self, g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
gtd_transition_get_property (GObject    *gobject,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  GtdTransition *self = GTD_TRANSITION (gobject);
  GtdTransitionPrivate *priv = gtd_transition_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_INTERVAL:
      g_value_set_object (value, priv->interval);
      break;

    case PROP_ANIMATABLE:
      g_value_set_object (value, priv->animatable);
      break;

    case PROP_REMOVE_ON_COMPLETE:
      g_value_set_boolean (value, priv->remove_on_complete);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
gtd_transition_dispose (GObject *gobject)
{
  GtdTransition *self = GTD_TRANSITION (gobject);
  GtdTransitionPrivate *priv = gtd_transition_get_instance_private (self);

  if (priv->animatable != NULL)
    gtd_transition_detach (GTD_TRANSITION (gobject),
                               priv->animatable);

  g_clear_object (&priv->interval);
  g_clear_object (&priv->animatable);

  G_OBJECT_CLASS (gtd_transition_parent_class)->dispose (gobject);
}

static void
gtd_transition_class_init (GtdTransitionClass *klass)
{
  GtdTimelineClass *timeline_class = GTD_TIMELINE_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  quark_animatable_set =
    g_quark_from_static_string ("-gtd-transition-animatable-set");

  klass->compute_value = gtd_transition_real_compute_value;
  klass->attached = gtd_transition_real_attached;
  klass->detached = gtd_transition_real_detached;

  timeline_class->new_frame = gtd_transition_new_frame;
  timeline_class->stopped = gtd_transition_stopped;

  gobject_class->set_property = gtd_transition_set_property;
  gobject_class->get_property = gtd_transition_get_property;
  gobject_class->dispose = gtd_transition_dispose;

  /**
   * GtdTransition:interval:
   *
   * The #GtdInterval used to describe the initial and final states
   * of the transition.
   */
  obj_props[PROP_INTERVAL] =
    g_param_spec_object ("interval",
                         "Interval",
                         "The interval of values to transition",
                         GTD_TYPE_INTERVAL,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GtdTransition:animatable:
   *
   * The #GtdAnimatable instance currently being animated.
   */
  obj_props[PROP_ANIMATABLE] =
    g_param_spec_object ("animatable",
                         "Animatable",
                         "The animatable object",
                         GTD_TYPE_ANIMATABLE,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GtdTransition:remove-on-complete:
   *
   * Whether the #GtdTransition should be automatically detached
   * from the #GtdTransition:animatable instance whenever the
   * #GtdTimeline::stopped signal is emitted.
   *
   * The #GtdTransition:remove-on-complete property takes into
   * account the value of the #GtdTimeline:repeat-count property,
   * and it only detaches the transition if the transition is not
   * repeating.
   */
  obj_props[PROP_REMOVE_ON_COMPLETE] =
    g_param_spec_boolean ("remove-on-complete",
                          "Remove on Complete",
                          "Detach the transition when completed",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, PROP_LAST, obj_props);
}

static void
gtd_transition_init (GtdTransition *self)
{
}

/**
 * gtd_transition_set_interval:
 * @transition: a #GtdTransition
 * @interval: (allow-none): a #GtdInterval, or %NULL
 *
 * Sets the #GtdTransition:interval property using @interval.
 *
 * The @transition will acquire a reference on the @interval, sinking
 * the floating flag on it if necessary.
 */
void
gtd_transition_set_interval (GtdTransition *self,
                                 GtdInterval   *interval)
{
  GtdTransitionPrivate *priv;

  g_return_if_fail (GTD_IS_TRANSITION (self));
  g_return_if_fail (interval == NULL || GTD_IS_INTERVAL (interval));

  priv = gtd_transition_get_instance_private (self);

  if (priv->interval == interval)
    return;

  g_clear_object (&priv->interval);

  if (interval != NULL)
    priv->interval = g_object_ref_sink (interval);

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_INTERVAL]);
}

/**
 * gtd_transition_get_interval:
 * @transition: a #GtdTransition
 *
 * Retrieves the interval set using gtd_transition_set_interval()
 *
 * Return value: (transfer none): a #GtdInterval, or %NULL; the returned
 *   interval is owned by the #GtdTransition and it should not be freed
 *   directly
 */
GtdInterval *
gtd_transition_get_interval (GtdTransition *self)
{
  GtdTransitionPrivate *priv = gtd_transition_get_instance_private (self);

  g_return_val_if_fail (GTD_IS_TRANSITION (self), NULL);

  priv = gtd_transition_get_instance_private (self);
  return priv->interval;
}

/**
 * gtd_transition_set_animatable:
 * @transition: a #GtdTransition
 * @animatable: (allow-none): a #GtdAnimatable, or %NULL
 *
 * Sets the #GtdTransition:animatable property.
 *
 * The @transition will acquire a reference to the @animatable instance,
 * and will call the #GtdTransitionClass.attached() virtual function.
 *
 * If an existing #GtdAnimatable is attached to @self, the
 * reference will be released, and the #GtdTransitionClass.detached()
 * virtual function will be called.
 */
void
gtd_transition_set_animatable (GtdTransition *self,
                               GtdAnimatable *animatable)
{
  GtdTransitionPrivate *priv;
  GtdWidget *widget;

  g_return_if_fail (GTD_IS_TRANSITION (self));
  g_return_if_fail (animatable == NULL || GTD_IS_ANIMATABLE (animatable));

  priv = gtd_transition_get_instance_private (self);

  if (priv->animatable == animatable)
    return;

  if (priv->animatable != NULL)
    gtd_transition_detach (self, priv->animatable);

  g_clear_object (&priv->animatable);

  if (animatable != NULL)
    {
      priv->animatable = g_object_ref (animatable);
      gtd_transition_attach (self, priv->animatable);
    }

  widget = gtd_animatable_get_widget (animatable);
  gtd_timeline_set_widget (GTD_TIMELINE (self), widget);
}

/**
 * gtd_transition_get_animatable:
 * @transition: a #GtdTransition
 *
 * Retrieves the #GtdAnimatable set using gtd_transition_set_animatable().
 *
 * Return value: (transfer none): a #GtdAnimatable, or %NULL; the returned
 *   animatable is owned by the #GtdTransition, and it should not be freed
 *   directly.
 */
GtdAnimatable *
gtd_transition_get_animatable (GtdTransition *self)
{
  GtdTransitionPrivate *priv;

  g_return_val_if_fail (GTD_IS_TRANSITION (self), NULL);

  priv = gtd_transition_get_instance_private (self);
  return priv->animatable;
}

/**
 * gtd_transition_set_remove_on_complete:
 * @transition: a #GtdTransition
 * @remove_complete: whether to detach @transition when complete
 *
 * Sets whether @transition should be detached from the #GtdAnimatable
 * set using gtd_transition_set_animatable() when the
 * #GtdTimeline::completed signal is emitted.
 */
void
gtd_transition_set_remove_on_complete (GtdTransition *self,
                                       gboolean       remove_complete)
{
  GtdTransitionPrivate *priv;

  g_return_if_fail (GTD_IS_TRANSITION (self));

  priv = gtd_transition_get_instance_private (self);
  remove_complete = !!remove_complete;

  if (priv->remove_on_complete == remove_complete)
    return;

  priv->remove_on_complete = remove_complete;

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_REMOVE_ON_COMPLETE]);
}

/**
 * gtd_transition_get_remove_on_complete:
 * @transition: a #GtdTransition
 *
 * Retrieves the value of the #GtdTransition:remove-on-complete property.
 *
 * Return value: %TRUE if the @transition should be detached when complete,
 *   and %FALSE otherwise
 */
gboolean
gtd_transition_get_remove_on_complete (GtdTransition *self)
{
  GtdTransitionPrivate *priv;

  g_return_val_if_fail (GTD_IS_TRANSITION (self), FALSE);

  priv = gtd_transition_get_instance_private (self);
  return priv->remove_on_complete;
}

typedef void (* IntervalSetFunc) (GtdInterval *interval,
                                  const GValue    *value);

static inline void
gtd_transition_set_value (GtdTransition   *self,
                          IntervalSetFunc  interval_set_func,
                          const GValue    *value)
{
  GtdTransitionPrivate *priv = gtd_transition_get_instance_private (self);
  GType interval_type;

  if (priv->interval == NULL)
    {
      priv->interval = gtd_interval_new_with_values (G_VALUE_TYPE (value), NULL, NULL);
      g_object_ref_sink (priv->interval);
    }

  interval_type = gtd_interval_get_value_type (priv->interval);

  if (!g_type_is_a (G_VALUE_TYPE (value), interval_type))
    {
      if (g_value_type_compatible (G_VALUE_TYPE (value), interval_type))
        {
          interval_set_func (priv->interval, value);
          return;
        }

      if (g_value_type_transformable (G_VALUE_TYPE (value), interval_type))
        {
          GValue transform = G_VALUE_INIT;

          g_value_init (&transform, interval_type);
          if (g_value_transform (value, &transform))
            interval_set_func (priv->interval, &transform);
          else
            {
              g_warning ("%s: Unable to convert a value of type '%s' into "
                         "the value type '%s' of the interval used by the "
                         "transition.",
                         G_STRLOC,
                         g_type_name (G_VALUE_TYPE (value)),
                         g_type_name (interval_type));
            }

          g_value_unset (&transform);
        }
    }
  else
    {
      interval_set_func (priv->interval, value);
    }
}

/**
 * gtd_transition_set_from_value: (rename-to gtd_transition_set_from)
 * @transition: a #GtdTransition
 * @value: a #GValue with the initial value of the transition
 *
 * Sets the initial value of the transition.
 *
 * This is a convenience function that will either create the
 * #GtdInterval used by @self, or will update it if
 * the #GtdTransition:interval is already set.
 *
 * This function will copy the contents of @value, so it is
 * safe to call g_value_unset() after it returns.
 *
 * If @transition already has a #GtdTransition:interval set,
 * then @value must hold the same type, or a transformable type,
 * as the interval's #GtdInterval:value-type property.
 *
 * This function is meant to be used by language bindings.
 */
void
gtd_transition_set_from_value (GtdTransition *self,
                               const GValue  *value)
{
  g_return_if_fail (GTD_IS_TRANSITION (self));
  g_return_if_fail (G_IS_VALUE (value));

  gtd_transition_set_value (self, gtd_interval_set_initial_value, value);
}

/**
 * gtd_transition_set_to_value: (rename-to gtd_transition_set_to)
 * @transition: a #GtdTransition
 * @value: a #GValue with the final value of the transition
 *
 * Sets the final value of the transition.
 *
 * This is a convenience function that will either create the
 * #GtdInterval used by @self, or will update it if
 * the #GtdTransition:interval is already set.
 *
 * This function will copy the contents of @value, so it is
 * safe to call g_value_unset() after it returns.
 *
 * If @transition already has a #GtdTransition:interval set,
 * then @value must hold the same type, or a transformable type,
 * as the interval's #GtdInterval:value-type property.
 *
 * This function is meant to be used by language bindings.
 */
void
gtd_transition_set_to_value (GtdTransition *self,
                             const GValue  *value)
{
  g_return_if_fail (GTD_IS_TRANSITION (self));
  g_return_if_fail (G_IS_VALUE (value));

  gtd_transition_set_value (self,
                                gtd_interval_set_final_value,
                                value);
}

/**
 * gtd_transition_set_from: (skip)
 * @transition: a #GtdTransition
 * @value_type: the type of the value to set
 * @...: the initial value
 *
 * Sets the initial value of the transition.
 *
 * This is a convenience function that will either create the
 * #GtdInterval used by @self, or will update it if
 * the #GtdTransition:interval is already set.
 *
 * If @transition already has a #GtdTransition:interval set,
 * then @value must hold the same type, or a transformable type,
 * as the interval's #GtdInterval:value-type property.
 *
 * This is a convenience function for the C API; language bindings
 * should use gtd_transition_set_from_value() instead.
 */
void
gtd_transition_set_from (GtdTransition *self,
                         GType          value_type,
                         ...)
{
  GValue value = G_VALUE_INIT;
  gchar *error = NULL;
  va_list args;

  g_return_if_fail (GTD_IS_TRANSITION (self));
  g_return_if_fail (value_type != G_TYPE_INVALID);

  va_start (args, value_type);

  G_VALUE_COLLECT_INIT (&value, value_type, args, 0, &error);

  va_end (args);

  if (error != NULL)
    {
      g_warning ("%s: %s", G_STRLOC, error);
      g_free (error);
      return;
    }

  gtd_transition_set_value (self, gtd_interval_set_initial_value, &value);

  g_value_unset (&value);
}

/**
 * gtd_transition_set_to: (skip)
 * @transition: a #GtdTransition
 * @value_type: the type of the value to set
 * @...: the final value
 *
 * Sets the final value of the transition.
 *
 * This is a convenience function that will either create the
 * #GtdInterval used by @self, or will update it if
 * the #GtdTransition:interval is already set.
 *
 * If @transition already has a #GtdTransition:interval set,
 * then @value must hold the same type, or a transformable type,
 * as the interval's #GtdInterval:value-type property.
 *
 * This is a convenience function for the C API; language bindings
 * should use gtd_transition_set_to_value() instead.
 */
void
gtd_transition_set_to (GtdTransition *self,
                       GType          value_type,
                       ...)
{
  GValue value = G_VALUE_INIT;
  gchar *error = NULL;
  va_list args;

  g_return_if_fail (GTD_IS_TRANSITION (self));
  g_return_if_fail (value_type != G_TYPE_INVALID);

  va_start (args, value_type);

  G_VALUE_COLLECT_INIT (&value, value_type, args, 0, &error);

  va_end (args);

  if (error != NULL)
    {
      g_warning ("%s: %s", G_STRLOC, error);
      g_free (error);
      return;
    }

  gtd_transition_set_value (self, gtd_interval_set_final_value, &value);

  g_value_unset (&value);
}
