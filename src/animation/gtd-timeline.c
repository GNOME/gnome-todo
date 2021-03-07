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
#include "gtd-timeline-private.h"

typedef struct
{
  GtdTimelineDirection direction;

  GdkFrameClock *custom_frame_clock;
  GdkFrameClock *frame_clock;

  GtdWidget *widget;
  gulong widget_destroy_handler_id;
  gulong widget_map_handler_id;
  gulong widget_unmap_handler_id;

  guint delay_id;

  /* The total length in milliseconds of this timeline */
  guint duration;
  guint delay;

  /* The current amount of elapsed time */
  gint64 elapsed_time;

  /* The elapsed time since the last frame was fired */
  gint64 msecs_delta;

  /* Time we last advanced the elapsed time and showed a frame */
  gint64 last_frame_time;

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
  guint waiting_first_tick : 1;
  guint auto_reverse       : 1;
} GtdTimelinePrivate;

typedef struct
{
  gchar *name;
  GQuark quark;

  union {
    guint msecs;
    gdouble progress;
  } data;

  guint is_relative : 1;
} TimelineMarker;

enum
{
  PROP_0,

  PROP_AUTO_REVERSE,
  PROP_DELAY,
  PROP_DURATION,
  PROP_DIRECTION,
  PROP_REPEAT_COUNT,
  PROP_PROGRESS_MODE,
  PROP_FRAME_CLOCK,
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

static void update_frame_clock (GtdTimeline *self);
static void maybe_add_timeline (GtdTimeline *self);
static void maybe_remove_timeline (GtdTimeline *self);

G_DEFINE_TYPE_WITH_PRIVATE (GtdTimeline, gtd_timeline, G_TYPE_OBJECT)

static void
on_widget_destroyed (GtdWidget    *widget,
                    GtdTimeline *self)
{
  GtdTimelinePrivate *priv = gtd_timeline_get_instance_private (self);

  priv->widget = NULL;
}

static void
emit_frame_signal (GtdTimeline *self)
{
  GtdTimelinePrivate *priv = gtd_timeline_get_instance_private (self);

  GTD_TRACE_MSG ("Emitting ::new-frame signal on timeline[%p]", self);

  g_signal_emit (self, timeline_signals[NEW_FRAME], 0, priv->elapsed_time);
}

static gboolean
is_complete (GtdTimeline *self)
{
  GtdTimelinePrivate *priv = gtd_timeline_get_instance_private (self);

  return (priv->direction == GTD_TIMELINE_FORWARD
          ? priv->elapsed_time >= priv->duration
          : priv->elapsed_time <= 0);
}

static void
set_is_playing (GtdTimeline *self,
                gboolean         is_playing)
{
  GtdTimelinePrivate *priv = gtd_timeline_get_instance_private (self);

  is_playing = !!is_playing;

  if (is_playing == priv->is_playing)
    return;

  priv->is_playing = is_playing;

  if (priv->is_playing)
    {
      priv->waiting_first_tick = TRUE;
      priv->current_repeat = 0;

      maybe_add_timeline (self);
    }
  else
    {
      maybe_remove_timeline (self);
    }
}

static gboolean
gtd_timeline_do_frame (GtdTimeline *self)
{
  GtdTimelinePrivate *priv = gtd_timeline_get_instance_private (self);

  g_object_ref (self);

  GTD_TRACE_MSG ("Timeline [%p] activated (elapsed time: %ld, "
                 "duration: %ld, msecs_delta: %ld)",
                 self,
                 (long) priv->elapsed_time,
                 (long) priv->duration,
                 (long) priv->msecs_delta);

  /* Advance time */
  if (priv->direction == GTD_TIMELINE_FORWARD)
    priv->elapsed_time += priv->msecs_delta;
  else
    priv->elapsed_time -= priv->msecs_delta;

  /* If we have not reached the end of the timeline: */
  if (!is_complete (self))
    {
      /* Emit the signal */
      emit_frame_signal (self);
      g_object_unref (self);

      return priv->is_playing;
    }
  else
    {
      /* Handle loop or stop */
      GtdTimelineDirection saved_direction = priv->direction;
      gint elapsed_time_delta = priv->msecs_delta;
      guint overflow_msecs = priv->elapsed_time;
      gint end_msecs;

      /* Update the current elapsed time in case the signal handlers
       * want to take a peek. If we clamp elapsed time, then we need
       * to correpondingly reduce elapsed_time_delta to reflect the correct
       * range of times */
      if (priv->direction == GTD_TIMELINE_FORWARD)
        {
          elapsed_time_delta -= (priv->elapsed_time - priv->duration);
          priv->elapsed_time = priv->duration;
        }
      else if (priv->direction == GTD_TIMELINE_BACKWARD)
        {
          elapsed_time_delta -= - priv->elapsed_time;
          priv->elapsed_time = 0;
        }

      end_msecs = priv->elapsed_time;

      /* Emit the signal */
      emit_frame_signal (self);

      /* Did the signal handler modify the elapsed time? */
      if (priv->elapsed_time != end_msecs)
        {
          g_object_unref (self);
          return TRUE;
        }

      /* Note: If the new-frame signal handler paused the timeline
       * on the last frame we will still go ahead and send the
       * completed signal */
      GTD_TRACE_MSG ("Timeline [%p] completed (cur: %ld, tot: %ld)",
                    self,
                    (long) priv->elapsed_time,
                    (long) priv->msecs_delta);

      if (priv->is_playing &&
          (priv->repeat_count == 0 ||
           priv->repeat_count == priv->current_repeat))
        {
          /* We stop the timeline now, so that the completed signal handler
           * may choose to re-start the timeline
           *
           * XXX Perhaps we should do this earlier, and regardless of
           * priv->repeat_count. Are we limiting the things that could be
           * done in the above new-frame signal handler?
           */
          set_is_playing (self, FALSE);

          g_signal_emit (self, timeline_signals[COMPLETED], 0);
          g_signal_emit (self, timeline_signals[STOPPED], 0, TRUE);
        }
      else
        g_signal_emit (self, timeline_signals[COMPLETED], 0);

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

      /* Again check to see if the user has manually played with
       * the elapsed time, before we finally stop or loop the timeline */

      if (priv->elapsed_time != end_msecs &&
          !(/* Except allow changing time from 0 -> duration (or vice-versa)
               since these are considered equivalent */
            (priv->elapsed_time == 0 && end_msecs == (gint) priv->duration) ||
            (priv->elapsed_time == priv->duration && end_msecs == 0)
          ))
        {
          g_object_unref (self);
          return TRUE;
        }

      if (priv->repeat_count != 0)
        {
          /* We try and interpolate smoothly around a loop */
          if (saved_direction == GTD_TIMELINE_FORWARD)
            priv->elapsed_time = overflow_msecs - priv->duration;
          else
            priv->elapsed_time = priv->duration + overflow_msecs;

          /* Or if the direction changed, we try and bounce */
          if (priv->direction != saved_direction)
            priv->elapsed_time = priv->duration - priv->elapsed_time;

          g_object_unref (self);
          return TRUE;
        }
      else
        {
          gtd_timeline_rewind (self);

          g_object_unref (self);
          return FALSE;
        }
    }
}

static void
tick_timeline (GtdTimeline *self,
               gint64       tick_time)
{
  GtdTimelinePrivate *priv;

  priv = gtd_timeline_get_instance_private (self);

  GTD_TRACE_MSG ("Timeline [%p] ticked (elapsed_time: %ld, msecs_delta: %ld, "
                 "last_frame_time: %ld, tick_time: %ld)",
                 self,
                 (long) priv->elapsed_time,
                 (long) priv->msecs_delta,
                 (long) priv->last_frame_time,
                 (long) tick_time);

  /* Check the is_playing variable before performing the timeline tick.
   * This is necessary, as if a timeline is stopped in response to a
   * frame clock generated signal of a different timeline, this code can
   * still be reached.
   */
  if (!priv->is_playing)
    return;

  if (priv->waiting_first_tick)
    {
      priv->last_frame_time = tick_time;
      priv->msecs_delta = 0;
      priv->waiting_first_tick = FALSE;
      gtd_timeline_do_frame (self);
    }
  else
    {
      gint64 msecs;

      msecs = tick_time - priv->last_frame_time;

      /* if the clock rolled back between ticks we need to
       * account for it; the best course of action, since the
       * clock roll back can happen by any arbitrary amount
       * of milliseconds, is to drop a frame here
       */
      if (msecs < 0)
        {
          priv->last_frame_time = tick_time;
          return;
        }

      if (msecs != 0)
        {
          /* Avoid accumulating error */
          priv->last_frame_time += msecs;
          priv->msecs_delta = msecs;
          gtd_timeline_do_frame (self);
        }
    }
}

static void
on_frame_clock_after_paint_cb (GdkFrameClock *frame_clock,
                               GtdTimeline   *self)
{
  GtdTimelinePrivate *priv = gtd_timeline_get_instance_private (self);

  tick_timeline (self, gdk_frame_clock_get_frame_time (frame_clock) / 1000);

  if (priv->widget)
    gtk_widget_queue_allocate (GTK_WIDGET (priv->widget));
  else
    gdk_frame_clock_request_phase (priv->frame_clock, GDK_FRAME_CLOCK_PHASE_LAYOUT);
}

static void
maybe_add_timeline (GtdTimeline *self)
{
  GtdTimelinePrivate *priv = gtd_timeline_get_instance_private (self);

  if (!priv->frame_clock)
    return;

  g_signal_connect (priv->frame_clock, "after-paint", G_CALLBACK (on_frame_clock_after_paint_cb), self);

  if (priv->widget)
    gtk_widget_queue_allocate (GTK_WIDGET (priv->widget));
  else
    gdk_frame_clock_request_phase (priv->frame_clock, GDK_FRAME_CLOCK_PHASE_LAYOUT);
}

static void
maybe_remove_timeline (GtdTimeline *self)
{
  GtdTimelinePrivate *priv = gtd_timeline_get_instance_private (self);

  if (!priv->frame_clock)
    return;

  g_signal_handlers_disconnect_by_func (priv->frame_clock, on_frame_clock_after_paint_cb, self);
}

static void
set_frame_clock_internal (GtdTimeline   *self,
                          GdkFrameClock *frame_clock)
{
  GtdTimelinePrivate *priv = gtd_timeline_get_instance_private (self);

  if (priv->frame_clock == frame_clock)
    return;

  if (priv->frame_clock && priv->is_playing)
    maybe_remove_timeline (self);

  g_set_object (&priv->frame_clock, frame_clock);

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_FRAME_CLOCK]);

  if (priv->is_playing)
    maybe_add_timeline (self);
}

static void
update_frame_clock (GtdTimeline *self)
{
  GtdTimelinePrivate *priv = gtd_timeline_get_instance_private (self);
  GdkFrameClock *frame_clock = NULL;

  if (priv->widget)
    frame_clock = gtk_widget_get_frame_clock (GTK_WIDGET (priv->widget));

  set_frame_clock_internal (self, frame_clock);
}

static void
on_widget_map_changed_cb (GtdWidget   *widget,
                          GtdTimeline *self)
{
  update_frame_clock (self);
}

/**
 * gtd_timeline_set_widget:
 * @timeline: a #GtdTimeline
 * @widget: (nullable): a #GtdWidget
 *
 * Set the widget the timeline is associated with.
 */
void
gtd_timeline_set_widget (GtdTimeline *self,
                         GtdWidget   *widget)
{
  GtdTimelinePrivate *priv = gtd_timeline_get_instance_private (self);

  g_return_if_fail (!widget || (widget && !priv->custom_frame_clock));

  if (priv->widget)
    {
      g_clear_signal_handler (&priv->widget_destroy_handler_id, priv->widget);
      g_clear_signal_handler (&priv->widget_map_handler_id, priv->widget);
      g_clear_signal_handler (&priv->widget_unmap_handler_id, priv->widget);
      priv->widget = NULL;

      if (priv->is_playing)
        maybe_remove_timeline (self);

      priv->frame_clock = NULL;
    }

  priv->widget = widget;

  if (priv->widget)
    {
      priv->widget_destroy_handler_id =
        g_signal_connect (priv->widget, "destroy",
                          G_CALLBACK (on_widget_destroyed),
                          self);
      priv->widget_map_handler_id =
        g_signal_connect (priv->widget, "map",
                          G_CALLBACK (on_widget_map_changed_cb),
                          self);
      priv->widget_unmap_handler_id =
        g_signal_connect (priv->widget, "unmap",
                          G_CALLBACK (on_widget_map_changed_cb),
                          self);
    }

  update_frame_clock (self);
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

    case PROP_FRAME_CLOCK:
      gtd_timeline_set_frame_clock (self, g_value_get_object (value));
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
      g_value_set_uint (value, priv->delay);
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

    case PROP_FRAME_CLOCK:
      g_value_set_object (value, priv->frame_clock);
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
gtd_timeline_finalize (GObject *object)
{
  GtdTimeline *self = GTD_TIMELINE (object);
  GtdTimelinePrivate *priv = gtd_timeline_get_instance_private (self);

  if (priv->is_playing)
    maybe_remove_timeline (self);

  g_clear_object (&priv->frame_clock);

  G_OBJECT_CLASS (gtd_timeline_parent_class)->finalize (object);
}

static void
gtd_timeline_dispose (GObject *object)
{
  GtdTimeline *self = GTD_TIMELINE (object);
  GtdTimelinePrivate *priv = gtd_timeline_get_instance_private (self);

  gtd_timeline_cancel_delay (self);

  if (priv->widget)
    {
      g_clear_signal_handler (&priv->widget_destroy_handler_id, priv->widget);
      g_clear_signal_handler (&priv->widget_map_handler_id, priv->widget);
      g_clear_signal_handler (&priv->widget_unmap_handler_id, priv->widget);
      priv->widget = NULL;
    }

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
   *
   * Since: 0.4
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
   * Duration of the timeline in milliseconds, depending on the
   * GtdTimeline:fps value.
   *
   * Since: 0.6
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
   *
   * Since: 0.6
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
   *
   * Since: 1.6
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
   *
   * Since: 1.10
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
   *
   * Since: 1.10
   */
  obj_props[PROP_PROGRESS_MODE] =
    g_param_spec_enum ("progress-mode",
                       "Progress Mode",
                       "How the timeline should compute the progress",
                       GTD_TYPE_EASE_MODE,
                       GTD_EASE_LINEAR,
                       G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GtdTimeline:frame-clock:
   *
   * The frame clock driving the timeline.
   */
  obj_props[PROP_FRAME_CLOCK] =
    g_param_spec_object ("frame-clock",
                         "Frame clock",
                         "Frame clock driving the timeline",
                         GDK_TYPE_FRAME_CLOCK,
                         G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  object_class->dispose = gtd_timeline_dispose;
  object_class->finalize = gtd_timeline_finalize;
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
   *
   * Since: 1.12
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
}

static gboolean
delay_timeout_func (gpointer data)
{
  GtdTimeline *self = data;
  GtdTimelinePrivate *priv = gtd_timeline_get_instance_private (self);

  priv->delay_id = 0;
  priv->msecs_delta = 0;
  set_is_playing (self, TRUE);

  g_signal_emit (self, timeline_signals[STARTED], 0);

  return FALSE;
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

  if (priv->delay_id || priv->is_playing)
    return;

  if (priv->duration == 0)
    return;

  g_warn_if_fail ((priv->widget &&
                   gtk_widget_get_mapped (GTK_WIDGET (priv->widget))) ||
                  priv->frame_clock);

  if (priv->delay)
    {
      priv->delay_id = g_timeout_add (priv->delay, delay_timeout_func, self);
    }
  else
    {
      priv->msecs_delta = 0;
      set_is_playing (self, TRUE);

      g_signal_emit (self, timeline_signals[STARTED], 0);
    }
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

  gtd_timeline_cancel_delay (self);

  if (!priv->is_playing)
    return;

  priv->msecs_delta = 0;
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
    gtd_timeline_advance (self, priv->duration);
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

  g_return_if_fail (GTD_IS_TIMELINE (self));

  priv = gtd_timeline_get_instance_private (self);

  if (priv->direction == GTD_TIMELINE_FORWARD)
    {
      priv->elapsed_time += msecs;

      if (priv->elapsed_time > priv->duration)
        priv->elapsed_time = 1;
    }
  else if (priv->direction == GTD_TIMELINE_BACKWARD)
    {
      priv->elapsed_time -= msecs;

      if (priv->elapsed_time < 1)
        priv->elapsed_time = priv->duration - 1;
    }

  priv->msecs_delta = 0;
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

  g_return_if_fail (GTD_IS_TIMELINE (self));

  priv = gtd_timeline_get_instance_private (self);

  priv->elapsed_time = MIN (msecs, priv->duration);
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
  return priv->elapsed_time;
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
 * gtd_timeline_new:
 * @duration_ms: Duration of the timeline in milliseconds
 *
 * Creates a new #GtdTimeline with a duration of @duration_ms milli seconds.
 *
 * Return value: the newly created #GtdTimeline instance. Use
 *   g_object_unref() when done using it
 *
 * Since: 0.6
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
 * gtd_timeline_new_for_frame_clock:
 * @frame_clock: The #GdkFrameClock the timeline is driven by
 * @duration_ms: Duration of the timeline in milliseconds
 *
 * Creates a new #GtdTimeline with a duration of @duration_ms milli seconds.
 *
 * Return value: the newly created #GtdTimeline instance. Use
 *   g_object_unref() when done using it
 */
GtdTimeline *
gtd_timeline_new_for_frame_clock (GdkFrameClock *frame_clock,
                                  guint          duration_ms)
{
  return g_object_new (GTD_TYPE_TIMELINE,
                       "duration", duration_ms,
                       "frame-clock", frame_clock,
                       NULL);
}

/**
 * gtd_timeline_get_delay:
 * @timeline: a #GtdTimeline
 *
 * Retrieves the delay set using gtd_timeline_set_delay().
 *
 * Return value: the delay in milliseconds.
 *
 * Since: 0.4
 */
guint
gtd_timeline_get_delay (GtdTimeline *self)
{
  GtdTimelinePrivate *priv;

  g_return_val_if_fail (GTD_IS_TIMELINE (self), 0);

  priv = gtd_timeline_get_instance_private (self);
  return priv->delay;
}

/**
 * gtd_timeline_set_delay:
 * @timeline: a #GtdTimeline
 * @msecs: delay in milliseconds
 *
 * Sets the delay, in milliseconds, before @timeline should start.
 *
 * Since: 0.4
 */
void
gtd_timeline_set_delay (GtdTimeline *self,
                        guint        msecs)
{
  GtdTimelinePrivate *priv;

  g_return_if_fail (GTD_IS_TIMELINE (self));

  priv = gtd_timeline_get_instance_private (self);

  if (priv->delay != msecs)
    {
      priv->delay = msecs;
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
 *
 * Since: 0.6
 */
guint
gtd_timeline_get_duration (GtdTimeline *self)
{
  GtdTimelinePrivate *priv;

  g_return_val_if_fail (GTD_IS_TIMELINE (self), 0);

  priv = gtd_timeline_get_instance_private (self);

  return priv->duration;
}

/**
 * gtd_timeline_set_duration:
 * @timeline: a #GtdTimeline
 * @msecs: duration of the timeline in milliseconds
 *
 * Sets the duration of the timeline, in milliseconds. The speed
 * of the timeline depends on the GtdTimeline:fps setting.
 *
 * Since: 0.6
 */
void
gtd_timeline_set_duration (GtdTimeline *self,
                           guint        msecs)
{
  GtdTimelinePrivate *priv;

  g_return_if_fail (GTD_IS_TIMELINE (self));
  g_return_if_fail (msecs > 0);

  priv = gtd_timeline_get_instance_private (self);

  if (priv->duration != msecs)
    {
      priv->duration = msecs;

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
 *
 * Since: 0.6
 */
gdouble
gtd_timeline_get_progress (GtdTimeline *self)
{
  GtdTimelinePrivate *priv;

  g_return_val_if_fail (GTD_IS_TIMELINE (self), 0.0);

  priv = gtd_timeline_get_instance_private (self);

  /* short-circuit linear progress */
  if (priv->progress_func == NULL)
    return (gdouble) priv->elapsed_time / (gdouble) priv->duration;
  else
    return priv->progress_func (self,
                                (gdouble) priv->elapsed_time,
                                (gdouble) priv->duration,
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
 *
 * Since: 0.6
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
 *
 * Since: 0.6
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

      if (priv->elapsed_time == 0)
        priv->elapsed_time = priv->duration;

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
 *
 * Since: 0.6
 */
guint
gtd_timeline_get_delta (GtdTimeline *self)
{
  GtdTimelinePrivate *priv;

  g_return_val_if_fail (GTD_IS_TIMELINE (self), 0);

  if (!gtd_timeline_is_playing (self))
    return 0;

  priv = gtd_timeline_get_instance_private (self);
  return priv->msecs_delta;
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
 *
 * Since: 1.6
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
 *
 * Since: 1.6
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
 *
 * Since: 1.10
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
 *
 * Since: 1.10
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
 *
 * Since: 1.10
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

static gdouble
gtd_timeline_progress_func (GtdTimeline *self,
                            gdouble      elapsed,
                            gdouble      duration,
                            gpointer     user_data)
{
  GtdTimelinePrivate *priv = gtd_timeline_get_instance_private (self);

  return gtd_easing_for_mode (priv->progress_mode, elapsed, duration);
}

/**
 * gtd_timeline_set_progress_mode:
 * @timeline: a #GtdTimeline
 * @mode: the progress mode, as a #GtdEaseMode
 *
 * Sets the progress function using a value from the #GtdEaseMode
 * enumeration. The @mode cannot be %GTD_CUSTOM_MODE or bigger than
 * %GTD_ANIMATION_LAST.
 *
 * Since: 1.10
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

  /* short-circuit linear progress */
  if (priv->progress_mode != GTD_EASE_LINEAR)
    priv->progress_func = gtd_timeline_progress_func;
  else
    priv->progress_func = NULL;

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
 *
 * Since: 1.10
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
 *
 * Since: 1.10
 */
gint64
gtd_timeline_get_duration_hint (GtdTimeline *self)
{
  GtdTimelinePrivate *priv;

  g_return_val_if_fail (GTD_IS_TIMELINE (self), 0);

  priv = gtd_timeline_get_instance_private (self);

  if (priv->repeat_count == 0)
    return priv->duration;
  else if (priv->repeat_count < 0)
    return G_MAXINT64;
  else
    return priv->repeat_count * priv->duration;
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
 *
 * Since: 1.10
 */
gint
gtd_timeline_get_current_repeat (GtdTimeline *self)
{
  GtdTimelinePrivate *priv;

  g_return_val_if_fail (GTD_IS_TIMELINE (self), 0);

  priv = gtd_timeline_get_instance_private (self);
  return priv->current_repeat;
}

void
gtd_timeline_cancel_delay (GtdTimeline *self)
{
  GtdTimelinePrivate *priv = gtd_timeline_get_instance_private (self);

  g_clear_handle_id (&priv->delay_id, g_source_remove);
}


/**
 * gtd_timeline_get_frame_clock: (skip)
 */
GdkFrameClock *
gtd_timeline_get_frame_clock (GtdTimeline *self)
{
  GtdTimelinePrivate *priv;

  g_return_val_if_fail (GTD_IS_TIMELINE (self), NULL);

  priv = gtd_timeline_get_instance_private (self);
  return priv->frame_clock;
}

void
gtd_timeline_set_frame_clock (GtdTimeline   *self,
                              GdkFrameClock *frame_clock)
{
  GtdTimelinePrivate *priv;

  g_return_if_fail (GTD_IS_TIMELINE (self));

  priv = gtd_timeline_get_instance_private (self);

  g_assert (!frame_clock || (frame_clock && !priv->widget));
  g_return_if_fail (!frame_clock || (frame_clock && !priv->widget));

  priv->custom_frame_clock = frame_clock;
  if (!priv->widget)
    set_frame_clock_internal (self, frame_clock);
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
