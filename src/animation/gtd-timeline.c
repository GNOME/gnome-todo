/*
 * Gtd.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Authored By Matthew Allum  <mallum@openedhand.com>
 *
 * Copyright (C) 2006 OpenedHand
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * SECTION:gtd-timeline
 * @short_description: A class for time-based events
 *
 * #GtdTimeline is a base class for managing time-based event that cause
 * GTK to redraw, such as animations.
 *
 * Each #GtdTimeline instance has a duration: once a timeline has been
 * started, using gtd_timeline_start(), it will emit a signal that can
 * be used to update the state of the widgets.
 *
 * It is important to note that #GtdTimeline is not a generic API for
 * calling closures after an interval; each Timeline is tied into the master
 * clock used to drive the frame cycle. If you need to schedule a closure
 * after an interval, see gtd_threads_add_timeout() instead.
 *
 * Users of #GtdTimeline should connect to the #GtdTimeline::new-frame
 * signal, which is emitted each time a timeline is advanced during the maste
 * clock iteration. The #GtdTimeline::new-frame signal provides the time
 * elapsed since the beginning of the timeline, in milliseconds. A normalized
 * progress value can be obtained by calling gtd_timeline_get_progress().
 * By using gtd_timeline_get_delta() it is possible to obtain the wallclock
 * time elapsed since the last emission of the #GtdTimeline::new-frame
 * signal.
 *
 * Initial state can be set up by using the #GtdTimeline::started signal,
 * while final state can be set up by using the #GtdTimeline::stopped
 * signal. The #GtdTimeline guarantees the emission of at least a single
 * #GtdTimeline::new-frame signal, as well as the emission of the
 * #GtdTimeline::completed signal every time the #GtdTimeline reaches
 * its #GtdTimeline:duration.
 *
 * It is possible to connect to specific points in the timeline progress by
 * adding markers using gtd_timeline_add_marker_at_time() and connecting
 * to the #GtdTimeline::marker-reached signal.
 *
 * Timelines can be made to loop once they reach the end of their duration, by
 * using gtd_timeline_set_repeat_count(); a looping timeline will still
 * emit the #GtdTimeline::completed signal once it reaches the end of its
 * duration at each repeat. If you want to be notified of the end of the last
 * repeat, use the #GtdTimeline::stopped signal.
 *
 * Timelines have a #GtdTimeline:direction: the default direction is
 * %GTD_TIMELINE_FORWARD, and goes from 0 to the duration; it is possible
 * to change the direction to %GTD_TIMELINE_BACKWARD, and have the timeline
 * go from the duration to 0. The direction can be automatically reversed
 * when reaching completion by using the #GtdTimeline:auto-reverse property.
 *
 * Timelines are used in the Gtd animation framework by classes like
 * #GtdTransition.
 */

#define G_LOG_DOMAIN "GtdTimeline"

#include "gtd-timeline.h"

#include "gtd-debug.h"
#include "gnome-todo.h"

typedef struct
{
  GtdTimelineDirection direction;

  GtdWidget *widget;
  guint frame_tick_id;

  gint64 duration_us;
  gint64 delay_us;

  /* The current amount of elapsed time */
  gint64 elapsed_time_us;

  /* The elapsed time since the last frame was fired */
  gint64 delta_us;

  /* Time we last advanced the elapsed time and showed a frame */
  gint64 last_frame_time_us;

  gint64 start_us;

  /* How many times the timeline should repeat */
  gint repeat_count;

  /* The number of times the timeline has repeated */
  gint current_repeat;

  GtdTimelineProgressFunc progress_func;
  gpointer progress_data;
  GDestroyNotify progress_notify;
  GtdEaseMode progress_mode;

  guint is_playing         : 1;

  /* If we've just started playing and haven't yet gotten
   * a tick from the master clock
   */
  guint auto_reverse       : 1;
} GtdTimelinePrivate;

enum
{
  PROP_0,

  PROP_AUTO_REVERSE,
  PROP_DELAY,
  PROP_DURATION,
  PROP_DIRECTION,
  PROP_REPEAT_COUNT,
  PROP_PROGRESS_MODE,
  PROP_WIDGET,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST] = { NULL, };

enum
{
  NEW_FRAME,
  STARTED,
  PAUSED,
  COMPLETED,
  STOPPED,

  LAST_SIGNAL
};

static guint timeline_signals[LAST_SIGNAL] = { 0, };

static void set_is_playing (GtdTimeline *self,
                            gboolean     is_playing);

G_DEFINE_TYPE_WITH_PRIVATE (GtdTimeline, gtd_timeline, G_TYPE_OBJECT)

static inline gint64
us_to_ms (gint64 ms)
{
  return ms / 1000;
}

static inline gint64
ms_to_us (gint64 us)
{
  return us * 1000;
}

static inline gboolean
is_waiting_for_delay (GtdTimeline *self,
                      gint64       frame_time_us)
{
  GtdTimelinePrivate *priv = gtd_timeline_get_instance_private (self);
  return priv->start_us + priv->delay_us > frame_time_us;
}

static void
emit_frame_signal (GtdTimeline *self)
{
  GtdTimelinePrivate *priv = gtd_timeline_get_instance_private (self);
  gint64 elapsed_time_ms = us_to_ms (priv->elapsed_time_us);

  GTD_TRACE_MSG ("Emitting ::new-frame signal on timeline[%p]", self);

  g_signal_emit (self, timeline_signals[NEW_FRAME], 0, elapsed_time_ms);
}

static gboolean
is_complete (GtdTimeline *self)
{
  GtdTimelinePrivate *priv = gtd_timeline_get_instance_private (self);

  return (priv->direction == GTD_TIMELINE_FORWARD
          ? priv->elapsed_time_us >= priv->duration_us
          : priv->elapsed_time_us <= 0);
}

static gboolean
maybe_loop_timeline (GtdTimeline *self)
{
  GtdTimelinePrivate *priv = gtd_timeline_get_instance_private (self);
  GtdTimelineDirection saved_direction = priv->direction;
  gint64 overflow_us = priv->elapsed_time_us;
  gint64 end_us;

  /* Update the current elapsed time in case the signal handlers
   * want to take a peek. If we clamp elapsed time, then we need
   * to correpondingly reduce elapsed_time_delta to reflect the correct
   * range of times */
  if (priv->direction == GTD_TIMELINE_FORWARD)
    priv->elapsed_time_us = priv->duration_us;
  else if (priv->direction == GTD_TIMELINE_BACKWARD)
    priv->elapsed_time_us = 0;

  end_us = priv->elapsed_time_us;

  /* Emit the signal */
  emit_frame_signal (self);

  /* Did the signal handler modify the elapsed time? */
  if (priv->elapsed_time_us != end_us)
    return TRUE;

  /* Note: If the new-frame signal handler paused the timeline
   * on the last frame we will still go ahead and send the
   * completed signal */
  GTD_TRACE_MSG ("Timeline [%p] completed (cur: %ldµs, tot: %ldµs)",
                self,
                priv->elapsed_time_us,
                priv->delta_us);

  if (priv->is_playing &&
      (priv->repeat_count == 0 ||
       priv->repeat_count == priv->current_repeat))
    {
      /* We stop the timeline now, so that the completed signal handler
       * may choose to re-start the timeline
       */
      set_is_playing (self, FALSE);

      g_signal_emit (self, timeline_signals[COMPLETED], 0);
      g_signal_emit (self, timeline_signals[STOPPED], 0, TRUE);
    }
  else
    {
      g_signal_emit (self, timeline_signals[COMPLETED], 0);
    }

  priv->current_repeat += 1;

  if (priv->auto_reverse)
    {
      /* :auto-reverse changes the direction of the timeline */
      if (priv->direction == GTD_TIMELINE_FORWARD)
        priv->direction = GTD_TIMELINE_BACKWARD;
      else
        priv->direction = GTD_TIMELINE_FORWARD;

      g_object_notify_by_pspec (G_OBJECT (self),
                                obj_props[PROP_DIRECTION]);
    }

  /*
   * Again check to see if the user has manually played with
   * the elapsed time, before we finally stop or loop the timeline,
   * except changing time from 0 -> duration (or vice-versa)
   * since these are considered equivalent
   */
  if (priv->elapsed_time_us != end_us &&
      !((priv->elapsed_time_us == 0 && end_us == priv->duration_us) ||
        (priv->elapsed_time_us == priv->duration_us && end_us == 0)))
    {
      return TRUE;
    }

  if (priv->repeat_count == 0)
    {
      gtd_timeline_rewind (self);
      return FALSE;
    }

  /* Try and interpolate smoothly around a loop */
  if (saved_direction == GTD_TIMELINE_FORWARD)
    priv->elapsed_time_us = overflow_us - priv->duration_us;
  else
    priv->elapsed_time_us = priv->duration_us + overflow_us;

  /* Or if the direction changed, we try and bounce */
  if (priv->direction != saved_direction)
    priv->elapsed_time_us = priv->duration_us - priv->elapsed_time_us;

  return TRUE;
}

static gboolean
tick_timeline (GtdTimeline *self,
               gint64       tick_time_us)
{
  GtdTimelinePrivate *priv;
  gboolean should_continue;
  gboolean complete;
  gint64 elapsed_us;

  priv = gtd_timeline_get_instance_private (self);

  GTD_TRACE_MSG ("Timeline [%p] ticked (elapsed_time: %ldµs, delta_us: %ldµs, "
                 "last_frame_time: %ldµs, tick_time: %ldµs)",
                 self,
                 priv->elapsed_time_us,
                 priv->delta_us,
                 priv->last_frame_time_us,
                 tick_time_us);

  /* Check the is_playing variable before performing the timeline tick.
   * This is necessary, as if a timeline is stopped in response to a
   * frame clock generated signal of a different timeline, this code can
   * still be reached.
   */
  if (!priv->is_playing)
    return FALSE;

  elapsed_us = tick_time_us - priv->last_frame_time_us;
  priv->last_frame_time_us = tick_time_us;

  if (is_waiting_for_delay (self, tick_time_us))
    {
      GTD_TRACE_MSG ("- waiting for delay");
      return G_SOURCE_CONTINUE;
    }

  /* if the clock rolled back between ticks we need to
   * account for it; the best course of action, since the
   * clock roll back can happen by any arbitrary amount
   * of milliseconds, is to drop a frame here
   */
  if (elapsed_us <= 0)
    return TRUE;

  priv->delta_us = elapsed_us;

  GTD_TRACE_MSG ("Timeline [%p] activated (elapsed time: %ldµs, "
                 "duration: %ldµs, delta_us: %ldµs)",
                 self,
                 priv->elapsed_time_us,
                 priv->duration_us,
                 priv->delta_us);

  g_object_ref (self);

  /* Advance time */
  if (priv->direction == GTD_TIMELINE_FORWARD)
    priv->elapsed_time_us += priv->delta_us;
  else
    priv->elapsed_time_us -= priv->delta_us;

  complete = is_complete (self);
  should_continue = !complete ? priv->is_playing : maybe_loop_timeline (self);

  /* If we have not reached the end of the timeline */
  if (!complete)
    emit_frame_signal (self);

  g_object_unref (self);

  return should_continue;
}

static gboolean
frame_tick_cb (GtkWidget     *widget,
               GdkFrameClock *frame_clock,
               gpointer       user_data)
{
  GtdTimeline *self = GTD_TIMELINE (user_data);
  GtdTimelinePrivate *priv = gtd_timeline_get_instance_private (self);
  gint64 frame_time_us;

  frame_time_us = gdk_frame_clock_get_frame_time (frame_clock);

  if (tick_timeline (self, frame_time_us))
    return G_SOURCE_CONTINUE;

  priv->frame_tick_id = 0;
  return G_SOURCE_REMOVE;
}

static void
add_tick_callback (GtdTimeline *self)
{
  GtdTimelinePrivate *priv = gtd_timeline_get_instance_private (self);

  g_assert (!(priv->frame_tick_id > 0 && !priv->widget));

  if (priv->frame_tick_id == 0)
    {
      priv->frame_tick_id = gtk_widget_add_tick_callback (GTK_WIDGET (priv->widget),
                                                          frame_tick_cb,
                                                          self,
                                                          NULL);
    }
}

static void
remove_tick_callback (GtdTimeline *self)
{
  GtdTimelinePrivate *priv = gtd_timeline_get_instance_private (self);

  g_assert (!(priv->frame_tick_id > 0 && !priv->widget));

  if (priv->frame_tick_id > 0)
    {
      gtk_widget_remove_tick_callback (GTK_WIDGET (priv->widget), priv->frame_tick_id);
      priv->frame_tick_id = 0;
    }
}

static void
set_is_playing (GtdTimeline *self,
                gboolean     is_playing)
{
  GtdTimelinePrivate *priv = gtd_timeline_get_instance_private (self);

  is_playing = !!is_playing;

  if (is_playing == priv->is_playing)
    return;

  priv->is_playing = is_playing;

  if (priv->is_playing)
    {
      priv->start_us = g_get_monotonic_time ();
      priv->last_frame_time_us = priv->start_us;
      priv->current_repeat = 0;

      add_tick_callback (self);
    }
  else
    {
      remove_tick_callback (self);
    }
}

static gdouble
timeline_progress_func (GtdTimeline *self,
                        gdouble      elapsed,
                        gdouble      duration,
                        gpointer     user_data)
{
  GtdTimelinePrivate *priv = gtd_timeline_get_instance_private (self);

  return gtd_easing_for_mode (priv->progress_mode, elapsed, duration);
}


/*
 * GObject overrides
 */

static void
gtd_timeline_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  GtdTimeline *self = GTD_TIMELINE (object);

  switch (prop_id)
    {
    case PROP_DELAY:
      gtd_timeline_set_delay (self, g_value_get_uint (value));
      break;

    case PROP_DURATION:
      gtd_timeline_set_duration (self, g_value_get_uint (value));
      break;

    case PROP_DIRECTION:
      gtd_timeline_set_direction (self, g_value_get_enum (value));
      break;

    case PROP_AUTO_REVERSE:
      gtd_timeline_set_auto_reverse (self, g_value_get_boolean (value));
      break;

    case PROP_REPEAT_COUNT:
      gtd_timeline_set_repeat_count (self, g_value_get_int (value));
      break;

    case PROP_PROGRESS_MODE:
      gtd_timeline_set_progress_mode (self, g_value_get_enum (value));
      break;

    case PROP_WIDGET:
      gtd_timeline_set_widget (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gtd_timeline_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  GtdTimeline *self = GTD_TIMELINE (object);
  GtdTimelinePrivate *priv = gtd_timeline_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_DELAY:
      g_value_set_uint (value, us_to_ms (priv->delay_us));
      break;

    case PROP_DURATION:
      g_value_set_uint (value, gtd_timeline_get_duration (self));
      break;

    case PROP_DIRECTION:
      g_value_set_enum (value, priv->direction);
      break;

    case PROP_AUTO_REVERSE:
      g_value_set_boolean (value, priv->auto_reverse);
      break;

    case PROP_REPEAT_COUNT:
      g_value_set_int (value, priv->repeat_count);
      break;

    case PROP_PROGRESS_MODE:
      g_value_set_enum (value, priv->progress_mode);
      break;

    case PROP_WIDGET:
      g_value_set_object (value, priv->widget);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gtd_timeline_dispose (GObject *object)
{
  GtdTimeline *self = GTD_TIMELINE (object);
  GtdTimelinePrivate *priv = gtd_timeline_get_instance_private (self);

  if (priv->progress_notify != NULL)
    {
      priv->progress_notify (priv->progress_data);
      priv->progress_func = NULL;
      priv->progress_data = NULL;
      priv->progress_notify = NULL;
    }

  G_OBJECT_CLASS (gtd_timeline_parent_class)->dispose (object);
}

static void
gtd_timeline_class_init (GtdTimelineClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  /**
   * GtdTimeline::widget:
   *
   * The widget the timeline is associated with. This will determine what frame
   * clock will drive it.
   */
  obj_props[PROP_WIDGET] =
    g_param_spec_object ("widget",
                         "Widget",
                         "Associated GtdWidget",
                         GTD_TYPE_WIDGET,
                         G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  /**
   * GtdTimeline:delay:
   *
   * A delay, in milliseconds, that should be observed by the
   * timeline before actually starting.
   */
  obj_props[PROP_DELAY] =
    g_param_spec_uint ("delay",
                       "Delay",
                       "Delay before start",
                       0, G_MAXUINT,
                       0,
                       G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GtdTimeline:duration:
   *
   * Duration of the timeline in milliseconds.
   */
  obj_props[PROP_DURATION] =
    g_param_spec_uint ("duration",
                       "Duration",
                       "Duration of the timeline in milliseconds",
                       0, G_MAXUINT,
                       1000,
                       G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GtdTimeline:direction:GIT
   *
   * The direction of the timeline, either %GTD_TIMELINE_FORWARD or
   * %GTD_TIMELINE_BACKWARD.
   */
  obj_props[PROP_DIRECTION] =
    g_param_spec_enum ("direction",
                       "Direction",
                       "Direction of the timeline",
                       GTD_TYPE_TIMELINE_DIRECTION,
                       GTD_TIMELINE_FORWARD,
                       G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GtdTimeline:auto-reverse:
   *
   * If the direction of the timeline should be automatically reversed
   * when reaching the end.
   */
  obj_props[PROP_AUTO_REVERSE] =
    g_param_spec_boolean ("auto-reverse",
                          "Auto Reverse",
                          "Whether the direction should be reversed when reaching the end",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GtdTimeline:repeat-count:
   *
   * Defines how many times the timeline should repeat.
   *
   * If the repeat count is 0, the timeline does not repeat.
   *
   * If the repeat count is set to -1, the timeline will repeat until it is
   * stopped.
   */
  obj_props[PROP_REPEAT_COUNT] =
    g_param_spec_int ("repeat-count",
                      "Repeat Count",
                      "How many times the timeline should repeat",
                      -1, G_MAXINT,
                      0,
                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GtdTimeline:progress-mode:
   *
   * Controls the way a #GtdTimeline computes the normalized progress.
   */
  obj_props[PROP_PROGRESS_MODE] =
    g_param_spec_enum ("progress-mode",
                       "Progress Mode",
                       "How the timeline should compute the progress",
                       GTD_TYPE_EASE_MODE,
                       GTD_EASE_LINEAR,
                       G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  object_class->dispose = gtd_timeline_dispose;
  object_class->set_property = gtd_timeline_set_property;
  object_class->get_property = gtd_timeline_get_property;
  g_object_class_install_properties (object_class, PROP_LAST, obj_props);

  /**
   * GtdTimeline::new-frame:
   * @timeline: the timeline which received the signal
   * @msecs: the elapsed time between 0 and duration
   *
   * The ::new-frame signal is emitted for each timeline running
   * timeline before a new frame is drawn to give animations a chance
   * to update the scene.
   */
  timeline_signals[NEW_FRAME] =
    g_signal_new ("new-frame",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GtdTimelineClass, new_frame),
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  1, G_TYPE_INT);
  /**
   * GtdTimeline::completed:
   * @timeline: the #GtdTimeline which received the signal
   *
   * The #GtdTimeline::completed signal is emitted when the timeline's
   * elapsed time reaches the value of the #GtdTimeline:duration
   * property.
   *
   * This signal will be emitted even if the #GtdTimeline is set to be
   * repeating.
   *
   * If you want to get notification on whether the #GtdTimeline has
   * been stopped or has finished its run, including its eventual repeats,
   * you should use the #GtdTimeline::stopped signal instead.
   */
  timeline_signals[COMPLETED] =
    g_signal_new ("completed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GtdTimelineClass, completed),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
  /**
   * GtdTimeline::started:
   * @timeline: the #GtdTimeline which received the signal
   *
   * The ::started signal is emitted when the timeline starts its run.
   * This might be as soon as gtd_timeline_start() is invoked or
   * after the delay set in the GtdTimeline:delay property has
   * expired.
   */
  timeline_signals[STARTED] =
    g_signal_new ("started",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GtdTimelineClass, started),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
  /**
   * GtdTimeline::paused:
   * @timeline: the #GtdTimeline which received the signal
   *
   * The ::paused signal is emitted when gtd_timeline_pause() is invoked.
   */
  timeline_signals[PAUSED] =
    g_signal_new ("paused",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GtdTimelineClass, paused),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  /**
   * GtdTimeline::stopped:
   * @timeline: the #GtdTimeline that emitted the signal
   * @is_finished: %TRUE if the signal was emitted at the end of the
   *   timeline.
   *
   * The #GtdTimeline::stopped signal is emitted when the timeline
   * has been stopped, either because gtd_timeline_stop() has been
   * called, or because it has been exhausted.
   *
   * This is different from the #GtdTimeline::completed signal,
   * which gets emitted after every repeat finishes.
   *
   * If the #GtdTimeline has is marked as infinitely repeating,
   * this signal will never be emitted.
   */
  timeline_signals[STOPPED] =
    g_signal_new ("stopped",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GtdTimelineClass, stopped),
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  1,
                  G_TYPE_BOOLEAN);
}

static void
gtd_timeline_init (GtdTimeline *self)
{
  GtdTimelinePrivate *priv = gtd_timeline_get_instance_private (self);

  priv->progress_mode = GTD_EASE_LINEAR;
  priv->progress_func = timeline_progress_func;
}

/**
 * gtd_timeline_new:
 * @duration_ms: Duration of the timeline in milliseconds
 *
 * Creates a new #GtdTimeline with a duration of @duration_ms milli seconds.
 *
 * Return value: the newly created #GtdTimeline instance. Use
 *   g_object_unref() when done using it
 */
GtdTimeline *
gtd_timeline_new (guint duration_ms)
{
  return g_object_new (GTD_TYPE_TIMELINE,
                       "duration", duration_ms,
                       NULL);
}

/**
 * gtd_timeline_new_for_widget:
 * @widget: The #GtdWidget the timeline is associated with
 * @duration_ms: Duration of the timeline in milliseconds
 *
 * Creates a new #GtdTimeline with a duration of @duration milli seconds.
 *
 * Return value: the newly created #GtdTimeline instance. Use
 *   g_object_unref() when done using it
 */
GtdTimeline *
gtd_timeline_new_for_widget (GtdWidget *widget,
                             guint      duration_ms)
{
  return g_object_new (GTD_TYPE_TIMELINE,
                       "duration", duration_ms,
                       "widget", widget,
                       NULL);
}

/**
 * gtd_timeline_set_widget:
 * @timeline: a #GtdTimeline
 * @widget: a #GtdWidget
 *
 * Set the widget the timeline is associated with.
 */
void
gtd_timeline_set_widget (GtdTimeline *self,
                         GtdWidget   *widget)
{
  GtdTimelinePrivate *priv = gtd_timeline_get_instance_private (self);

  g_return_if_fail (GTD_IS_TIMELINE (self));

  if (priv->widget)
    {
      remove_tick_callback (self);
      priv->widget = NULL;
    }

  priv->widget = widget;

  if (priv->is_playing)
    add_tick_callback (self);
}

/**
 * gtd_timeline_start:
 * @timeline: A #GtdTimeline
 *
 * Starts the #GtdTimeline playing.
 **/
void
gtd_timeline_start (GtdTimeline *self)
{
  GtdTimelinePrivate *priv;

  g_return_if_fail (GTD_IS_TIMELINE (self));

  priv = gtd_timeline_get_instance_private (self);

  if (priv->is_playing)
    return;

  if (priv->duration_us == 0)
    return;

  priv->delta_us = 0;
  set_is_playing (self, TRUE);

  g_signal_emit (self, timeline_signals[STARTED], 0);
}

/**
 * gtd_timeline_pause:
 * @timeline: A #GtdTimeline
 *
 * Pauses the #GtdTimeline on current frame
 **/
void
gtd_timeline_pause (GtdTimeline *self)
{
  GtdTimelinePrivate *priv;

  g_return_if_fail (GTD_IS_TIMELINE (self));

  priv = gtd_timeline_get_instance_private (self);

  if (!priv->is_playing)
    return;

  priv->delta_us = 0;
  set_is_playing (self, FALSE);

  g_signal_emit (self, timeline_signals[PAUSED], 0);
}

/**
 * gtd_timeline_stop:
 * @timeline: A #GtdTimeline
 *
 * Stops the #GtdTimeline and moves to frame 0
 **/
void
gtd_timeline_stop (GtdTimeline *self)
{
  GtdTimelinePrivate *priv;
  gboolean was_playing;

  g_return_if_fail (GTD_IS_TIMELINE (self));

  priv = gtd_timeline_get_instance_private (self);

  /* we check the is_playing here because pause() will return immediately
   * if the timeline wasn't playing, so we don't know if it was actually
   * stopped, and yet we still don't want to emit a ::stopped signal if
   * the timeline was not playing in the first place.
   */
  was_playing = priv->is_playing;

  gtd_timeline_pause (self);
  gtd_timeline_rewind (self);

  if (was_playing)
    g_signal_emit (self, timeline_signals[STOPPED], 0, FALSE);
}

/**
 * gtd_timeline_rewind:
 * @timeline: A #GtdTimeline
 *
 * Rewinds #GtdTimeline to the first frame if its direction is
 * %GTD_TIMELINE_FORWARD and the last frame if it is
 * %GTD_TIMELINE_BACKWARD.
 */
void
gtd_timeline_rewind (GtdTimeline *self)
{
  GtdTimelinePrivate *priv;

  g_return_if_fail (GTD_IS_TIMELINE (self));

  priv = gtd_timeline_get_instance_private (self);

  if (priv->direction == GTD_TIMELINE_FORWARD)
    gtd_timeline_advance (self, 0);
  else if (priv->direction == GTD_TIMELINE_BACKWARD)
    gtd_timeline_advance (self, us_to_ms (priv->duration_us));
}

/**
 * gtd_timeline_skip:
 * @timeline: A #GtdTimeline
 * @msecs: Amount of time to skip
 *
 * Advance timeline by the requested time in milliseconds
 */
void
gtd_timeline_skip (GtdTimeline *self,
                   guint        msecs)
{
  GtdTimelinePrivate *priv;
  gint64 us;

  g_return_if_fail (GTD_IS_TIMELINE (self));

  priv = gtd_timeline_get_instance_private (self);
  us = ms_to_us (msecs);

  if (priv->direction == GTD_TIMELINE_FORWARD)
    {
      priv->elapsed_time_us += us;

      if (priv->elapsed_time_us > priv->duration_us)
        priv->elapsed_time_us = 1;
    }
  else if (priv->direction == GTD_TIMELINE_BACKWARD)
    {
      priv->elapsed_time_us -= us;

      if (priv->elapsed_time_us < 1)
        priv->elapsed_time_us = priv->duration_us - 1;
    }

  priv->delta_us = 0;
}

/**
 * gtd_timeline_advance:
 * @timeline: A #GtdTimeline
 * @msecs: Time to advance to
 *
 * Advance timeline to the requested point. The point is given as a
 * time in milliseconds since the timeline started.
 *
 * The @timeline will not emit the #GtdTimeline::new-frame
 * signal for the given time. The first ::new-frame signal after the call to
 * gtd_timeline_advance() will be emit the skipped markers.
 */
void
gtd_timeline_advance (GtdTimeline *self,
                      guint        msecs)
{
  GtdTimelinePrivate *priv;
  gint64 us;

  g_return_if_fail (GTD_IS_TIMELINE (self));

  priv = gtd_timeline_get_instance_private (self);
  us = ms_to_us (msecs);

  priv->elapsed_time_us = MIN (us, priv->duration_us);
}

/**
 * gtd_timeline_get_elapsed_time:
 * @timeline: A #GtdTimeline
 *
 * Request the current time position of the timeline.
 *
 * Return value: current elapsed time in milliseconds.
 */
guint
gtd_timeline_get_elapsed_time (GtdTimeline *self)
{
  GtdTimelinePrivate *priv;

  g_return_val_if_fail (GTD_IS_TIMELINE (self), 0);

  priv = gtd_timeline_get_instance_private (self);
  return us_to_ms (priv->elapsed_time_us);
}

/**
 * gtd_timeline_is_playing:
 * @timeline: A #GtdTimeline
 *
 * Queries state of a #GtdTimeline.
 *
 * Return value: %TRUE if timeline is currently playing
 */
gboolean
gtd_timeline_is_playing (GtdTimeline *self)
{
  GtdTimelinePrivate *priv;

  g_return_val_if_fail (GTD_IS_TIMELINE (self), FALSE);

  priv = gtd_timeline_get_instance_private (self);
  return priv->is_playing;
}

/**
 * gtd_timeline_get_delay:
 * @timeline: a #GtdTimeline
 *
 * Retrieves the delay set using gtd_timeline_set_delay().
 *
 * Return value: the delay in milliseconds.
 */
guint
gtd_timeline_get_delay (GtdTimeline *self)
{
  GtdTimelinePrivate *priv;

  g_return_val_if_fail (GTD_IS_TIMELINE (self), 0);

  priv = gtd_timeline_get_instance_private (self);
  return us_to_ms (priv->delay_us);
}

/**
 * gtd_timeline_set_delay:
 * @timeline: a #GtdTimeline
 * @msecs: delay in milliseconds
 *
 * Sets the delay, in milliseconds, before @timeline should start.
 */
void
gtd_timeline_set_delay (GtdTimeline *self,
                        guint        msecs)
{
  GtdTimelinePrivate *priv;
  gint64 us;

  g_return_if_fail (GTD_IS_TIMELINE (self));

  priv = gtd_timeline_get_instance_private (self);
  us = ms_to_us (msecs);

  if (priv->delay_us != us)
    {
      priv->delay_us = us;
      g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_DELAY]);
    }
}

/**
 * gtd_timeline_get_duration:
 * @timeline: a #GtdTimeline
 *
 * Retrieves the duration of a #GtdTimeline in milliseconds.
 * See gtd_timeline_set_duration().
 *
 * Return value: the duration of the timeline, in milliseconds.
 */
guint
gtd_timeline_get_duration (GtdTimeline *self)
{
  GtdTimelinePrivate *priv;

  g_return_val_if_fail (GTD_IS_TIMELINE (self), 0);

  priv = gtd_timeline_get_instance_private (self);

  return us_to_ms (priv->duration_us);
}

/**
 * gtd_timeline_set_duration:
 * @timeline: a #GtdTimeline
 * @msecs: duration of the timeline in milliseconds
 *
 * Sets the duration of the timeline, in milliseconds. The speed
 * of the timeline depends on the GtdTimeline:fps setting.
 */
void
gtd_timeline_set_duration (GtdTimeline *self,
                           guint        msecs)
{
  GtdTimelinePrivate *priv;
  gint64 us;

  g_return_if_fail (GTD_IS_TIMELINE (self));
  g_return_if_fail (msecs > 0);

  priv = gtd_timeline_get_instance_private (self);
  us = ms_to_us (msecs);

  if (priv->duration_us != us)
    {
      priv->duration_us = us;

      g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_DURATION]);
    }
}

/**
 * gtd_timeline_get_progress:
 * @timeline: a #GtdTimeline
 *
 * The position of the timeline in a normalized [-1, 2] interval.
 *
 * The return value of this function is determined by the progress
 * mode set using gtd_timeline_set_progress_mode(), or by the
 * progress function set using gtd_timeline_set_progress_func().
 *
 * Return value: the normalized current position in the timeline.
 */
gdouble
gtd_timeline_get_progress (GtdTimeline *self)
{
  GtdTimelinePrivate *priv;

  g_return_val_if_fail (GTD_IS_TIMELINE (self), 0.0);

  priv = gtd_timeline_get_instance_private (self);

  return priv->progress_func (self,
                              (gdouble) priv->elapsed_time_us,
                              (gdouble) priv->duration_us,
                              priv->progress_data);
}

/**
 * gtd_timeline_get_direction:
 * @timeline: a #GtdTimeline
 *
 * Retrieves the direction of the timeline set with
 * gtd_timeline_set_direction().
 *
 * Return value: the direction of the timeline
 */
GtdTimelineDirection
gtd_timeline_get_direction (GtdTimeline *self)
{
  GtdTimelinePrivate *priv;

  g_return_val_if_fail (GTD_IS_TIMELINE (self), GTD_TIMELINE_FORWARD);

  priv = gtd_timeline_get_instance_private (self);
  return priv->direction;
}

/**
 * gtd_timeline_set_direction:
 * @timeline: a #GtdTimeline
 * @direction: the direction of the timeline
 *
 * Sets the direction of @timeline, either %GTD_TIMELINE_FORWARD or
 * %GTD_TIMELINE_BACKWARD.
 */
void
gtd_timeline_set_direction (GtdTimeline          *self,
                            GtdTimelineDirection  direction)
{
  GtdTimelinePrivate *priv;

  g_return_if_fail (GTD_IS_TIMELINE (self));

  priv = gtd_timeline_get_instance_private (self);

  if (priv->direction != direction)
    {
      priv->direction = direction;

      if (priv->elapsed_time_us == 0)
        priv->elapsed_time_us = priv->duration_us;

      g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_DIRECTION]);
    }
}

/**
 * gtd_timeline_get_delta:
 * @timeline: a #GtdTimeline
 *
 * Retrieves the amount of time elapsed since the last
 * GtdTimeline::new-frame signal.
 *
 * This function is only useful inside handlers for the ::new-frame
 * signal, and its behaviour is undefined if the timeline is not
 * playing.
 *
 * Return value: the amount of time in milliseconds elapsed since the
 * last frame
 */
guint
gtd_timeline_get_delta (GtdTimeline *self)
{
  GtdTimelinePrivate *priv;

  g_return_val_if_fail (GTD_IS_TIMELINE (self), 0);

  if (!gtd_timeline_is_playing (self))
    return 0;

  priv = gtd_timeline_get_instance_private (self);
  return us_to_ms (priv->delta_us);
}

/**
 * gtd_timeline_set_auto_reverse:
 * @timeline: a #GtdTimeline
 * @reverse: %TRUE if the @timeline should reverse the direction
 *
 * Sets whether @timeline should reverse the direction after the
 * emission of the #GtdTimeline::completed signal.
 *
 * Setting the #GtdTimeline:auto-reverse property to %TRUE is the
 * equivalent of connecting a callback to the #GtdTimeline::completed
 * signal and changing the direction of the timeline from that callback;
 * for instance, this code:
 *
 * |[
 * static void
 * reverse_timeline (GtdTimeline *self)
 * {
 *   GtdTimelineDirection dir = gtd_timeline_get_direction (self);
 *
 *   if (dir == GTD_TIMELINE_FORWARD)
 *     dir = GTD_TIMELINE_BACKWARD;
 *   else
 *     dir = GTD_TIMELINE_FORWARD;
 *
 *   gtd_timeline_set_direction (self, dir);
 * }
 * ...
 *   timeline = gtd_timeline_new (1000);
 *   gtd_timeline_set_repeat_count (self, -1);
 *   g_signal_connect (self, "completed",
 *                     G_CALLBACK (reverse_timeline),
 *                     NULL);
 * ]|
 *
 * can be effectively replaced by:
 *
 * |[
 *   timeline = gtd_timeline_new (1000);
 *   gtd_timeline_set_repeat_count (self, -1);
 *   gtd_timeline_set_auto_reverse (self);
 * ]|
 */
void
gtd_timeline_set_auto_reverse (GtdTimeline *self,
                               gboolean     reverse)
{
  GtdTimelinePrivate *priv;

  g_return_if_fail (GTD_IS_TIMELINE (self));

  reverse = !!reverse;

  priv = gtd_timeline_get_instance_private (self);

  if (priv->auto_reverse != reverse)
    {
      priv->auto_reverse = reverse;
      g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_AUTO_REVERSE]);
    }
}

/**
 * gtd_timeline_get_auto_reverse:
 * @timeline: a #GtdTimeline
 *
 * Retrieves the value set by gtd_timeline_set_auto_reverse().
 *
 * Return value: %TRUE if the timeline should automatically reverse, and
 *   %FALSE otherwise
 */
gboolean
gtd_timeline_get_auto_reverse (GtdTimeline *self)
{
  GtdTimelinePrivate *priv;

  g_return_val_if_fail (GTD_IS_TIMELINE (self), FALSE);

  priv = gtd_timeline_get_instance_private (self);
  return priv->auto_reverse;
}

/**
 * gtd_timeline_set_repeat_count:
 * @timeline: a #GtdTimeline
 * @count: the number of times the timeline should repeat
 *
 * Sets the number of times the @timeline should repeat.
 *
 * If @count is 0, the timeline never repeats.
 *
 * If @count is -1, the timeline will always repeat until
 * it's stopped.
 */
void
gtd_timeline_set_repeat_count (GtdTimeline *self,
                               gint         count)
{
  GtdTimelinePrivate *priv;

  g_return_if_fail (GTD_IS_TIMELINE (self));
  g_return_if_fail (count >= -1);

  priv = gtd_timeline_get_instance_private (self);

  if (priv->repeat_count != count)
    {
      priv->repeat_count = count;
      g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_REPEAT_COUNT]);
    }
}

/**
 * gtd_timeline_get_repeat_count:
 * @timeline: a #GtdTimeline
 *
 * Retrieves the number set using gtd_timeline_set_repeat_count().
 *
 * Return value: the number of repeats
 */
gint
gtd_timeline_get_repeat_count (GtdTimeline *self)
{
  GtdTimelinePrivate *priv;

  g_return_val_if_fail (GTD_IS_TIMELINE (self), 0);

  priv = gtd_timeline_get_instance_private (self);
  return priv->repeat_count;
}

/**
 * gtd_timeline_set_progress_func:
 * @timeline: a #GtdTimeline
 * @func: (scope notified) (allow-none): a progress function, or %NULL
 * @data: (closure): data to pass to @func
 * @notify: a function to be called when the progress function is removed
 *    or the timeline is disposed
 *
 * Sets a custom progress function for @timeline. The progress function will
 * be called by gtd_timeline_get_progress() and will be used to compute
 * the progress value based on the elapsed time and the total duration of the
 * timeline.
 *
 * If @func is not %NULL, the #GtdTimeline:progress-mode property will
 * be set to %GTD_CUSTOM_MODE.
 *
 * If @func is %NULL, any previously set progress function will be unset, and
 * the #GtdTimeline:progress-mode property will be set to %GTD_EASE_LINEAR.
 */
void
gtd_timeline_set_progress_func (GtdTimeline             *self,
                                GtdTimelineProgressFunc  func,
                                gpointer                 data,
                                GDestroyNotify           notify)
{
  GtdTimelinePrivate *priv;

  g_return_if_fail (GTD_IS_TIMELINE (self));

  priv = gtd_timeline_get_instance_private (self);

  if (priv->progress_notify != NULL)
    priv->progress_notify (priv->progress_data);

  priv->progress_func = func;
  priv->progress_data = data;
  priv->progress_notify = notify;

  if (priv->progress_func != NULL)
    priv->progress_mode = GTD_CUSTOM_MODE;
  else
    priv->progress_mode = GTD_EASE_LINEAR;

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_PROGRESS_MODE]);
}

/**
 * gtd_timeline_set_progress_mode:
 * @timeline: a #GtdTimeline
 * @mode: the progress mode, as a #GtdEaseMode
 *
 * Sets the progress function using a value from the #GtdEaseMode
 * enumeration. The @mode cannot be %GTD_CUSTOM_MODE or bigger than
 * %GTD_ANIMATION_LAST.
 */
void
gtd_timeline_set_progress_mode (GtdTimeline *self,
                                GtdEaseMode  mode)
{
  GtdTimelinePrivate *priv;

  g_return_if_fail (GTD_IS_TIMELINE (self));
  g_return_if_fail (mode < GTD_EASE_LAST);
  g_return_if_fail (mode != GTD_CUSTOM_MODE);

  priv = gtd_timeline_get_instance_private (self);

  if (priv->progress_mode == mode)
    return;

  if (priv->progress_notify != NULL)
    priv->progress_notify (priv->progress_data);

  priv->progress_mode = mode;
  priv->progress_func = timeline_progress_func;
  priv->progress_data = NULL;
  priv->progress_notify = NULL;

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_PROGRESS_MODE]);
}

/**
 * gtd_timeline_get_progress_mode:
 * @timeline: a #GtdTimeline
 *
 * Retrieves the progress mode set using gtd_timeline_set_progress_mode()
 * or gtd_timeline_set_progress_func().
 *
 * Return value: a #GtdEaseMode
 */
GtdEaseMode
gtd_timeline_get_progress_mode (GtdTimeline *self)
{
  GtdTimelinePrivate *priv;

  g_return_val_if_fail (GTD_IS_TIMELINE (self), GTD_EASE_LINEAR);

  priv = gtd_timeline_get_instance_private (self);
  return priv->progress_mode;
}

/**
 * gtd_timeline_get_duration_hint:
 * @timeline: a #GtdTimeline
 *
 * Retrieves the full duration of the @timeline, taking into account the
 * current value of the #GtdTimeline:repeat-count property.
 *
 * If the #GtdTimeline:repeat-count property is set to -1, this function
 * will return %G_MAXINT64.
 *
 * The returned value is to be considered a hint, and it's only valid
 * as long as the @timeline hasn't been changed.
 *
 * Return value: the full duration of the #GtdTimeline
 */
gint64
gtd_timeline_get_duration_hint (GtdTimeline *self)
{
  GtdTimelinePrivate *priv;
  gint64 duration_ms;

  g_return_val_if_fail (GTD_IS_TIMELINE (self), 0);

  priv = gtd_timeline_get_instance_private (self);
  duration_ms = us_to_ms (priv->duration_us);

  if (priv->repeat_count == 0)
    return duration_ms;
  else if (priv->repeat_count < 0)
    return G_MAXINT64;
  else
    return priv->repeat_count * duration_ms;
}

/**
 * gtd_timeline_get_current_repeat:
 * @timeline: a #GtdTimeline
 *
 * Retrieves the current repeat for a timeline.
 *
 * Repeats start at 0.
 *
 * Return value: the current repeat
 */
gint
gtd_timeline_get_current_repeat (GtdTimeline *self)
{
  GtdTimelinePrivate *priv;

  g_return_val_if_fail (GTD_IS_TIMELINE (self), 0);

  priv = gtd_timeline_get_instance_private (self);
  return priv->current_repeat;
}

/**
 * gtd_timeline_get_widget:
 * @timeline: a #GtdTimeline
 *
 * Get the widget the timeline is associated with.
 *
 * Returns: (transfer none): the associated #GtdWidget
 */
GtdWidget *
gtd_timeline_get_widget (GtdTimeline *self)
{
  GtdTimelinePrivate *priv = gtd_timeline_get_instance_private (self);

  return priv->widget;
}
