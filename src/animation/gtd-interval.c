/* gtd-interval.c
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
 * SECTION:clutter-interval
 * @short_description: An object holding an interval of two values
 *
 * #GtdInterval is a simple object that can hold two values
 * defining an interval. #GtdInterval can hold any value that
 * can be enclosed inside a #GValue.
 *
 * Once a #GtdInterval for a specific #GType has been instantiated
 * the #GtdInterval:value-type property cannot be changed anymore.
 *
 * #GtdInterval starts with a floating reference; this means that
 * any object taking a reference on a #GtdInterval instance should
 * also take ownership of the interval by using g_object_ref_sink().
 *
 * #GtdInterval can be subclassed to override the validation
 * and value computation.
 *
 * #GtdInterval is available since Gtd 1.0
 */

#include "gtd-interval.h"

#include "gtd-animation-utils.h"
#include "gtd-easing.h"

#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <glib-object.h>
#include <gobject/gvaluecollector.h>

enum
{
  PROP_0,
  PROP_VALUE_TYPE,
  PROP_INITIAL,
  PROP_FINAL,
  PROP_LAST,
};

static GParamSpec *obj_props[PROP_LAST];

enum
{
  INITIAL,
  FINAL,
  RESULT,
  N_VALUES,
};

typedef struct
{
  GType value_type;

  GValue *values;
} GtdIntervalPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GtdInterval, gtd_interval, G_TYPE_INITIALLY_UNOWNED);


static gboolean
gtd_interval_real_validate (GtdInterval *self,
                            GParamSpec  *pspec)
{
  GType pspec_gtype = G_PARAM_SPEC_VALUE_TYPE (pspec);

  /* then check the fundamental types */
  switch (G_TYPE_FUNDAMENTAL (pspec_gtype))
    {
    case G_TYPE_INT:
      {
        GParamSpecInt *pspec_int = G_PARAM_SPEC_INT (pspec);
        gint a, b;

        a = b = 0;
        gtd_interval_get_interval (self, &a, &b);
        if ((a >= pspec_int->minimum && a <= pspec_int->maximum) &&
            (b >= pspec_int->minimum && b <= pspec_int->maximum))
          return TRUE;
        else
          return FALSE;
      }
      break;

    case G_TYPE_INT64:
      {
        GParamSpecInt64 *pspec_int = G_PARAM_SPEC_INT64 (pspec);
        gint64 a, b;

        a = b = 0;
        gtd_interval_get_interval (self, &a, &b);
        if ((a >= pspec_int->minimum && a <= pspec_int->maximum) &&
            (b >= pspec_int->minimum && b <= pspec_int->maximum))
          return TRUE;
        else
          return FALSE;
      }
      break;

    case G_TYPE_UINT:
      {
        GParamSpecUInt *pspec_uint = G_PARAM_SPEC_UINT (pspec);
        guint a, b;

        a = b = 0;
        gtd_interval_get_interval (self, &a, &b);
        if ((a >= pspec_uint->minimum && a <= pspec_uint->maximum) &&
            (b >= pspec_uint->minimum && b <= pspec_uint->maximum))
          return TRUE;
        else
          return FALSE;
      }
      break;

    case G_TYPE_UINT64:
      {
        GParamSpecUInt64 *pspec_int = G_PARAM_SPEC_UINT64 (pspec);
        guint64 a, b;

        a = b = 0;
        gtd_interval_get_interval (self, &a, &b);
        if ((a >= pspec_int->minimum && a <= pspec_int->maximum) &&
            (b >= pspec_int->minimum && b <= pspec_int->maximum))
          return TRUE;
        else
          return FALSE;
      }
      break;

    case G_TYPE_CHAR:
      {
        GParamSpecChar *pspec_char = G_PARAM_SPEC_CHAR (pspec);
        guchar a, b;

        a = b = 0;
        gtd_interval_get_interval (self, &a, &b);
        if ((a >= pspec_char->minimum && a <= pspec_char->maximum) &&
            (b >= pspec_char->minimum && b <= pspec_char->maximum))
          return TRUE;
        else
          return FALSE;
      }
      break;

    case G_TYPE_UCHAR:
      {
        GParamSpecUChar *pspec_uchar = G_PARAM_SPEC_UCHAR (pspec);
        guchar a, b;

        a = b = 0;
        gtd_interval_get_interval (self, &a, &b);
        if ((a >= pspec_uchar->minimum && a <= pspec_uchar->maximum) &&
            (b >= pspec_uchar->minimum && b <= pspec_uchar->maximum))
          return TRUE;
        else
          return FALSE;
      }
      break;

    case G_TYPE_FLOAT:
      {
        GParamSpecFloat *pspec_flt = G_PARAM_SPEC_FLOAT (pspec);
        float a, b;

        a = b = 0.f;
        gtd_interval_get_interval (self, &a, &b);
        if ((a >= pspec_flt->minimum && a <= pspec_flt->maximum) &&
            (b >= pspec_flt->minimum && b <= pspec_flt->maximum))
          return TRUE;
        else
          return FALSE;
      }
      break;

    case G_TYPE_DOUBLE:
      {
        GParamSpecDouble *pspec_flt = G_PARAM_SPEC_DOUBLE (pspec);
        double a, b;

        a = b = 0;
        gtd_interval_get_interval (self, &a, &b);
        if ((a >= pspec_flt->minimum && a <= pspec_flt->maximum) &&
            (b >= pspec_flt->minimum && b <= pspec_flt->maximum))
          return TRUE;
        else
          return FALSE;
      }
      break;

    case G_TYPE_BOOLEAN:
      return TRUE;

    default:
      break;
    }

  return TRUE;
}

static gboolean
gtd_interval_real_compute_value (GtdInterval *self,
                                 gdouble      factor,
                                 GValue      *value)
{
  GValue *initial, *final;
  GType value_type;
  gboolean retval = FALSE;

  initial = gtd_interval_peek_initial_value (self);
  final = gtd_interval_peek_final_value (self);

  value_type = gtd_interval_get_value_type (self);

  if (gtd_has_progress_function (value_type))
    {
      retval = gtd_run_progress_function (value_type, initial, final, factor, value);
      if (retval)
        return TRUE;
    }

  switch (G_TYPE_FUNDAMENTAL (value_type))
    {
    case G_TYPE_INT:
      {
        gint ia, ib, res;

        ia = g_value_get_int (initial);
        ib = g_value_get_int (final);

        res = (factor * (ib - ia)) + ia;

        g_value_set_int (value, res);

        retval = TRUE;
      }
      break;

    case G_TYPE_CHAR:
      {
        gchar ia, ib, res;

        ia = g_value_get_schar (initial);
        ib = g_value_get_schar (final);

        res = (factor * (ib - (gdouble) ia)) + ia;

        g_value_set_schar (value, res);

        retval = TRUE;
      }
      break;

    case G_TYPE_UINT:
      {
        guint ia, ib, res;

        ia = g_value_get_uint (initial);
        ib = g_value_get_uint (final);

        res = (factor * (ib - (gdouble) ia)) + ia;

        g_value_set_uint (value, res);

        retval = TRUE;
      }
      break;

    case G_TYPE_UCHAR:
      {
        guchar ia, ib, res;

        ia = g_value_get_uchar (initial);
        ib = g_value_get_uchar (final);

        res = (factor * (ib - (gdouble) ia)) + ia;

        g_value_set_uchar (value, res);

        retval = TRUE;
      }
      break;

    case G_TYPE_FLOAT:
    case G_TYPE_DOUBLE:
      {
        gdouble ia, ib, res;

        if (value_type == G_TYPE_DOUBLE)
          {
            ia = g_value_get_double (initial);
            ib = g_value_get_double (final);
          }
        else
          {
            ia = g_value_get_float (initial);
            ib = g_value_get_float (final);
          }

        res = (factor * (ib - ia)) + ia;

        if (value_type == G_TYPE_DOUBLE)
          g_value_set_double (value, res);
        else
          g_value_set_float (value, res);

        retval = TRUE;
      }
      break;

    case G_TYPE_BOOLEAN:
      if (factor > 0.5)
        g_value_set_boolean (value, TRUE);
      else
        g_value_set_boolean (value, FALSE);

      retval = TRUE;
      break;

    case G_TYPE_BOXED:
      break;

    default:
      break;
    }

  /* We're trying to animate a property without knowing how to do that. Issue
   * a warning with a hint to what could be done to fix that */
  if (G_UNLIKELY (retval == FALSE))
    {
      g_warning ("%s: Could not compute progress between two %s. You can "
                 "register a progress function to instruct GtdInterval "
                 "how to deal with this GType",
                 G_STRLOC,
                 g_type_name (value_type));
    }

  return retval;
}

static void
gtd_interval_finalize (GObject *object)
{
  GtdInterval *self = GTD_INTERVAL (object);
  GtdIntervalPrivate *priv = gtd_interval_get_instance_private (self);

  if (G_IS_VALUE (&priv->values[INITIAL]))
    g_value_unset (&priv->values[INITIAL]);

  if (G_IS_VALUE (&priv->values[FINAL]))
    g_value_unset (&priv->values[FINAL]);

  if (G_IS_VALUE (&priv->values[RESULT]))
    g_value_unset (&priv->values[RESULT]);

  g_free (priv->values);

  G_OBJECT_CLASS (gtd_interval_parent_class)->finalize (object);
}

static void
gtd_interval_set_property (GObject      *gobject,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  GtdInterval *self = GTD_INTERVAL (gobject);
  GtdIntervalPrivate *priv = gtd_interval_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_VALUE_TYPE:
      priv->value_type = g_value_get_gtype (value);
      break;

    case PROP_INITIAL:
      if (g_value_get_boxed (value) != NULL)
        gtd_interval_set_initial_value (self, g_value_get_boxed (value));
      else if (G_IS_VALUE (&priv->values[INITIAL]))
        g_value_unset (&priv->values[INITIAL]);
      break;

    case PROP_FINAL:
      if (g_value_get_boxed (value) != NULL)
        gtd_interval_set_final_value (self, g_value_get_boxed (value));
      else if (G_IS_VALUE (&priv->values[FINAL]))
        g_value_unset (&priv->values[FINAL]);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
gtd_interval_get_property (GObject    *gobject,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  GtdIntervalPrivate *priv;

  priv = gtd_interval_get_instance_private (GTD_INTERVAL (gobject));

  switch (prop_id)
    {
    case PROP_VALUE_TYPE:
      g_value_set_gtype (value, priv->value_type);
      break;

    case PROP_INITIAL:
      if (G_IS_VALUE (&priv->values[INITIAL]))
        g_value_set_boxed (value, &priv->values[INITIAL]);
      break;

    case PROP_FINAL:
      if (G_IS_VALUE (&priv->values[FINAL]))
        g_value_set_boxed (value, &priv->values[FINAL]);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
gtd_interval_class_init (GtdIntervalClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  klass->validate = gtd_interval_real_validate;
  klass->compute_value = gtd_interval_real_compute_value;

  gobject_class->set_property = gtd_interval_set_property,
  gobject_class->get_property = gtd_interval_get_property;
  gobject_class->finalize = gtd_interval_finalize;

  /**
   * GtdInterval:value-type:
   *
   * The type of the values in the interval.
   */
  obj_props[PROP_VALUE_TYPE] =
    g_param_spec_gtype ("value-type",
                        "Value Type",
                        "The type of the values in the interval",
                        G_TYPE_NONE,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  /**
   * GtdInterval:initial:
   *
   * The initial value of the interval.
   */
  obj_props[PROP_INITIAL] =
    g_param_spec_boxed ("initial",
                        "Initial Value",
                        "Initial value of the interval",
                        G_TYPE_VALUE,
                        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GtdInterval:final:
   *
   * The final value of the interval.
   */
  obj_props[PROP_FINAL] =
    g_param_spec_boxed ("final",
                        "Final Value",
                        "Final value of the interval",
                        G_TYPE_VALUE,
                        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, PROP_LAST, obj_props);
}

static void
gtd_interval_init (GtdInterval *self)
{
  GtdIntervalPrivate *priv = gtd_interval_get_instance_private (self);

  priv->value_type = G_TYPE_INVALID;
  priv->values = g_malloc0 (sizeof (GValue) * N_VALUES);
}

static inline void
gtd_interval_set_value_internal (GtdInterval  *self,
                                 gint          index_,
                                 const GValue *value)
{
  GtdIntervalPrivate *priv = gtd_interval_get_instance_private (self);
  GType value_type;

  g_assert (index_ >= INITIAL && index_ <= RESULT);

  if (G_IS_VALUE (&priv->values[index_]))
    g_value_unset (&priv->values[index_]);

  g_value_init (&priv->values[index_], priv->value_type);

  value_type = G_VALUE_TYPE (value);
  if (value_type != priv->value_type ||
      !g_type_is_a (value_type, priv->value_type))
    {
      if (g_value_type_compatible (value_type, priv->value_type))
        {
          g_value_copy (value, &priv->values[index_]);
          return;
        }

      if (g_value_type_transformable (value_type, priv->value_type))
        {
          GValue transform = G_VALUE_INIT;

          g_value_init (&transform, priv->value_type);

          if (g_value_transform (value, &transform))
            g_value_copy (&transform, &priv->values[index_]);
          else
            {
              g_warning ("%s: Unable to convert a value of type '%s' into "
                         "the value type '%s' of the interval.",
                         G_STRLOC,
                         g_type_name (value_type),
                         g_type_name (priv->value_type));
            }

          g_value_unset (&transform);
        }
    }
  else
    g_value_copy (value, &priv->values[index_]);
}

static inline void
gtd_interval_get_value_internal (GtdInterval *self,
                                 gint         index_,
                                 GValue      *value)
{
  GtdIntervalPrivate *priv = gtd_interval_get_instance_private (self);

  g_assert (index_ >= INITIAL && index_ <= RESULT);

  g_value_copy (&priv->values[index_], value);
}

static gboolean
gtd_interval_set_initial_internal (GtdInterval *self,
                                   va_list     *args)
{;
  GtdIntervalPrivate *priv = gtd_interval_get_instance_private (self);
  GType gtype = priv->value_type;
  GValue value = G_VALUE_INIT;
  gchar *error;

  /* initial value */
  G_VALUE_COLLECT_INIT (&value, gtype, *args, 0, &error);

  if (error)
    {
      g_warning ("%s: %s", G_STRLOC, error);

      /* we leak the value here as it might not be in a valid state
       * given the error and calling g_value_unset() might lead to
       * undefined behaviour
       */
      g_free (error);
      return FALSE;
    }

  gtd_interval_set_value_internal (self, INITIAL, &value);
  g_value_unset (&value);

  return TRUE;
}

static gboolean
gtd_interval_set_final_internal (GtdInterval *self,
                                 va_list     *args)
{
  GtdIntervalPrivate *priv = gtd_interval_get_instance_private (self);
  GType gtype = priv->value_type;
  GValue value = G_VALUE_INIT;
  gchar *error;

  /* initial value */
  G_VALUE_COLLECT_INIT (&value, gtype, *args, 0, &error);

  if (error)
    {
      g_warning ("%s: %s", G_STRLOC, error);

      /* we leak the value here as it might not be in a valid state
       * given the error and calling g_value_unset() might lead to
       * undefined behaviour
       */
      g_free (error);
      return FALSE;
    }

  gtd_interval_set_value_internal (self, FINAL, &value);
  g_value_unset (&value);

  return TRUE;
}

static void
gtd_interval_get_interval_valist (GtdInterval *self,
                                  va_list      var_args)
{
  GtdIntervalPrivate *priv = gtd_interval_get_instance_private (self);
  GType gtype = priv->value_type;
  GValue value = G_VALUE_INIT;
  gchar *error;

  /* initial value */
  g_value_init (&value, gtype);
  gtd_interval_get_initial_value (self, &value);
  G_VALUE_LCOPY (&value, var_args, 0, &error);
  if (error)
    {
      g_warning ("%s: %s", G_STRLOC, error);
      g_free (error);
      g_value_unset (&value);
      return;
    }

  g_value_unset (&value);

  /* final value */
  g_value_init (&value, gtype);
  gtd_interval_get_final_value (self, &value);
  G_VALUE_LCOPY (&value, var_args, 0, &error);
  if (error)
    {
      g_warning ("%s: %s", G_STRLOC, error);
      g_free (error);
      g_value_unset (&value);
      return;
    }

  g_value_unset (&value);
}

/**
 * gtd_interval_new:
 * @gtype: the type of the values in the interval
 * @...: the initial value and the final value of the interval
 *
 * Creates a new #GtdInterval holding values of type @gtype.
 *
 * This function avoids using a #GValue for the initial and final values
 * of the interval:
 *
 * |[
 *   interval = gtd_interval_new (G_TYPE_FLOAT, 0.0, 1.0);
 *   interval = gtd_interval_new (G_TYPE_BOOLEAN, FALSE, TRUE);
 *   interval = gtd_interval_new (G_TYPE_INT, 0, 360);
 * ]|
 *
 * Return value: the newly created #GtdInterval
 */
GtdInterval *
gtd_interval_new (GType gtype,
                      ...)
{
  GtdInterval *retval;
  va_list args;

  g_return_val_if_fail (gtype != G_TYPE_INVALID, NULL);

  retval = g_object_new (GTD_TYPE_INTERVAL, "value-type", gtype, NULL);

  va_start (args, gtype);

  if (!gtd_interval_set_initial_internal (retval, &args))
    goto out;

  gtd_interval_set_final_internal (retval, &args);

out:
  va_end (args);

  return retval;
}

/**
 * gtd_interval_new_with_values:
 * @gtype: the type of the values in the interval
 * @initial: (allow-none): a #GValue holding the initial value of the interval
 * @final: (allow-none): a #GValue holding the final value of the interval
 *
 * Creates a new #GtdInterval of type @gtype, between @initial
 * and @final.
 *
 * This function is useful for language bindings.
 *
 * Return value: the newly created #GtdInterval
 */
GtdInterval *
gtd_interval_new_with_values (GType         gtype,
                              const GValue *initial,
                              const GValue *final)
{
  g_return_val_if_fail (gtype != G_TYPE_INVALID, NULL);
  g_return_val_if_fail (initial == NULL || G_VALUE_TYPE (initial) == gtype, NULL);
  g_return_val_if_fail (final == NULL || G_VALUE_TYPE (final) == gtype, NULL);

  return g_object_new (GTD_TYPE_INTERVAL,
                       "value-type", gtype,
                       "initial", initial,
                       "final", final,
                       NULL);
}

/**
 * gtd_interval_clone:
 * @interval: a #GtdInterval
 *
 * Creates a copy of @interval.
 *
 * Return value: (transfer full): the newly created #GtdInterval
 */
GtdInterval *
gtd_interval_clone (GtdInterval *self)
{
  GtdIntervalPrivate *priv = gtd_interval_get_instance_private (self);
  GtdInterval *retval;
  GType gtype;
  GValue *tmp;

  g_return_val_if_fail (GTD_IS_INTERVAL (self), NULL);
  g_return_val_if_fail (priv->value_type != G_TYPE_INVALID, NULL);

  gtype = priv->value_type;
  retval = g_object_new (GTD_TYPE_INTERVAL, "value-type", gtype, NULL);

  tmp = gtd_interval_peek_initial_value (self);
  gtd_interval_set_initial_value (retval, tmp);

  tmp = gtd_interval_peek_final_value (self);
  gtd_interval_set_final_value (retval, tmp);

  return retval;
}

/**
 * gtd_interval_get_value_type:
 * @interval: a #GtdInterval
 *
 * Retrieves the #GType of the values inside @interval.
 *
 * Return value: the type of the value, or G_TYPE_INVALID
 */
GType
gtd_interval_get_value_type (GtdInterval *self)
{
  GtdIntervalPrivate *priv;

  g_return_val_if_fail (GTD_IS_INTERVAL (self), G_TYPE_INVALID);

  priv = gtd_interval_get_instance_private (self);
  return priv->value_type;
}

/**
 * gtd_interval_set_initial_value: (rename-to gtd_interval_set_initial)
 * @interval: a #GtdInterval
 * @value: a #GValue
 *
 * Sets the initial value of @interval to @value. The value is copied
 * inside the #GtdInterval.
 */
void
gtd_interval_set_initial_value (GtdInterval  *self,
                                const GValue *value)
{
  g_return_if_fail (GTD_IS_INTERVAL (self));
  g_return_if_fail (value != NULL);

  gtd_interval_set_value_internal (self, INITIAL, value);
}

/**
 * gtd_interval_set_initial: (skip)
 * @interval: a #GtdInterval
 * @...: the initial value of the interval.
 *
 * Variadic arguments version of gtd_interval_set_initial_value().
 *
 * This function is meant as a convenience for the C API.
 *
 * Language bindings should use gtd_interval_set_initial_value()
 * instead.
 */
void
gtd_interval_set_initial (GtdInterval *self,
                          ...)
{
  va_list args;

  g_return_if_fail (GTD_IS_INTERVAL (self));

  va_start (args, self);
  gtd_interval_set_initial_internal (self, &args);
  va_end (args);
}

/**
 * gtd_interval_get_initial_value:
 * @interval: a #GtdInterval
 * @value: (out caller-allocates): a #GValue
 *
 * Retrieves the initial value of @interval and copies
 * it into @value.
 *
 * The passed #GValue must be initialized to the value held by
 * the #GtdInterval.
 */
void
gtd_interval_get_initial_value (GtdInterval *self,
                                GValue      *value)
{
  g_return_if_fail (GTD_IS_INTERVAL (self));
  g_return_if_fail (value != NULL);

  gtd_interval_get_value_internal (self, INITIAL, value);
}

/**
 * gtd_interval_peek_initial_value:
 * @interval: a #GtdInterval
 *
 * Gets the pointer to the initial value of @interval
 *
 * Return value: (transfer none): the initial value of the interval.
 *   The value is owned by the #GtdInterval and it should not be
 *   modified or freed
 */
GValue *
gtd_interval_peek_initial_value (GtdInterval *self)
{
  GtdIntervalPrivate *priv;

  g_return_val_if_fail (GTD_IS_INTERVAL (self), NULL);

  priv = gtd_interval_get_instance_private (self);
  return priv->values + INITIAL;
}

/**
 * gtd_interval_set_final_value: (rename-to gtd_interval_set_final)
 * @interval: a #GtdInterval
 * @value: a #GValue
 *
 * Sets the final value of @interval to @value. The value is
 * copied inside the #GtdInterval.
 */
void
gtd_interval_set_final_value (GtdInterval  *self,
                              const GValue *value)
{
  g_return_if_fail (GTD_IS_INTERVAL (self));
  g_return_if_fail (value != NULL);

  gtd_interval_set_value_internal (self, FINAL, value);
}

/**
 * gtd_interval_get_final_value:
 * @interval: a #GtdInterval
 * @value: (out caller-allocates): a #GValue
 *
 * Retrieves the final value of @interval and copies
 * it into @value.
 *
 * The passed #GValue must be initialized to the value held by
 * the #GtdInterval.
 */
void
gtd_interval_get_final_value (GtdInterval *self,
                              GValue      *value)
{
  g_return_if_fail (GTD_IS_INTERVAL (self));
  g_return_if_fail (value != NULL);

  gtd_interval_get_value_internal (self, FINAL, value);
}

/**
 * gtd_interval_set_final: (skip)
 * @interval: a #GtdInterval
 * @...: the final value of the interval
 *
 * Variadic arguments version of gtd_interval_set_final_value().
 *
 * This function is meant as a convenience for the C API.
 *
 * Language bindings should use gtd_interval_set_final_value() instead.
 */
void
gtd_interval_set_final (GtdInterval *self,
                        ...)
{
  va_list args;

  g_return_if_fail (GTD_IS_INTERVAL (self));

  va_start (args, self);
  gtd_interval_set_final_internal (self, &args);
  va_end (args);
}

/**
 * gtd_interval_peek_final_value:
 * @interval: a #GtdInterval
 *
 * Gets the pointer to the final value of @interval
 *
 * Return value: (transfer none): the final value of the interval.
 *   The value is owned by the #GtdInterval and it should not be
 *   modified or freed
 */
GValue *
gtd_interval_peek_final_value (GtdInterval *self)
{
  GtdIntervalPrivate *priv;

  g_return_val_if_fail (GTD_IS_INTERVAL (self), NULL);

  priv = gtd_interval_get_instance_private (self);
  return priv->values + FINAL;
}

/**
 * gtd_interval_set_interval:
 * @interval: a #GtdInterval
 * @...: the initial and final values of the interval
 *
 * Variable arguments wrapper for gtd_interval_set_initial_value()
 * and gtd_interval_set_final_value() that avoids using the
 * #GValue arguments:
 *
 * |[
 *   gtd_interval_set_interval (self, 0, 50);
 *   gtd_interval_set_interval (self, 1.0, 0.0);
 *   gtd_interval_set_interval (self, FALSE, TRUE);
 * ]|
 *
 * This function is meant for the convenience of the C API; bindings
 * should reimplement this function using the #GValue-based API.
 */
void
gtd_interval_set_interval (GtdInterval *self,
                           ...)
{
  GtdIntervalPrivate *priv = gtd_interval_get_instance_private (self);
  va_list args;

  g_return_if_fail (GTD_IS_INTERVAL (self));
  g_return_if_fail (priv->value_type != G_TYPE_INVALID);

  va_start (args, self);

  if (!gtd_interval_set_initial_internal (self, &args))
    goto out;

  gtd_interval_set_final_internal (self, &args);

out:
  va_end (args);
}

/**
 * gtd_interval_get_interval:
 * @interval: a #GtdInterval
 * @...: return locations for the initial and final values of
 *   the interval
 *
 * Variable arguments wrapper for gtd_interval_get_initial_value()
 * and gtd_interval_get_final_value() that avoids using the
 * #GValue arguments:
 *
 * |[
 *   gint a = 0, b = 0;
 *   gtd_interval_get_interval (self, &a, &b);
 * ]|
 *
 * This function is meant for the convenience of the C API; bindings
 * should reimplement this function using the #GValue-based API.
 */
void
gtd_interval_get_interval (GtdInterval *self,
                               ...)
{
  GtdIntervalPrivate *priv = gtd_interval_get_instance_private (self);
  va_list args;

  g_return_if_fail (GTD_IS_INTERVAL (self));
  g_return_if_fail (priv->value_type != G_TYPE_INVALID);

  va_start (args, self);
  gtd_interval_get_interval_valist (self, args);
  va_end (args);
}

/**
 * gtd_interval_validate:
 * @interval: a #GtdInterval
 * @pspec: a #GParamSpec
 *
 * Validates the initial and final values of @interval against
 * a #GParamSpec.
 *
 * Return value: %TRUE if the #GtdInterval is valid, %FALSE otherwise
 */
gboolean
gtd_interval_validate (GtdInterval *self,
                           GParamSpec      *pspec)
{
  g_return_val_if_fail (GTD_IS_INTERVAL (self), FALSE);
  g_return_val_if_fail (G_IS_PARAM_SPEC (pspec), FALSE);

  return GTD_INTERVAL_GET_CLASS (self)->validate (self, pspec);
}

/**
 * gtd_interval_compute_value:
 * @interval: a #GtdInterval
 * @factor: the progress factor, between 0 and 1
 * @value: (out caller-allocates): return location for an initialized #GValue
 *
 * Computes the value between the @interval boundaries given the
 * progress @factor and copies it into @value.
 *
 * Return value: %TRUE if the operation was successful
 */
gboolean
gtd_interval_compute_value (GtdInterval *self,
                            gdouble      factor,
                            GValue      *value)
{
  g_return_val_if_fail (GTD_IS_INTERVAL (self), FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  return GTD_INTERVAL_GET_CLASS (self)->compute_value (self, factor, value);
}

/**
 * gtd_interval_compute:
 * @interval: a #GtdInterval
 * @factor: the progress factor, between 0 and 1
 *
 * Computes the value between the @interval boundaries given the
 * progress @factor
 *
 * Unlike gtd_interval_compute_value(), this function will
 * return a const pointer to the computed value
 *
 * You should use this function if you immediately pass the computed
 * value to another function that makes a copy of it, like
 * g_object_set_property()
 *
 * Return value: (transfer none): a pointer to the computed value,
 *   or %NULL if the computation was not successfull
 */
const GValue *
gtd_interval_compute (GtdInterval *self,
                      gdouble      factor)
{
  GtdIntervalPrivate *priv = gtd_interval_get_instance_private (self);
  GValue *value;
  gboolean res;

  g_return_val_if_fail (GTD_IS_INTERVAL (self), NULL);

  value = &(priv->values[RESULT]);

  if (G_VALUE_TYPE (value) == G_TYPE_INVALID)
    g_value_init (value, priv->value_type);

  res = GTD_INTERVAL_GET_CLASS (self)->compute_value (self, factor, value);

  if (res)
    return priv->values + RESULT;

  return NULL;
}

/**
 * gtd_interval_is_valid:
 * @interval: a #GtdInterval
 *
 * Checks if the @interval has a valid initial and final values.
 *
 * Return value: %TRUE if the #GtdInterval has an initial and
 *   final values, and %FALSE otherwise
 */
gboolean
gtd_interval_is_valid (GtdInterval *self)
{
  GtdIntervalPrivate *priv;

  g_return_val_if_fail (GTD_IS_INTERVAL (self), FALSE);

  priv = gtd_interval_get_instance_private (self);

  return G_IS_VALUE (&priv->values[INITIAL]) &&
         G_IS_VALUE (&priv->values[FINAL]);
}
