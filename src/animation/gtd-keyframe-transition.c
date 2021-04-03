/* gtd-keyframe-transition.c
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
 * SECTION:gtd-keyframe-transition
 * @Title: GtdKeyframeTransition
 * @Short_Description: Keyframe property transition
 *
 * #GtdKeyframeTransition allows animating a property by defining
 * "key frames": values at a normalized position on the transition
 * duration.
 *
 * The #GtdKeyframeTransition interpolates the value of the property
 * to which it's bound across these key values.
 *
 * Setting up a #GtdKeyframeTransition means providing the times,
 * values, and easing modes between these key frames, for instance:
 *
 * |[
 *   GtdTransition *keyframe;
 *
 *   keyframe = gtd_keyframe_transition_new ("opacity");
 *   gtd_transition_set_from (keyframe, G_TYPE_UINT, 255);
 *   gtd_transition_set_to (keyframe, G_TYPE_UINT, 0);
 *   gtd_keyframe_transition_set (GTD_KEYFRAME_TRANSITION (keyframe),
 *                                G_TYPE_UINT,
 *                                1, /&ast; number of key frames &ast;/
 *                                0.5, 128, GTD_EASE_IN_OUT_CUBIC);
 * ]|
 *
 * The example above sets up a keyframe transition for the #GtdActor:opacity
 * property of a #GtdActor; the transition starts and sets the value of the
 * property to fully transparent; between the start of the transition and its mid
 * point, it will animate the property to half opacity, using an easy in/easy out
 * progress. Once the transition reaches the mid point, it will linearly fade the
 * actor out until it reaches the end of the transition.
 *
 * The #GtdKeyframeTransition will add an implicit key frame between the last
 * and the 1.0 value, to interpolate to the final value of the transition's
 * interval.
 */

#include "gtd-keyframe-transition.h"

#include "gtd-debug.h"
#include "gtd-easing.h"
#include "gtd-interval.h"
#include "gtd-timeline.h"

#include <math.h>
#include <gobject/gvaluecollector.h>

typedef struct _KeyFrame
{
  double key;

  double start;
  double end;

  GtdEaseMode mode;

  GtdInterval *interval;
} KeyFrame;

typedef struct
{
  GArray *frames;

  gint current_frame;
} GtdKeyframeTransitionPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GtdKeyframeTransition, gtd_keyframe_transition, GTD_TYPE_PROPERTY_TRANSITION)

static void
key_frame_free (gpointer data)
{
  if (data != NULL)
    {
      KeyFrame *key = data;

      g_object_unref (key->interval);
    }
}

static int
sort_by_key (gconstpointer a,
             gconstpointer b)
{
  const KeyFrame *k_a = a;
  const KeyFrame *k_b = b;

  if (fabs (k_a->key - k_b->key) < 0.0001)
    return 0;

  if (k_a->key > k_b->key)
    return 1;

  return -1;
}

static inline void
gtd_keyframe_transition_sort_frames (GtdKeyframeTransition *self)
{
  GtdKeyframeTransitionPrivate *priv = gtd_keyframe_transition_get_instance_private (self);

  if (priv->frames != NULL)
    g_array_sort (priv->frames, sort_by_key);
}

static inline void
gtd_keyframe_transition_init_frames (GtdKeyframeTransition *self,
                                     gssize                 n_key_frames)
{
  GtdKeyframeTransitionPrivate *priv = gtd_keyframe_transition_get_instance_private (self);
  guint i;

  priv->frames = g_array_sized_new (FALSE, FALSE,
                                    sizeof (KeyFrame),
                                    n_key_frames);
  g_array_set_clear_func (priv->frames, key_frame_free);

  /* we add an implicit key frame that goes to 1.0, so that the
   * user doesn't have to do that an can simply add key frames
   * in between 0.0 and 1.0
   */
  for (i = 0; i < n_key_frames + 1; i++)
    {
      KeyFrame frame;

      if (i == n_key_frames)
        frame.key = 1.0;
      else
        frame.key = 0.0;

      frame.mode = GTD_EASE_LINEAR;
      frame.interval = NULL;

      g_array_insert_val (priv->frames, i, frame);
    }
}

static inline void
gtd_keyframe_transition_update_frames (GtdKeyframeTransition *self)
{
  GtdKeyframeTransitionPrivate *priv = gtd_keyframe_transition_get_instance_private (self);
  guint i;

  if (priv->frames == NULL)
    return;

  for (i = 0; i < priv->frames->len; i++)
    {
      KeyFrame *cur_frame = &g_array_index (priv->frames, KeyFrame, i);
      KeyFrame *prev_frame;

      if (i > 0)
        prev_frame = &g_array_index (priv->frames, KeyFrame, i - 1);
      else
        prev_frame = NULL;

      if (prev_frame != NULL)
        {
          cur_frame->start = prev_frame->key;

          if (prev_frame->interval != NULL)
            {
              const GValue *value;

              value = gtd_interval_peek_final_value (prev_frame->interval);

              if (cur_frame->interval != NULL)
                gtd_interval_set_initial_value (cur_frame->interval, value);
              else
                {
                  cur_frame->interval =
                    gtd_interval_new_with_values (G_VALUE_TYPE (value), value, NULL);
                }
            }
        }
      else
        cur_frame->start = 0.0;

      cur_frame->end = cur_frame->key;
    }
}

static void
gtd_keyframe_transition_compute_value (GtdTransition *transition,
                                       GtdAnimatable *animatable,
                                       GtdInterval   *interval,
                                       gdouble        progress)
{
  GtdKeyframeTransition *self = GTD_KEYFRAME_TRANSITION (transition);
  GtdKeyframeTransitionPrivate *priv = gtd_keyframe_transition_get_instance_private (self);
  GtdTimeline *timeline = GTD_TIMELINE (self);
  GtdTransitionClass *parent_class;
  GtdTimelineDirection direction;
  GtdInterval *real_interval;
  gdouble real_progress;
  double t, d, p;
  KeyFrame *cur_frame = NULL;

  real_interval = interval;
  real_progress = progress;

  /* if we don't have any keyframe, we behave like our parent class */
  if (priv->frames == NULL)
    goto out;

  direction = gtd_timeline_get_direction (timeline);

  /* we need a normalized linear value */
  t = gtd_timeline_get_elapsed_time (timeline);
  d = gtd_timeline_get_duration (timeline);
  p = t / d;

  if (priv->current_frame < 0)
    {
      if (direction == GTD_TIMELINE_FORWARD)
        priv->current_frame = 0;
      else
        priv->current_frame = priv->frames->len - 1;
    }

  cur_frame = &g_array_index (priv->frames, KeyFrame, priv->current_frame);

  /* skip to the next key frame, depending on the direction of the timeline */
  if (direction == GTD_TIMELINE_FORWARD)
    {
      if (p > cur_frame->end)
        {
          priv->current_frame = MIN (priv->current_frame + 1,
                                     (gint) priv->frames->len - 1);
          cur_frame = &g_array_index (priv->frames, KeyFrame, priv->current_frame);
       }
    }
  else
    {
      if (p < cur_frame->start)
        {
          priv->current_frame = MAX (priv->current_frame - 1, 0);

          cur_frame = &g_array_index (priv->frames, KeyFrame, priv->current_frame);
        }
    }

  /* if we are at the boundaries of the transition, use the from and to
   * value from the transition
   */
  if (priv->current_frame == 0)
    {
      const GValue *value;

      value = gtd_interval_peek_initial_value (interval);
      gtd_interval_set_initial_value (cur_frame->interval, value);
    }
  else if (priv->current_frame == (gint) priv->frames->len - 1)
    {
      const GValue *value;

      cur_frame->mode = gtd_timeline_get_progress_mode (timeline);

      value = gtd_interval_peek_final_value (interval);
      gtd_interval_set_final_value (cur_frame->interval, value);
    }

  /* update the interval to be used to interpolate the property */
  real_interval = cur_frame->interval;

  /* normalize the progress and apply the easing mode */
  real_progress = gtd_easing_for_mode (cur_frame->mode,
                                       (p - cur_frame->start),
                                       (cur_frame->end - cur_frame->start));

#ifdef GTD_ENABLE_DEBUG
  if (GTD_HAS_DEBUG (ANIMATION))
    {
      char *from, *to;
      const GValue *value;

      value = gtd_interval_peek_initial_value (cur_frame->interval);
      from = g_strdup_value_contents (value);

      value = gtd_interval_peek_final_value (cur_frame->interval);
      to = g_strdup_value_contents (value);

      GTD_TRACE_MSG ("[animation] cur_frame [%d] => { %g, %s, %s %s %s } - "
                    "progress: %g, sub-progress: %g\n",
                    priv->current_frame,
                    cur_frame->key,
                    gtd_get_easing_name_for_mode (cur_frame->mode),
                    from,
                    direction == GTD_TIMELINE_FORWARD ? "->" : "<-",
                    to,
                    p,
                     real_progress);

      g_free (from);
      g_free (to);
    }
#endif /* GTD_ENABLE_DEBUG */

out:
  parent_class =
    GTD_TRANSITION_CLASS (gtd_keyframe_transition_parent_class);
  parent_class->compute_value (transition, animatable, real_interval, real_progress);
}

static void
gtd_keyframe_transition_started (GtdTimeline *timeline)
{
  GtdKeyframeTransition *self;
  GtdKeyframeTransitionPrivate *priv;

  self = GTD_KEYFRAME_TRANSITION (timeline);
  priv = gtd_keyframe_transition_get_instance_private (self);

  priv->current_frame = -1;

  gtd_keyframe_transition_sort_frames (self);
  gtd_keyframe_transition_update_frames (self);
}

static void
gtd_keyframe_transition_completed (GtdTimeline *timeline)
{
  GtdKeyframeTransition *self = GTD_KEYFRAME_TRANSITION (timeline);
  GtdKeyframeTransitionPrivate *priv = gtd_keyframe_transition_get_instance_private (self);

  priv->current_frame = -1;
}

static void
gtd_keyframe_transition_finalize (GObject *gobject)
{
  GtdKeyframeTransition *self = GTD_KEYFRAME_TRANSITION (gobject);
  GtdKeyframeTransitionPrivate *priv = gtd_keyframe_transition_get_instance_private (self);

  if (priv->frames != NULL)
    g_array_unref (priv->frames);

  G_OBJECT_CLASS (gtd_keyframe_transition_parent_class)->finalize (gobject);
}

static void
gtd_keyframe_transition_class_init (GtdKeyframeTransitionClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GtdTimelineClass *timeline_class = GTD_TIMELINE_CLASS (klass);
  GtdTransitionClass *transition_class = GTD_TRANSITION_CLASS (klass);

  gobject_class->finalize = gtd_keyframe_transition_finalize;

  timeline_class->started = gtd_keyframe_transition_started;
  timeline_class->completed = gtd_keyframe_transition_completed;

  transition_class->compute_value = gtd_keyframe_transition_compute_value;
}

static void
gtd_keyframe_transition_init (GtdKeyframeTransition *self)
{
}

/**
 * gtd_keyframe_transition_new:
 * @property_name: the property to animate
 *
 * Creates a new #GtdKeyframeTransition for @property_name.
 *
 * Return value: (transfer full): the newly allocated
 *   #GtdKeyframeTransition instance. Use g_object_unref() when
 *   done to free its resources.
 *
 * Since: 1.12
 */
GtdTransition *
gtd_keyframe_transition_new (const gchar *property_name)
{
  return g_object_new (GTD_TYPE_KEYFRAME_TRANSITION,
                       "property-name", property_name,
                       NULL);
}

/**
 * gtd_keyframe_transition_set_key_frames:
 * @transition: a #GtdKeyframeTransition
 * @n_key_frames: the number of values
 * @key_frames: (array length=n_key_frames): an array of keys between 0.0
 *   and 1.0, one for each key frame
 *
 * Sets the keys for each key frame inside @transition.
 *
 * If @transition does not hold any key frame, @n_key_frames key frames
 * will be created; if @transition already has key frames, @key_frames must
 * have at least as many elements as the number of key frames.
 *
 * Since: 1.12
 */
void
gtd_keyframe_transition_set_key_frames (GtdKeyframeTransition *self,
                                        guint                  n_key_frames,
                                        const gdouble         *key_frames)
{
  GtdKeyframeTransitionPrivate *priv;
  guint i;

  g_return_if_fail (GTD_IS_KEYFRAME_TRANSITION (self));
  g_return_if_fail (n_key_frames > 0);
  g_return_if_fail (key_frames != NULL);

  priv = gtd_keyframe_transition_get_instance_private (self);

  if (priv->frames == NULL)
    gtd_keyframe_transition_init_frames (self, n_key_frames);
  else
    g_return_if_fail (n_key_frames == priv->frames->len - 1);

  for (i = 0; i < n_key_frames; i++)
    {
      KeyFrame *frame = &g_array_index (priv->frames, KeyFrame, i);

      frame->key = key_frames[i];
    }
}

/**
 * gtd_keyframe_transition_set_values:
 * @transition: a #GtdKeyframeTransition
 * @n_values: the number of values
 * @values: (array length=n_values): an array of values, one for each
 *   key frame
 *
 * Sets the values for each key frame inside @transition.
 *
 * If @transition does not hold any key frame, @n_values key frames will
 * be created; if @transition already has key frames, @values must have
 * at least as many elements as the number of key frames.
 *
 * Since: 1.12
 */
void
gtd_keyframe_transition_set_values (GtdKeyframeTransition *self,
                                    guint                  n_values,
                                    const GValue          *values)
{
  GtdKeyframeTransitionPrivate *priv;
  guint i;

  g_return_if_fail (GTD_IS_KEYFRAME_TRANSITION (self));
  g_return_if_fail (n_values > 0);
  g_return_if_fail (values != NULL);

  priv = gtd_keyframe_transition_get_instance_private (self);

  if (priv->frames == NULL)
    gtd_keyframe_transition_init_frames (self, n_values);
  else
    g_return_if_fail (n_values == priv->frames->len - 1);

  for (i = 0; i < n_values; i++)
    {
      KeyFrame *frame = &g_array_index (priv->frames, KeyFrame, i);

      if (frame->interval)
        gtd_interval_set_final_value (frame->interval, &values[i]);
      else
        frame->interval =
          gtd_interval_new_with_values (G_VALUE_TYPE (&values[i]), NULL,
                                            &values[i]);
    }
}

/**
 * gtd_keyframe_transition_set_modes:
 * @transition: a #GtdKeyframeTransition
 * @n_modes: the number of easing modes
 * @modes: (array length=n_modes): an array of easing modes, one for
 *   each key frame
 *
 * Sets the easing modes for each key frame inside @transition.
 *
 * If @transition does not hold any key frame, @n_modes key frames will
 * be created; if @transition already has key frames, @modes must have
 * at least as many elements as the number of key frames.
 */
void
gtd_keyframe_transition_set_modes (GtdKeyframeTransition *self,
                                   guint                  n_modes,
                                   const GtdEaseMode     *modes)
{
  GtdKeyframeTransitionPrivate *priv;
  guint i;

  g_return_if_fail (GTD_IS_KEYFRAME_TRANSITION (self));
  g_return_if_fail (n_modes > 0);
  g_return_if_fail (modes != NULL);

  priv = gtd_keyframe_transition_get_instance_private (self);

  if (priv->frames == NULL)
    gtd_keyframe_transition_init_frames (self, n_modes);
  else
    g_return_if_fail (n_modes == priv->frames->len - 1);

  for (i = 0; i < n_modes; i++)
    {
      KeyFrame *frame = &g_array_index (priv->frames, KeyFrame, i);

      frame->mode = modes[i];
    }
}

/**
 * gtd_keyframe_transition_set: (skip)
 * @transition: a #GtdKeyframeTransition
 * @gtype: the type of the values to use for the key frames
 * @n_key_frames: the number of key frames between the initial
 *   and final values
 * @...: a list of tuples, containing the key frame index, the value
 *   at the key frame, and the animation mode
 *
 * Sets the key frames of the @transition.
 *
 * This variadic arguments function is a convenience for C developers;
 * language bindings should use gtd_keyframe_transition_set_key_frames(),
 * gtd_keyframe_transition_set_modes(), and
 * gtd_keyframe_transition_set_values() instead.
 */
void
gtd_keyframe_transition_set (GtdKeyframeTransition *self,
                             GType                  gtype,
                             guint                  n_key_frames,
                             ...)
{
  GtdKeyframeTransitionPrivate *priv;
  va_list args;
  guint i;

  g_return_if_fail (GTD_IS_KEYFRAME_TRANSITION (self));
  g_return_if_fail (gtype != G_TYPE_INVALID);
  g_return_if_fail (n_key_frames > 0);

  priv = gtd_keyframe_transition_get_instance_private (self);

  if (priv->frames == NULL)
    gtd_keyframe_transition_init_frames (self, n_key_frames);
  else
    g_return_if_fail (n_key_frames == priv->frames->len - 1);

  va_start (args, n_key_frames);

  for (i = 0; i < n_key_frames; i++)
    {
      KeyFrame *frame = &g_array_index (priv->frames, KeyFrame, i);
      GValue value = G_VALUE_INIT;
      char *error = NULL;

      frame->key = va_arg (args, gdouble);

      G_VALUE_COLLECT_INIT (&value, gtype, args, 0, &error);
      if (error != NULL)
        {
          g_warning ("%s: %s", G_STRLOC, error);
          g_free (error);
          break;
        }

      frame->mode = va_arg (args, GtdEaseMode);

      g_clear_object (&frame->interval);
      frame->interval = gtd_interval_new_with_values (gtype, NULL, &value);

      g_value_unset (&value);
    }

  va_end (args);
}

/**
 * gtd_keyframe_transition_clear:
 * @transition: a #GtdKeyframeTransition
 *
 * Removes all key frames from @transition.
 */
void
gtd_keyframe_transition_clear (GtdKeyframeTransition *self)
{
  GtdKeyframeTransitionPrivate *priv;

  g_return_if_fail (GTD_IS_KEYFRAME_TRANSITION (self));

  priv = gtd_keyframe_transition_get_instance_private (self);
  if (priv->frames != NULL)
    {
      g_array_unref (priv->frames);
      priv->frames = NULL;
    }
}

/**
 * gtd_keyframe_transition_get_n_key_frames:
 * @transition: a #GtdKeyframeTransition
 *
 * Retrieves the number of key frames inside @transition.
 *
 * Return value: the number of key frames
 */
guint
gtd_keyframe_transition_get_n_key_frames (GtdKeyframeTransition *self)
{
  GtdKeyframeTransitionPrivate *priv;

  g_return_val_if_fail (GTD_IS_KEYFRAME_TRANSITION (self), 0);

  priv = gtd_keyframe_transition_get_instance_private (self);
  if (priv->frames == NULL)
    return 0;

  return priv->frames->len - 1;
}

/**
 * gtd_keyframe_transition_set_key_frame:
 * @transition: a #GtdKeyframeTransition
 * @index_: the index of the key frame
 * @key: the key of the key frame
 * @mode: the easing mode of the key frame
 * @value: a #GValue containing the value of the key frame
 *
 * Sets the details of the key frame at @index_ inside @transition.
 *
 * The @transition must already have a key frame at @index_, and @index_
 * must be smaller than the number of key frames inside @transition.
 */
void
gtd_keyframe_transition_set_key_frame (GtdKeyframeTransition *self,
                                           guint                      index_,
                                           double                     key,
                                           GtdEaseMode       mode,
                                           const GValue              *value)
{
  GtdKeyframeTransitionPrivate *priv;
  KeyFrame *frame;

  g_return_if_fail (GTD_IS_KEYFRAME_TRANSITION (self));

  priv = gtd_keyframe_transition_get_instance_private (self);

  g_return_if_fail (priv->frames != NULL);
  g_return_if_fail (index_ < priv->frames->len - 1);

  frame = &g_array_index (priv->frames, KeyFrame, index_);
  frame->key = key;
  frame->mode = mode;
  gtd_interval_set_final_value (frame->interval, value);
}

/**
 * gtd_keyframe_transition_get_key_frame:
 * @transition: a #GtdKeyframeTransition
 * @index_: the index of the key frame
 * @key: (out) (allow-none): return location for the key, or %NULL
 * @mode: (out) (allow-none): return location for the easing mode, or %NULL
 * @value: (out caller-allocates): a #GValue initialized with the type of
 *   the values
 *
 * Retrieves the details of the key frame at @index_ inside @transition.
 *
 * The @transition must already have key frames set, and @index_ must be
 * smaller than the number of key frames.
 */
void
gtd_keyframe_transition_get_key_frame (GtdKeyframeTransition *self,
                                       guint                  index_,
                                       double                *key,
                                       GtdEaseMode           *mode,
                                       GValue                *value)
{
  GtdKeyframeTransitionPrivate *priv;
  const KeyFrame *frame;

  g_return_if_fail (GTD_IS_KEYFRAME_TRANSITION (self));

  priv = gtd_keyframe_transition_get_instance_private (self);
  g_return_if_fail (priv->frames != NULL);
  g_return_if_fail (index_ < priv->frames->len - 1);

  frame = &g_array_index (priv->frames, KeyFrame, index_);

  if (key != NULL)
    *key = frame->key;

  if (mode != NULL)
    *mode = frame->mode;

  if (value != NULL)
    gtd_interval_get_final_value (frame->interval, value);
}
