/* gtd-animatable.c
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
 * SECTION:gtd-animatable
 * @short_description: Interface for animatable classes
 *
 * #GtdAnimatable is an interface that allows a #GObject class
 * to control how an widget will animate a property.
 *
 * Each #GtdAnimatable should implement the
 * #GtdAnimatableInterface.interpolate_property() virtual function of the
 * interface to compute the animation state between two values of an interval
 * depending on a progress fwidget, expressed as a floating point value.
 */

#include "gtd-animatable.h"

#include "gtd-debug.h"
#include "gtd-interval.h"

G_DEFINE_INTERFACE (GtdAnimatable, gtd_animatable, G_TYPE_OBJECT);

static void
gtd_animatable_default_init (GtdAnimatableInterface *iface)
{
}

/**
 * gtd_animatable_find_property:
 * @animatable: a #GtdAnimatable
 * @property_name: the name of the animatable property to find
 *
 * Finds the #GParamSpec for @property_name
 *
 * Return value: (transfer none) (nullable): The #GParamSpec for the given property
 */
GParamSpec *
gtd_animatable_find_property (GtdAnimatable *animatable,
                              const gchar   *property_name)
{
  GtdAnimatableInterface *iface;

  g_return_val_if_fail (GTD_IS_ANIMATABLE (animatable), NULL);
  g_return_val_if_fail (property_name != NULL, NULL);

  GTD_TRACE_MSG ("[animation] Looking for property '%s'", property_name);

  iface = GTD_ANIMATABLE_GET_IFACE (animatable);
  if (iface->find_property != NULL)
    return iface->find_property (animatable, property_name);

  return g_object_class_find_property (G_OBJECT_GET_CLASS (animatable),
                                       property_name);
}

/**
 * gtd_animatable_get_initial_state:
 * @animatable: a #GtdAnimatable
 * @property_name: the name of the animatable property to retrieve
 * @value: a #GValue initialized to the type of the property to retrieve
 *
 * Retrieves the current state of @property_name and sets @value with it
 */
void
gtd_animatable_get_initial_state (GtdAnimatable *animatable,
                                  const gchar   *property_name,
                                  GValue        *value)
{
  GtdAnimatableInterface *iface;

  g_return_if_fail (GTD_IS_ANIMATABLE (animatable));
  g_return_if_fail (property_name != NULL);

  GTD_TRACE_MSG ("[animation] Getting initial state of '%s'", property_name);

  iface = GTD_ANIMATABLE_GET_IFACE (animatable);
  if (iface->get_initial_state != NULL)
    iface->get_initial_state (animatable, property_name, value);
  else
    g_object_get_property (G_OBJECT (animatable), property_name, value);
}

/**
 * gtd_animatable_set_final_state:
 * @animatable: a #GtdAnimatable
 * @property_name: the name of the animatable property to set
 * @value: the value of the animatable property to set
 *
 * Sets the current state of @property_name to @value
 */
void
gtd_animatable_set_final_state (GtdAnimatable *animatable,
                                const gchar   *property_name,
                                const GValue  *value)
{
  GtdAnimatableInterface *iface;

  g_return_if_fail (GTD_IS_ANIMATABLE (animatable));
  g_return_if_fail (property_name != NULL);

  GTD_TRACE_MSG ("[animation] Setting state of property '%s'", property_name);

  iface = GTD_ANIMATABLE_GET_IFACE (animatable);
  if (iface->set_final_state != NULL)
    iface->set_final_state (animatable, property_name, value);
  else
    g_object_set_property (G_OBJECT (animatable), property_name, value);
}

/**
 * gtd_animatable_interpolate_value:
 * @animatable: a #GtdAnimatable
 * @property_name: the name of the property to interpolate
 * @interval: a #GtdInterval with the animation range
 * @progress: the progress to use to interpolate between the
 *   initial and final values of the @interval
 * @value: (out): return location for an initialized #GValue
 *   using the same type of the @interval
 *
 * Asks a #GtdAnimatable implementation to interpolate a
 * a named property between the initial and final values of
 * a #GtdInterval, using @progress as the interpolation
 * value, and store the result inside @value.
 *
 * This function should be used for every property animation
 * involving #GtdAnimatable<!-- -->s.
 *
 * This function replaces gtd_animatable_animate_property().
 *
 * Return value: %TRUE if the interpolation was successful,
 *   and %FALSE otherwise
 */
gboolean
gtd_animatable_interpolate_value (GtdAnimatable *animatable,
                                  const gchar   *property_name,
                                  GtdInterval   *interval,
                                  gdouble        progress,
                                  GValue        *value)
{
  GtdAnimatableInterface *iface;

  g_return_val_if_fail (GTD_IS_ANIMATABLE (animatable), FALSE);
  g_return_val_if_fail (property_name != NULL, FALSE);
  g_return_val_if_fail (GTD_IS_INTERVAL (interval), FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  GTD_TRACE_MSG ("[animation] Interpolating '%s' (progress: %.3f)",
                property_name,
                progress);

  iface = GTD_ANIMATABLE_GET_IFACE (animatable);
  if (iface->interpolate_value != NULL)
    {
      return iface->interpolate_value (animatable, property_name,
                                       interval,
                                       progress,
                                       value);
    }
  else
    {
      return gtd_interval_compute_value (interval, progress, value);
    }
}

/**
 * gtd_animatable_get_widget:
 * @animatable: a #GtdAnimatable
 *
 * Get animated widget.
 *
 * Return value: (transfer none): a #GtdWidget
 */
GtdWidget *
gtd_animatable_get_widget (GtdAnimatable *animatable)
{
  GtdAnimatableInterface *iface;

  g_return_val_if_fail (GTD_IS_ANIMATABLE (animatable), NULL);

  iface = GTD_ANIMATABLE_GET_IFACE (animatable);

  g_return_val_if_fail (iface->get_widget, NULL);

  return iface->get_widget (animatable);
}
