/* gtd-widget.c
 *
 * Copyright 2018-2020 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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

#include "gtd-animatable.h"
#include "gtd-bin-layout.h"
#include "gtd-debug.h"
#include "gtd-animation-enums.h"
#include "gtd-interval.h"
#include "gtd-timeline.h"
#include "gtd-property-transition.h"

#include <graphene-gobject.h>
#include <gobject/gvaluecollector.h>

enum
{
  X,
  Y,
  Z,
};

typedef struct
{
  guint easing_duration;
  guint easing_delay;
  GtdEaseMode easing_mode;
} AnimationState;

typedef struct
{
  struct {
    GHashTable *transitions;
    GArray *states;
    AnimationState *current_state;
  } animation;

  graphene_point3d_t  pivot_point;
  gfloat              rotation[3];
  gfloat              scale[3];
  graphene_point3d_t  translation;

  GskTransform       *cached_transform;
} GtdWidgetPrivate;

static void set_animatable_property (GtdWidget    *self,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec);

static void gtd_animatable_iface_init (GtdAnimatableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (GtdWidget, gtd_widget, GTK_TYPE_WIDGET,
                         G_ADD_PRIVATE (GtdWidget)
                         G_IMPLEMENT_INTERFACE (GTD_TYPE_ANIMATABLE, gtd_animatable_iface_init))

enum
{
  PROP_0,
  PROP_PIVOT_POINT,
  PROP_ROTATION_X,
  PROP_ROTATION_Y,
  PROP_ROTATION_Z,
  PROP_SCALE_X,
  PROP_SCALE_Y,
  PROP_SCALE_Z,
  PROP_TRANSLATION_X,
  PROP_TRANSLATION_Y,
  PROP_TRANSLATION_Z,
  N_PROPS
};

enum
{
  TRANSITION_STOPPED,
  TRANSITIONS_COMPLETED,
  NUM_SIGNALS
};


static guint signals[NUM_SIGNALS] = { 0, };

static GParamSpec *properties [N_PROPS] = { NULL, };


/*
 * Auxiliary methods
 */

typedef struct
{
  GtdWidget *widget;
  GtdTransition *transition;
  gchar *name;
  gulong completed_id;
} TransitionClosure;

static void
transition_closure_free (gpointer data)
{
  if (G_LIKELY (data != NULL))
    {
      TransitionClosure *closure = data;
      GtdTimeline *timeline;

      timeline = GTD_TIMELINE (closure->transition);

      /* we disconnect the signal handler before stopping the timeline,
       * so that we don't end up inside on_transition_stopped() from
       * a call to g_hash_table_remove().
       */
      g_clear_signal_handler (&closure->completed_id, closure->transition);

      if (gtd_timeline_is_playing (timeline))
        gtd_timeline_stop (timeline);

      g_object_unref (closure->transition);

      g_free (closure->name);

      g_slice_free (TransitionClosure, closure);
    }
}

static void
on_transition_stopped_cb (GtdTransition     *transition,
                          gboolean           is_finished,
                          TransitionClosure *closure)
{
  GtdWidget *self = closure->widget;
  GtdWidgetPrivate *priv = gtd_widget_get_instance_private (self);
  GQuark t_quark;
  gchar *t_name;

  if (closure->name == NULL)
    return;

  /* we need copies because we emit the signal after the
   * TransitionClosure data structure has been freed
   */
  t_quark = g_quark_from_string (closure->name);
  t_name = g_strdup (closure->name);

  if (gtd_transition_get_remove_on_complete (transition))
    {
      /* this is safe, because the timeline has now stopped,
       * so we won't recurse; the reference on the Animatable
       * will be dropped by the ::stopped signal closure in
       * GtdTransition, which is RUN_LAST, and thus will
       * be called after this handler
       */
      g_hash_table_remove (priv->animation.transitions, closure->name);
    }

  /* we emit the ::transition-stopped after removing the
   * transition, so that we can chain up new transitions
   * without interfering with the one that just finished
   */
  g_signal_emit (self, signals[TRANSITION_STOPPED], t_quark, t_name, is_finished);

  g_free (t_name);

  /* if it's the last transition then we clean up */
  if (g_hash_table_size (priv->animation.transitions) == 0)
    {
      g_hash_table_unref (priv->animation.transitions);
      priv->animation.transitions = NULL;

      GTD_TRACE_MSG ("[animation] Transitions for '%p' completed", self);

      g_signal_emit (self, signals[TRANSITIONS_COMPLETED], 0);
    }
}

static void
add_transition_to_widget (GtdWidget     *self,
                          const gchar   *name,
                          GtdTransition *transition)
{
  GtdWidgetPrivate *priv = gtd_widget_get_instance_private (self);
  TransitionClosure *closure;
  GtdTimeline *timeline;

  GTD_ENTRY;

  if (!priv->animation.transitions)
    {
      priv->animation.transitions = g_hash_table_new_full (g_str_hash,
                                                           g_str_equal,
                                                           NULL,
                                                           transition_closure_free);
    }

  if (g_hash_table_lookup (priv->animation.transitions, name) != NULL)
    {
      g_warning ("A transition with name '%s' already exists for the widget '%p'",
                 name,
                 self);
      GTD_RETURN ();
    }

  gtd_transition_set_animatable (transition, GTD_ANIMATABLE (self));

  timeline = GTD_TIMELINE (transition);

  closure = g_slice_new (TransitionClosure);
  closure->widget = self;
  closure->transition = g_object_ref (transition);
  closure->name = g_strdup (name);
  closure->completed_id = g_signal_connect (timeline,
                                            "stopped",
                                            G_CALLBACK (on_transition_stopped_cb),
                                            closure);

  GTD_TRACE_MSG ("[animation] Adding transition '%s' [%p] to widget %p",
                closure->name,
                closure->transition,
                self);

  g_hash_table_insert (priv->animation.transitions, closure->name, closure);
  gtd_timeline_start (timeline);

  GTD_EXIT;
}

static gboolean
should_skip_implicit_transition (GtdWidget  *self,
                                 GParamSpec *pspec)
{
  GtdWidgetPrivate *priv = gtd_widget_get_instance_private (self);

  /* if the easing state has a non-zero duration we always want an
   * implicit transition to occur
   */
  if (priv->animation.current_state->easing_duration == 0)
    return TRUE;

  /* if the widget is not mapped and is not part of a branch of the scene
   * graph that is being cloned, then we always skip implicit transitions
   * on the account of the fact that the widget is not going to be visible
   * when those transitions happen
   */
  if (!gtk_widget_get_mapped (GTK_WIDGET (self)))
    return TRUE;

  return FALSE;
}

static GtdTransition*
create_transition (GtdWidget  *self,
                   GParamSpec *pspec,
                   ...)
{
  GtdWidgetPrivate *priv = gtd_widget_get_instance_private (self);
  g_autofree gchar *error = NULL;
  g_auto (GValue) initial = G_VALUE_INIT;
  g_auto (GValue) final = G_VALUE_INIT;
  TransitionClosure *closure;
  GtdTimeline *timeline;
  GtdInterval *interval;
  GtdTransition *res = NULL;
  va_list var_args;
  GType ptype;

  g_assert (pspec != NULL);

  if (!priv->animation.transitions)
    {
      priv->animation.transitions = g_hash_table_new_full (g_str_hash,
                                                           g_str_equal,
                                                           NULL,
                                                           transition_closure_free);
    }

  va_start (var_args, pspec);

  ptype = G_PARAM_SPEC_VALUE_TYPE (pspec);

  G_VALUE_COLLECT_INIT (&initial, ptype, var_args, 0, &error);
  if (error != NULL)
    {
      g_critical ("%s: %s", G_STRLOC, error);
      goto out;
    }

  G_VALUE_COLLECT_INIT (&final, ptype, var_args, 0, &error);
  if (error != NULL)
    {
      g_critical ("%s: %s", G_STRLOC, error);
      goto out;
    }

  if (should_skip_implicit_transition (self, pspec))
    {
      GTD_TRACE_MSG ("[animation] Skipping implicit transition for '%p::%s'",
                    self,
                    pspec->name);

      /* remove a transition, if one exists */
      gtd_widget_remove_transition (self, pspec->name);

      /* we don't go through the Animatable interface because we
       * already know we got here through an animatable property.
       */
      set_animatable_property (self, pspec->param_id, &final, pspec);

      goto out;
    }

  closure = g_hash_table_lookup (priv->animation.transitions, pspec->name);
  if (closure == NULL)
    {
      res = gtd_property_transition_new (pspec->name);

      gtd_transition_set_remove_on_complete (res, TRUE);

      interval = gtd_interval_new_with_values (ptype, &initial, &final);
      gtd_transition_set_interval (res, interval);

      timeline = GTD_TIMELINE (res);
      gtd_timeline_set_delay (timeline, priv->animation.current_state->easing_delay);
      gtd_timeline_set_duration (timeline, priv->animation.current_state->easing_duration);
      gtd_timeline_set_progress_mode (timeline, priv->animation.current_state->easing_mode);

      /* this will start the transition as well */
      add_transition_to_widget (self, pspec->name, res);

      /* the widget now owns the transition */
      g_object_unref (res);
    }
  else
    {
      GtdEaseMode cur_mode;
      guint cur_duration;

      GTD_TRACE_MSG ("[animation] Existing transition for %p:%s",
                    self,
                    pspec->name);

      timeline = GTD_TIMELINE (closure->transition);

      cur_duration = gtd_timeline_get_duration (timeline);
      if (cur_duration != priv->animation.current_state->easing_duration)
        gtd_timeline_set_duration (timeline, priv->animation.current_state->easing_duration);

      cur_mode = gtd_timeline_get_progress_mode (timeline);
      if (cur_mode != priv->animation.current_state->easing_mode)
        gtd_timeline_set_progress_mode (timeline, priv->animation.current_state->easing_mode);

      gtd_timeline_rewind (timeline);

      interval = gtd_transition_get_interval (closure->transition);
      gtd_interval_set_initial_value (interval, &initial);
      gtd_interval_set_final_value (interval, &final);

      res = closure->transition;
    }

out:
  va_end (var_args);

  return res;
}

static void
invalidate_cached_transform (GtdWidget *self)
{
  GtdWidgetPrivate *priv = gtd_widget_get_instance_private (self);

  g_clear_pointer (&priv->cached_transform, gsk_transform_unref);
}

static void
calculate_transform (GtdWidget *self)
{
  GtdWidgetPrivate *priv = gtd_widget_get_instance_private (self);
  graphene_point3d_t pivot;
  GskTransform *transform;
  gboolean pivot_is_zero;
  gint height;
  gint width;

  transform = NULL;
  width = gtk_widget_get_width (GTK_WIDGET (self));
  height = gtk_widget_get_height (GTK_WIDGET (self));

  /* Pivot point */
  pivot_is_zero = graphene_point3d_equal (&priv->pivot_point, graphene_point3d_zero ());
  pivot = GRAPHENE_POINT3D_INIT (width * priv->pivot_point.x,
                                 height * priv->pivot_point.y,
                                 priv->pivot_point.z);
  if (!pivot_is_zero)
    transform = gsk_transform_translate_3d (transform, &pivot);

  /* Perspective */
  transform = gsk_transform_perspective (transform, 2 * MAX (width, height));

  /* Translation */
  if (G_APPROX_VALUE (priv->translation.z, 0.f, FLT_EPSILON))
    {
      transform = gsk_transform_translate (transform,
                                           &GRAPHENE_POINT_INIT (priv->translation.x,
                                                                 priv->translation.y));
    }
  else
    {
      transform = gsk_transform_translate_3d (transform, &priv->translation);
    }

  /* Scale */
  if (G_APPROX_VALUE (priv->scale[Z], 1.f, FLT_EPSILON))
    transform = gsk_transform_scale (transform, priv->scale[X], priv->scale[Y]);
  else
    transform = gsk_transform_scale_3d (transform, priv->scale[X], priv->scale[Y], priv->scale[Z]);

  /* Rotation */
  transform = gsk_transform_rotate_3d (transform, priv->rotation[X], graphene_vec3_x_axis ());
  transform = gsk_transform_rotate_3d (transform, priv->rotation[Y], graphene_vec3_y_axis ());
  transform = gsk_transform_rotate_3d (transform, priv->rotation[Z], graphene_vec3_z_axis ());

  /* Rollback pivot point */
  if (!pivot_is_zero)
    transform = gsk_transform_translate_3d (transform,
                                            &GRAPHENE_POINT3D_INIT (-pivot.x,
                                                                    -pivot.y,
                                                                    -pivot.z));

  priv->cached_transform = transform;
}

static void
set_rotation_internal (GtdWidget *self,
                       gfloat     rotation_x,
                       gfloat     rotation_y,
                       gfloat     rotation_z)
{
  GtdWidgetPrivate *priv;
  gboolean changed[3];

  GTD_ENTRY;

  g_return_if_fail (GTD_IS_WIDGET (self));

  priv = gtd_widget_get_instance_private (self);

  changed[X] = !G_APPROX_VALUE (priv->rotation[X], rotation_x, FLT_EPSILON);
  changed[Y] = !G_APPROX_VALUE (priv->rotation[Y], rotation_y, FLT_EPSILON);
  changed[Z] = !G_APPROX_VALUE (priv->rotation[Z], rotation_z, FLT_EPSILON);

  if (!changed[X] && !changed[Y] && !changed[Z])
    GTD_RETURN ();

  invalidate_cached_transform (self);

  priv->rotation[X] = rotation_x;
  priv->rotation[Y] = rotation_y;
  priv->rotation[Z] = rotation_z;

  if (changed[X])
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ROTATION_X]);

  if (changed[Y])
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ROTATION_Y]);

  if (changed[Z])
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ROTATION_Z]);

  gtk_widget_queue_resize (GTK_WIDGET (self));

  GTD_EXIT;
}

static void
set_scale_internal (GtdWidget *self,
                    gfloat     scale_x,
                    gfloat     scale_y,
                    gfloat     scale_z)
{
  GtdWidgetPrivate *priv;
  gboolean changed[3];

  GTD_ENTRY;

  g_return_if_fail (GTD_IS_WIDGET (self));

  priv = gtd_widget_get_instance_private (self);

  changed[X] = !G_APPROX_VALUE (priv->scale[X], scale_x, FLT_EPSILON);
  changed[Y] = !G_APPROX_VALUE (priv->scale[Y], scale_y, FLT_EPSILON);
  changed[Z] = !G_APPROX_VALUE (priv->scale[Z], scale_z, FLT_EPSILON);

  if (!changed[X] && !changed[Y] && !changed[Z])
    GTD_RETURN ();

  invalidate_cached_transform (self);

  priv->scale[X] = scale_x;
  priv->scale[Y] = scale_y;
  priv->scale[Z] = scale_z;

  if (changed[X])
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCALE_X]);

  if (changed[Y])
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCALE_Y]);

  if (changed[Z])
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCALE_Z]);

  gtk_widget_queue_resize (GTK_WIDGET (self));

  GTD_EXIT;
}

static void
set_translation_internal (GtdWidget *self,
                          gfloat     translation_x,
                          gfloat     translation_y,
                          gfloat     translation_z)
{
  graphene_point3d_t old_translation, translation;
  GtdWidgetPrivate *priv;

  GTD_ENTRY;

  g_return_if_fail (GTD_IS_WIDGET (self));

  priv = gtd_widget_get_instance_private (self);
  translation = GRAPHENE_POINT3D_INIT (translation_x, translation_y, translation_z);

  if (graphene_point3d_equal (&priv->translation, &translation))
    GTD_RETURN ();

  old_translation = priv->translation;

  invalidate_cached_transform (self);
  priv->translation = translation;

  if (!G_APPROX_VALUE (old_translation.x, translation.x, FLT_EPSILON))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TRANSLATION_X]);

  if (!G_APPROX_VALUE (old_translation.y, translation.y, FLT_EPSILON))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TRANSLATION_Y]);

  if (!G_APPROX_VALUE (old_translation.y, translation.y, FLT_EPSILON))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TRANSLATION_Z]);

  gtk_widget_queue_resize (GTK_WIDGET (self));

  GTD_EXIT;
}

static void
set_animatable_property (GtdWidget    *self,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  GtdWidgetPrivate *priv = gtd_widget_get_instance_private (self);
  GObject *object = G_OBJECT (self);

  g_object_freeze_notify (object);

  switch (prop_id)
    {
    case PROP_ROTATION_X:
      set_rotation_internal (self, g_value_get_float (value), priv->rotation[Y], priv->rotation[Z]);
      break;

    case PROP_ROTATION_Y:
      set_rotation_internal (self, priv->rotation[X], g_value_get_float (value), priv->rotation[Z]);
      break;

    case PROP_ROTATION_Z:
      set_rotation_internal (self, priv->rotation[X], priv->rotation[Y], g_value_get_float (value));
      break;

    case PROP_SCALE_X:
      set_scale_internal (self, g_value_get_float (value), priv->scale[Y], priv->scale[Z]);
      break;

    case PROP_SCALE_Y:
      set_scale_internal (self, priv->scale[X], g_value_get_float (value), priv->scale[Z]);
      break;

    case PROP_SCALE_Z:
      set_scale_internal (self, priv->scale[X], priv->scale[Y], g_value_get_float (value));
      break;

    case PROP_TRANSLATION_X:
      set_translation_internal (self, g_value_get_float (value), priv->translation.y, priv->translation.z);
      break;

    case PROP_TRANSLATION_Y:
      set_translation_internal (self, priv->translation.x, g_value_get_float (value), priv->translation.z);
      break;

    case PROP_TRANSLATION_Z:
      set_translation_internal (self, priv->translation.x, priv->translation.y, g_value_get_float (value));
      break;

    default:
      g_object_set_property (object, pspec->name, value);
      break;
    }

  g_object_thaw_notify (object);
}

/*
 * GtdAnimatable interface
 */

static GParamSpec *
gtd_widget_find_property (GtdAnimatable *animatable,
                          const gchar   *property_name)
{
  return g_object_class_find_property (G_OBJECT_GET_CLASS (animatable), property_name);
}

static void
gtd_widget_get_initial_state (GtdAnimatable *animatable,
                              const gchar   *property_name,
                              GValue        *initial)
{
  g_object_get_property (G_OBJECT (animatable), property_name, initial);
}

static void
gtd_widget_set_final_state (GtdAnimatable *animatable,
                            const gchar   *property_name,
                            const GValue  *final)
{
  GObjectClass *obj_class = G_OBJECT_GET_CLASS (animatable);
  GParamSpec *pspec;

  pspec = g_object_class_find_property (obj_class, property_name);

  if (pspec)
    set_animatable_property (GTD_WIDGET (animatable), pspec->param_id, final, pspec);
}

static GtdWidget*
gtd_widget_get_widget (GtdAnimatable *animatable)
{
  return GTD_WIDGET (animatable);
}

static void
gtd_animatable_iface_init (GtdAnimatableInterface *iface)
{
  iface->find_property = gtd_widget_find_property;
  iface->get_initial_state = gtd_widget_get_initial_state;
  iface->set_final_state = gtd_widget_set_final_state;
  iface->get_widget = gtd_widget_get_widget;
}


/*
 * GObject overrides
 */

static void
gtd_widget_dispose (GObject *object)
{
  GtdWidget *self = GTD_WIDGET (object);
  GtkWidget *child = gtk_widget_get_first_child (GTK_WIDGET (object));

  while (child)
    {
      GtkWidget *next = gtk_widget_get_next_sibling (child);

      gtk_widget_unparent (child);
      child = next;
    }

  invalidate_cached_transform (self);
  gtd_widget_remove_all_transitions (self);

  G_OBJECT_CLASS (gtd_widget_parent_class)->dispose (object);
}

static void
gtd_widget_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  GtdWidget *self = GTD_WIDGET (object);
  GtdWidgetPrivate *priv = gtd_widget_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_PIVOT_POINT:
      g_value_set_boxed (value, &priv->pivot_point);
      break;

    case PROP_ROTATION_X:
      g_value_set_float (value, priv->rotation[X]);
      break;

    case PROP_ROTATION_Y:
      g_value_set_float (value, priv->rotation[Y]);
      break;

    case PROP_ROTATION_Z:
      g_value_set_float (value, priv->rotation[Z]);
      break;

    case PROP_SCALE_X:
      g_value_set_float (value, priv->scale[X]);
      break;

    case PROP_SCALE_Y:
      g_value_set_float (value, priv->scale[Y]);
      break;

    case PROP_SCALE_Z:
      g_value_set_float (value, priv->scale[Z]);
      break;

    case PROP_TRANSLATION_X:
      g_value_set_float (value, priv->translation.x);
      break;

    case PROP_TRANSLATION_Y:
      g_value_set_float (value, priv->translation.y);
      break;

    case PROP_TRANSLATION_Z:
      g_value_set_float (value, priv->translation.z);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_widget_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  GtdWidget *self = GTD_WIDGET (object);
  GtdWidgetPrivate *priv = gtd_widget_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_PIVOT_POINT:
      gtd_widget_set_pivot_point (self, g_value_get_boxed (value));
      break;

    case PROP_ROTATION_X:
      gtd_widget_set_rotation (self, g_value_get_float (value), priv->rotation[Y], priv->rotation[Z]);
      break;

    case PROP_ROTATION_Y:
      gtd_widget_set_rotation (self, priv->rotation[X], g_value_get_float (value), priv->rotation[Z]);
      break;

    case PROP_ROTATION_Z:
      gtd_widget_set_rotation (self, priv->rotation[X], priv->rotation[Y], g_value_get_float (value));
      break;

    case PROP_SCALE_X:
      gtd_widget_set_scale (self, g_value_get_float (value), priv->scale[Y], priv->scale[Z]);
      break;

    case PROP_SCALE_Y:
      gtd_widget_set_scale (self, priv->scale[X], g_value_get_float (value), priv->scale[Z]);
      break;

    case PROP_SCALE_Z:
      gtd_widget_set_scale (self, priv->scale[X], priv->scale[Y], g_value_get_float (value));
      break;

    case PROP_TRANSLATION_X:
      gtd_widget_set_translation (self, g_value_get_float (value), priv->translation.y, priv->translation.z);
      break;

    case PROP_TRANSLATION_Y:
      gtd_widget_set_translation (self, priv->translation.x, g_value_get_float (value), priv->translation.z);
      break;

    case PROP_TRANSLATION_Z:
      gtd_widget_set_translation (self, priv->translation.x, priv->translation.y, g_value_get_float (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_widget_class_init (GtdWidgetClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = gtd_widget_dispose;
  object_class->get_property = gtd_widget_get_property;
  object_class->set_property = gtd_widget_set_property;

  /**
   * GtdWidget:
   */
  properties[PROP_PIVOT_POINT] = g_param_spec_boxed ("pivot-point",
                                                     "Pivot point",
                                                     "Pivot point",
                                                     GRAPHENE_TYPE_POINT3D,
                                                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GtdWidget:rotation-x
   */
  properties[PROP_ROTATION_X] = g_param_spec_float ("rotation-x",
                                                    "Rotation in the X axis",
                                                    "Rotation in the X axis",
                                                    -G_MAXFLOAT,
                                                    G_MAXFLOAT,
                                                    0.f,
                                                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GtdWidget:rotation-y
   */
  properties[PROP_ROTATION_Y] = g_param_spec_float ("rotation-y",
                                                    "Rotation in the Y axis",
                                                    "Rotation in the Y axis",
                                                    -G_MAXFLOAT,
                                                    G_MAXFLOAT,
                                                    0.f,
                                                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GtdWidget:rotation-z
   */
  properties[PROP_ROTATION_Z] = g_param_spec_float ("rotation-z",
                                                    "Rotation in the Z axis",
                                                    "Rotation in the Z axis",
                                                    -G_MAXFLOAT,
                                                    G_MAXFLOAT,
                                                    0.f,
                                                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GtdWidget:scale-x
   */
  properties[PROP_SCALE_X] = g_param_spec_float ("scale-x",
                                                 "Scale in the X axis",
                                                 "Scale in the X axis",
                                                 -G_MAXFLOAT,
                                                 G_MAXFLOAT,
                                                 1.f,
                                                 G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GtdWidget:scale-y
   */
  properties[PROP_SCALE_Y] = g_param_spec_float ("scale-y",
                                                 "Scale in the Y axis",
                                                 "Scale in the Y axis",
                                                 -G_MAXFLOAT,
                                                 G_MAXFLOAT,
                                                 1.f,
                                                 G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GtdWidget:scale-z
   */
  properties[PROP_SCALE_Z] = g_param_spec_float ("scale-z",
                                                 "Scale in the Z axis",
                                                 "Scale in the Z axis",
                                                 -G_MAXFLOAT,
                                                 G_MAXFLOAT,
                                                 1.f,
                                                 G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GtdWidget:translation-x
   */
  properties[PROP_TRANSLATION_X] = g_param_spec_float ("translation-x",
                                                       "Translation in the X axis",
                                                       "Translation in the X axis",
                                                       -G_MAXFLOAT,
                                                       G_MAXFLOAT,
                                                       0.f,
                                                       G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GtdWidget:translation-y
   */
  properties[PROP_TRANSLATION_Y] = g_param_spec_float ("translation-y",
                                                       "Translation in the Y axis",
                                                       "Translation in the Y axis",
                                                       -G_MAXFLOAT,
                                                       G_MAXFLOAT,
                                                       0.f,
                                                       G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GtdWidget:translation-z
   */
  properties[PROP_TRANSLATION_Z] = g_param_spec_float ("translation-z",
                                                       "Translation in the Z axis",
                                                       "Translation in the Z axis",
                                                       -G_MAXFLOAT,
                                                       G_MAXFLOAT,
                                                       0.f,
                                                       G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);

  /**
   * GtdWidget::transitions-completed:
   * @actor: a #GtdWidget
   *
   * The ::transitions-completed signal is emitted once all transitions
   * involving @actor are complete.
   *
   * Since: 1.10
   */
  signals[TRANSITIONS_COMPLETED] =
    g_signal_new ("transitions-completed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  0);

  /**
   * GtdWidget::transition-stopped:
   * @actor: a #GtdWidget
   * @name: the name of the transition
   * @is_finished: whether the transition was finished, or stopped
   *
   * The ::transition-stopped signal is emitted once a transition
   * is stopped; a transition is stopped once it reached its total
   * duration (including eventual repeats), it has been stopped
   * using gtd_timeline_stop(), or it has been removed from the
   * transitions applied on @actor, using gtd_actor_remove_transition().
   *
   * Since: 1.12
   */
  signals[TRANSITION_STOPPED] =
    g_signal_new ("transition-stopped",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE |
                  G_SIGNAL_NO_HOOKS | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  2,
                  G_TYPE_STRING,
                  G_TYPE_BOOLEAN);

  gtk_widget_class_set_layout_manager_type (widget_class, GTD_TYPE_BIN_LAYOUT);
}

static void
gtd_widget_init (GtdWidget *self)
{
  GtdWidgetPrivate *priv = gtd_widget_get_instance_private (self);

  priv->scale[X] = 1.f;
  priv->scale[Y] = 1.f;
  priv->scale[Z] = 1.f;

  priv->pivot_point = GRAPHENE_POINT3D_INIT (0.5, 0.5, 0.f);

  gtd_widget_save_easing_state (self);
  gtd_widget_set_easing_duration (self, 0);
}

GtkWidget*
gtd_widget_new (void)
{
  return g_object_new (GTD_TYPE_WIDGET, NULL);
}

/**
 */
void
gtd_widget_get_pivot_point (GtdWidget          *self,
                            graphene_point3d_t *out_pivot_point)
{
  GtdWidgetPrivate *priv;

  g_return_if_fail (GTD_IS_WIDGET (self));
  g_return_if_fail (out_pivot_point != NULL);

  priv = gtd_widget_get_instance_private (self);
  *out_pivot_point = priv->pivot_point;
}

/**
 */
void
gtd_widget_set_pivot_point (GtdWidget                *self,
                            const graphene_point3d_t *pivot_point)
{
  GtdWidgetPrivate *priv;

  GTD_ENTRY;

  g_return_if_fail (GTD_IS_WIDGET (self));
  g_return_if_fail (pivot_point != NULL);
  g_return_if_fail (pivot_point->x >= 0.f && pivot_point->x <= 1.0);
  g_return_if_fail (pivot_point->y >= 0.f && pivot_point->y <= 1.0);

  priv = gtd_widget_get_instance_private (self);

  if (graphene_point3d_equal (&priv->pivot_point, pivot_point))
    GTD_RETURN ();

  invalidate_cached_transform (self);
  priv->pivot_point = *pivot_point;

  gtk_widget_queue_resize (GTK_WIDGET (self));

  GTD_EXIT;
}

/**
 */
void
gtd_widget_get_rotation (GtdWidget *self,
                         gfloat    *rotation_x,
                         gfloat    *rotation_y,
                         gfloat    *rotation_z)
{
  GtdWidgetPrivate *priv;

  g_return_if_fail (GTD_IS_WIDGET (self));

  priv = gtd_widget_get_instance_private (self);

  if (rotation_x)
    *rotation_x = priv->rotation[X];

  if (rotation_y)
    *rotation_y = priv->rotation[Y];

  if (rotation_z)
    *rotation_z = priv->rotation[Z];
}

/**
 */
void
gtd_widget_set_rotation (GtdWidget *self,
                         gfloat     rotation_x,
                         gfloat     rotation_y,
                         gfloat     rotation_z)
{
  GtdWidgetPrivate *priv;
  gboolean changed[3];

  GTD_ENTRY;

  g_return_if_fail (GTD_IS_WIDGET (self));

  priv = gtd_widget_get_instance_private (self);

  changed[X] = !G_APPROX_VALUE (priv->rotation[X], rotation_x, FLT_EPSILON);
  changed[Y] = !G_APPROX_VALUE (priv->rotation[Y], rotation_y, FLT_EPSILON);
  changed[Z] = !G_APPROX_VALUE (priv->rotation[Z], rotation_z, FLT_EPSILON);

  if (!changed[X] && !changed[Y] && !changed[Z])
    GTD_RETURN ();

  if (changed[X])
    create_transition (self, properties[PROP_ROTATION_X], priv->rotation[X], rotation_x);

  if (changed[Y])
    create_transition (self, properties[PROP_ROTATION_Y], priv->rotation[Y], rotation_y);

  if (changed[Z])
    create_transition (self, properties[PROP_ROTATION_Z], priv->rotation[Z], rotation_z);

  GTD_EXIT;
}

/**
 */
void
gtd_widget_get_scale (GtdWidget *self,
                      gfloat    *scale_x,
                      gfloat    *scale_y,
                      gfloat    *scale_z)
{
  GtdWidgetPrivate *priv;

  g_return_if_fail (GTD_IS_WIDGET (self));

  priv = gtd_widget_get_instance_private (self);

  if (scale_x)
    *scale_x = priv->scale[0];

  if (scale_y)
    *scale_y = priv->scale[1];

  if (scale_z)
    *scale_z = priv->scale[2];
}

/**
 */
void
gtd_widget_set_scale (GtdWidget *self,
                      gfloat     scale_x,
                      gfloat     scale_y,
                      gfloat     scale_z)
{
  GtdWidgetPrivate *priv;
  gboolean changed[3];

  GTD_ENTRY;

  g_return_if_fail (GTD_IS_WIDGET (self));

  priv = gtd_widget_get_instance_private (self);

  changed[X] = !G_APPROX_VALUE (priv->scale[X], scale_x, FLT_EPSILON);
  changed[Y] = !G_APPROX_VALUE (priv->scale[Y], scale_y, FLT_EPSILON);
  changed[Z] = !G_APPROX_VALUE (priv->scale[Z], scale_z, FLT_EPSILON);

  if (!changed[X] && !changed[Y] && !changed[Z])
    GTD_RETURN ();

  if (changed[X])
    create_transition (self, properties[PROP_SCALE_X], priv->scale[X], scale_x);

  if (changed[Y])
    create_transition (self, properties[PROP_SCALE_Y], priv->scale[Y], scale_y);

  if (changed[Z])
    create_transition (self, properties[PROP_SCALE_Z], priv->scale[Z], scale_z);

  GTD_EXIT;
}

/**
 */
void
gtd_widget_get_translation (GtdWidget *self,
                            gfloat    *translation_x,
                            gfloat    *translation_y,
                            gfloat    *translation_z)
{
  GtdWidgetPrivate *priv;

  g_return_if_fail (GTD_IS_WIDGET (self));

  priv = gtd_widget_get_instance_private (self);

  if (translation_x)
    *translation_x = priv->translation.x;

  if (translation_y)
    *translation_y = priv->translation.y;

  if (translation_z)
    *translation_z = priv->translation.z;
}

/**
 */
void
gtd_widget_set_translation (GtdWidget *self,
                            gfloat     translation_x,
                            gfloat     translation_y,
                            gfloat     translation_z)
{
  graphene_point3d_t translation;
  GtdWidgetPrivate *priv;

  GTD_ENTRY;

  g_return_if_fail (GTD_IS_WIDGET (self));

  priv = gtd_widget_get_instance_private (self);
  translation = GRAPHENE_POINT3D_INIT (translation_x, translation_y, translation_z);

  if (graphene_point3d_equal (&priv->translation, &translation))
    GTD_RETURN ();

  if (!G_APPROX_VALUE (priv->translation.x, translation.x, FLT_EPSILON))
    create_transition (self, properties[PROP_TRANSLATION_X], priv->translation.x, translation_x);

  if (!G_APPROX_VALUE (priv->translation.y, translation.y, FLT_EPSILON))
    create_transition (self, properties[PROP_TRANSLATION_Y], priv->translation.y, translation_y);

  if (!G_APPROX_VALUE (priv->translation.y, translation.y, FLT_EPSILON))
    create_transition (self, properties[PROP_TRANSLATION_Z], priv->translation.z, translation_z);

  GTD_EXIT;
}

/**
 */
GskTransform*
gtd_widget_apply_transform (GtdWidget    *self,
                            GskTransform *transform)
{
  GtdWidgetPrivate *priv;

  g_return_val_if_fail (GTD_IS_WIDGET (self), NULL);

  priv = gtd_widget_get_instance_private (self);

  if (!priv->cached_transform)
    calculate_transform (self);

  if (!transform)
    return gsk_transform_ref (priv->cached_transform);

  return gsk_transform_transform (transform, priv->cached_transform);
}

/**
 * gtd_widget_add_transition:
 * @self: a #GtdWidget
 * @name: the name of the transition to add
 * @transition: the #GtdTransition to add
 *
 * Adds a @transition to the #GtdWidget's list of animations.
 *
 * The @name string is a per-widget unique identifier of the @transition: only
 * one #GtdTransition can be associated to the specified @name.
 *
 * The @transition will be started once added.
 *
 * This function will take a reference on the @transition.
 *
 * This function is usually called implicitly when modifying an animatable
 * property.
 *
 * Since: 1.10
 */
void
gtd_widget_add_transition (GtdWidget     *self,
                           const gchar   *name,
                           GtdTransition *transition)
{
  g_return_if_fail (GTD_IS_WIDGET (self));
  g_return_if_fail (name != NULL);
  g_return_if_fail (GTD_IS_TRANSITION (transition));

  add_transition_to_widget (self, name, transition);
}

/**
 * gtd_widget_remove_transition:
 * @self: a #GtdWidget
 * @name: the name of the transition to remove
 *
 * Removes the transition stored inside a #GtdWidget using @name
 * identifier.
 *
 * If the transition is currently in progress, it will be stopped.
 *
 * This function releases the reference acquired when the transition
 * was added to the #GtdWidget.
 *
 * Since: 1.10
 */
void
gtd_widget_remove_transition (GtdWidget   *self,
                              const gchar *name)
{
  GtdWidgetPrivate *priv;
  TransitionClosure *closure;
  gboolean was_playing;
  GQuark t_quark;
  gchar *t_name;

  g_return_if_fail (GTD_IS_WIDGET (self));
  g_return_if_fail (name != NULL);

  priv = gtd_widget_get_instance_private (self);

  if (priv->animation.transitions == NULL)
    return;

  closure = g_hash_table_lookup (priv->animation.transitions, name);
  if (closure == NULL)
    return;

  was_playing =
    gtd_timeline_is_playing (GTD_TIMELINE (closure->transition));
  t_quark = g_quark_from_string (closure->name);
  t_name = g_strdup (closure->name);

  g_hash_table_remove (priv->animation.transitions, name);

  /* we want to maintain the invariant that ::transition-stopped is
   * emitted after the transition has been removed, to allow replacing
   * or chaining; removing the transition from the hash table will
   * stop it, but transition_closure_free() will disconnect the signal
   * handler we install in add_transition_internal(), to avoid loops
   * or segfaults.
   *
   * since we know already that a transition will stop once it's removed
   * from an widget, we can simply emit the ::transition-stopped here
   * ourselves, if the timeline was playing (if it wasn't, then the
   * signal was already emitted at least once).
   */
  if (was_playing)
    g_signal_emit (self, signals[TRANSITION_STOPPED], t_quark, t_name, FALSE);

  g_free (t_name);
}

/**
 * gtd_widget_remove_all_transitions:
 * @self: a #GtdWidget
 *
 * Removes all transitions associated to @self.
 *
 * Since: 1.10
 */
void
gtd_widget_remove_all_transitions (GtdWidget *self)
{
  GtdWidgetPrivate *priv;

  g_return_if_fail (GTD_IS_WIDGET (self));

  priv = gtd_widget_get_instance_private (self);
  if (priv->animation.transitions == NULL)
    return;

  g_hash_table_remove_all (priv->animation.transitions);
}

/**
 * gtd_widget_set_easing_duration:
 * @self: a #GtdWidget
 * @msecs: the duration of the easing, or %NULL
 *
 * Sets the duration of the tweening for animatable properties
 * of @self for the current easing state.
 *
 * Since: 1.10
 */
void
gtd_widget_set_easing_duration (GtdWidget *self,
                                guint      msecs)
{
  GtdWidgetPrivate *priv;

  g_return_if_fail (GTD_IS_WIDGET (self));

  priv = gtd_widget_get_instance_private (self);

  if (priv->animation.current_state == NULL)
    {
      g_warning ("You must call gtd_widget_save_easing_state() prior "
                 "to calling gtd_widget_set_easing_duration().");
      return;
    }

  if (priv->animation.current_state->easing_duration != msecs)
    priv->animation.current_state->easing_duration = msecs;
}

/**
 * gtd_widget_get_easing_duration:
 * @self: a #GtdWidget
 *
 * Retrieves the duration of the tweening for animatable
 * properties of @self for the current easing state.
 *
 * Return value: the duration of the tweening, in milliseconds
 *
 * Since: 1.10
 */
guint
gtd_widget_get_easing_duration (GtdWidget *self)
{
  GtdWidgetPrivate *priv;

  g_return_val_if_fail (GTD_IS_WIDGET (self), 0);

  priv = gtd_widget_get_instance_private (self);
  if (priv->animation.current_state != NULL)
    return priv->animation.current_state->easing_duration;

  return 0;
}

/**
 * gtd_widget_set_easing_mode:
 * @self: a #GtdWidget
 * @mode: an easing mode, excluding %GTD_CUSTOM_MODE
 *
 * Sets the easing mode for the tweening of animatable properties
 * of @self.
 *
 * Since: 1.10
 */
void
gtd_widget_set_easing_mode (GtdWidget   *self,
                            GtdEaseMode  mode)
{
  GtdWidgetPrivate *priv;

  g_return_if_fail (GTD_IS_WIDGET (self));
  g_return_if_fail (mode != GTD_CUSTOM_MODE);
  g_return_if_fail (mode < GTD_EASE_LAST);

  priv = gtd_widget_get_instance_private (self);
  if (priv->animation.current_state == NULL)
    {
      g_warning ("You must call gtd_widget_save_easing_state() prior "
                 "to calling gtd_widget_set_easing_mode().");
      return;
    }

  if (priv->animation.current_state->easing_mode != mode)
    priv->animation.current_state->easing_mode = mode;
}

/**
 * gtd_widget_get_easing_mode:
 * @self: a #GtdWidget
 *
 * Retrieves the easing mode for the tweening of animatable properties
 * of @self for the current easing state.
 *
 * Return value: an easing mode
 *
 * Since: 1.10
 */
GtdEaseMode
gtd_widget_get_easing_mode (GtdWidget *self)
{
  GtdWidgetPrivate *priv;

  g_return_val_if_fail (GTD_IS_WIDGET (self), GTD_EASE_OUT_CUBIC);

  priv = gtd_widget_get_instance_private (self);

  if (priv->animation.current_state != NULL)
    return priv->animation.current_state->easing_mode;

  return GTD_EASE_OUT_CUBIC;
}

/**
 * gtd_widget_set_easing_delay:
 * @self: a #GtdWidget
 * @msecs: the delay before the start of the tweening, in milliseconds
 *
 * Sets the delay that should be applied before tweening animatable
 * properties.
 *
 * Since: 1.10
 */
void
gtd_widget_set_easing_delay (GtdWidget *self,
                             guint      msecs)
{
  GtdWidgetPrivate *priv;

  g_return_if_fail (GTD_IS_WIDGET (self));

  priv = gtd_widget_get_instance_private (self);

  if (priv->animation.current_state == NULL)
    {
      g_warning ("You must call gtd_widget_save_easing_state() prior "
                 "to calling gtd_widget_set_easing_delay().");
      return;
    }

  if (priv->animation.current_state->easing_delay != msecs)
    priv->animation.current_state->easing_delay = msecs;
}

/**
 * gtd_widget_get_easing_delay:
 * @self: a #GtdWidget
 *
 * Retrieves the delay that should be applied when tweening animatable
 * properties.
 *
 * Return value: a delay, in milliseconds
 *
 * Since: 1.10
 */
guint
gtd_widget_get_easing_delay (GtdWidget *self)
{
  GtdWidgetPrivate *priv;

  g_return_val_if_fail (GTD_IS_WIDGET (self), 0);

  priv = gtd_widget_get_instance_private (self);

  if (priv->animation.current_state != NULL)
    return priv->animation.current_state->easing_delay;

  return 0;
}

/**
 * gtd_widget_get_transition:
 * @self: a #GtdWidget
 * @name: the name of the transition
 *
 * Retrieves the #GtdTransition of a #GtdWidget by using the
 * transition @name.
 *
 * Transitions created for animatable properties use the name of the
 * property itself, for instance the code below:
 *
 * |[<!-- language="C" -->
 *   gtd_widget_set_easing_duration (widget, 1000);
 *   gtd_widget_set_rotation_angle (widget, GTD_Y_AXIS, 360.0);
 *
 *   transition = gtd_widget_get_transition (widget, "rotation-angle-y");
 *   g_signal_connect (transition, "stopped",
 *                     G_CALLBACK (on_transition_stopped),
 *                     widget);
 * ]|
 *
 * will call the `on_transition_stopped` callback when the transition
 * is finished.
 *
 * If you just want to get notifications of the completion of a transition,
 * you should use the #GtdWidget::transition-stopped signal, using the
 * transition name as the signal detail.
 *
 * Return value: (transfer none): a #GtdTransition, or %NULL is none
 *   was found to match the passed name; the returned instance is owned
 *   by Gtd and it should not be freed
 */
GtdTransition *
gtd_widget_get_transition (GtdWidget   *self,
                           const gchar *name)
{
  TransitionClosure *closure;
  GtdWidgetPrivate *priv;

  g_return_val_if_fail (GTD_IS_WIDGET (self), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  priv = gtd_widget_get_instance_private (self);
  if (priv->animation.transitions == NULL)
    return NULL;

  closure = g_hash_table_lookup (priv->animation.transitions, name);
  if (closure == NULL)
    return NULL;

  return closure->transition;
}

/**
 * gtd_widget_has_transitions: (skip)
 */
gboolean
gtd_widget_has_transitions (GtdWidget *self)
{
  GtdWidgetPrivate *priv;

  g_return_val_if_fail (GTD_IS_WIDGET (self), FALSE);

  priv = gtd_widget_get_instance_private (self);
  if (priv->animation.transitions == NULL)
    return FALSE;

  return g_hash_table_size (priv->animation.transitions) > 0;
}

/**
 * gtd_widget_save_easing_state:
 * @self: a #GtdWidget
 *
 * Saves the current easing state for animatable properties, and creates
 * a new state with the default values for easing mode and duration.
 *
 * New transitions created after calling this function will inherit the
 * duration, easing mode, and delay of the new easing state; this also
 * applies to transitions modified in flight.
 */
void
gtd_widget_save_easing_state (GtdWidget *self)
{
  GtdWidgetPrivate *priv;
  AnimationState new_state;

  g_return_if_fail (GTD_IS_WIDGET (self));

  priv = gtd_widget_get_instance_private (self);

  if (priv->animation.states == NULL)
    priv->animation.states = g_array_new (FALSE, FALSE, sizeof (AnimationState));

  new_state.easing_mode = GTD_EASE_OUT_CUBIC;
  new_state.easing_duration = 250;
  new_state.easing_delay = 0;

  g_array_append_val (priv->animation.states, new_state);

  priv->animation.current_state = &g_array_index (priv->animation.states,
                                    AnimationState,
                                    priv->animation.states->len - 1);
}

/**
 * gtd_widget_restore_easing_state:
 * @self: a #GtdWidget
 *
 * Restores the easing state as it was prior to a call to
 * gtd_widget_save_easing_state().
 *
 * Since: 1.10
 */
void
gtd_widget_restore_easing_state (GtdWidget *self)
{
  GtdWidgetPrivate *priv;

  g_return_if_fail (GTD_IS_WIDGET (self));

  priv = gtd_widget_get_instance_private (self);

  if (priv->animation.states == NULL)
    {
      g_critical ("The function gtd_widget_restore_easing_state() has "
                  "been called without a previous call to "
                  "gtd_widget_save_easing_state().");
      return;
    }

  g_array_remove_index (priv->animation.states, priv->animation.states->len - 1);

  if (priv->animation.states->len > 0)
    priv->animation.current_state = &g_array_index (priv->animation.states, AnimationState, priv->animation.states->len - 1);
  else
    {
      g_array_unref (priv->animation.states);
      priv->animation.states = NULL;
      priv->animation.current_state = NULL;
    }
}
