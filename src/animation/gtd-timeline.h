/* gtd-timeline.h
 *
 * Copyright 2020 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
 *
 * Heavily inspired by Clutter, authored By Matthew Allum  <mallum@openedhand.com>
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

#pragma once

#include <glib-object.h>
#include <gtk/gtk.h>

#include "gtd-easing.h"
#include "gtd-types.h"

G_BEGIN_DECLS

#define GTD_TYPE_TIMELINE (gtd_timeline_get_type())
G_DECLARE_DERIVABLE_TYPE (GtdTimeline, gtd_timeline, GTD, TIMELINE, GObject)

/**
 * GtdTimelineProgressFunc:
 * @timeline: a #GtdTimeline
 * @elapsed: the elapsed time, in milliseconds
 * @total: the total duration of the timeline, in milliseconds,
 * @user_data: data passed to the function
 *
 * A function for defining a custom progress.
 *
 * Return value: the progress, as a floating point value between -1.0 and 2.0.
 */
typedef gdouble (* GtdTimelineProgressFunc)     (GtdTimeline *timeline,
                                                 gdouble      elapsed,
                                                 gdouble      total,
                                                 gpointer     user_data);

/**
 * GtdTimelineClass:
 * @started: class handler for the #GtdTimeline::started signal
 * @completed: class handler for the #GtdTimeline::completed signal
 * @paused: class handler for the #GtdTimeline::paused signal
 * @new_frame: class handler for the #GtdTimeline::new-frame signal
 * @stopped: class handler for the #GtdTimeline::stopped signal
 *
 * The #GtdTimelineClass structure contains only private data
 */
struct _GtdTimelineClass
{
  /*< private >*/
  GObjectClass        parent_class;

  /*< public >*/
  void               (*started)        (GtdTimeline *timeline);
  void               (*completed)      (GtdTimeline *timeline);
  void               (*paused)         (GtdTimeline *timeline);

  void               (*new_frame)      (GtdTimeline *timeline,
                                        gint             msecs);

  void               (*stopped)        (GtdTimeline *timeline,
                                        gboolean         is_finished);
};

GtdTimeline*         gtd_timeline_new_for_widget                 (GtdWidget            *widget,
                                                                  guint                 duration_ms);

GtdWidget*           gtd_timeline_get_widget                     (GtdTimeline          *timeline);

void                 gtd_timeline_set_widget                     (GtdTimeline          *timeline,
                                                                  GtdWidget             *widget);

guint                gtd_timeline_get_duration                   (GtdTimeline          *timeline);

void                 gtd_timeline_set_duration                   (GtdTimeline          *timeline,
                                                                  guint                 msecs);

GtdTimelineDirection gtd_timeline_get_direction                  (GtdTimeline          *timeline);

void                 gtd_timeline_set_direction                  (GtdTimeline          *timeline,
                                                                  GtdTimelineDirection  direction);

void                 gtd_timeline_start                          (GtdTimeline          *timeline);

void                 gtd_timeline_pause                          (GtdTimeline          *timeline);

void                 gtd_timeline_stop                           (GtdTimeline          *timeline);

void                 gtd_timeline_set_auto_reverse               (GtdTimeline          *timeline,
                                                                  gboolean              reverse);

gboolean             gtd_timeline_get_auto_reverse               (GtdTimeline          *timeline);

void                 gtd_timeline_set_repeat_count               (GtdTimeline          *timeline,
                                                                  gint                  count);

gint                 gtd_timeline_get_repeat_count               (GtdTimeline          *timeline);

void                 gtd_timeline_rewind                         (GtdTimeline          *timeline);

void                 gtd_timeline_skip                           (GtdTimeline          *timeline,
                                                                  guint                 msecs);

void                 gtd_timeline_advance                        (GtdTimeline          *timeline,
                                                                  guint                 msecs);

guint                gtd_timeline_get_elapsed_time               (GtdTimeline          *timeline);

gdouble              gtd_timeline_get_progress                   (GtdTimeline          *timeline);

gboolean             gtd_timeline_is_playing                     (GtdTimeline          *timeline);

void                 gtd_timeline_set_delay                      (GtdTimeline          *timeline,
                                                                  guint                 msecs);

guint                gtd_timeline_get_delay                      (GtdTimeline          *timeline);

guint                gtd_timeline_get_delta                      (GtdTimeline          *timeline);

void                 gtd_timeline_set_progress_func              (GtdTimeline          *timeline,
                                                                  GtdTimelineProgressFunc func,
                                                                  gpointer                data,
                                                                  GDestroyNotify          notify);

void                 gtd_timeline_set_progress_mode              (GtdTimeline          *timeline,
                                                                  GtdEaseMode           mode);

GtdEaseMode          gtd_timeline_get_progress_mode              (GtdTimeline          *timeline);

gint64               gtd_timeline_get_duration_hint              (GtdTimeline          *timeline);
gint                 gtd_timeline_get_current_repeat             (GtdTimeline          *timeline);

G_END_DECLS


G_END_DECLS
