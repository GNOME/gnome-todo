/* gtd-transition.h
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

#pragma once

#include "gtd-timeline.h"

G_BEGIN_DECLS

#define GTD_TYPE_TRANSITION (gtd_transition_get_type())
G_DECLARE_DERIVABLE_TYPE (GtdTransition, gtd_transition, GTD, TRANSITION, GtdTimeline)

/**
 * GtdTransitionClass:
 * @attached: virtual function; called when a transition is attached to
 *   a #GtdAnimatable instance
 * @detached: virtual function; called when a transition is detached from
 *   a #GtdAnimatable instance
 * @compute_value: virtual function; called each frame to compute and apply
 *   the interpolation of the interval
 *
 * The #GtdTransitionClass structure contains
 * private data.
 *
 * Since: 1.10
 */
struct _GtdTransitionClass
{
  /*< private >*/
  GtdTimelineClass parent_class;

  /*< public >*/
  void (* attached) (GtdTransition *transition,
                     GtdAnimatable *animatable);
  void (* detached) (GtdTransition *transition,
                     GtdAnimatable *animatable);

  void (* compute_value) (GtdTransition *transition,
                          GtdAnimatable *animatable,
                          GtdInterval   *interval,
                          gdouble        progress);

  /*< private >*/
  gpointer _padding[8];
};

void                    gtd_transition_set_interval                 (GtdTransition *transition,
                                                                     GtdInterval   *interval);
GtdInterval *           gtd_transition_get_interval                 (GtdTransition *transition);
void                    gtd_transition_set_from_value               (GtdTransition *transition,
                                                                     const GValue  *value);
void                    gtd_transition_set_to_value                 (GtdTransition *transition,
                                                                     const GValue  *value);
void                    gtd_transition_set_from                     (GtdTransition *transition,
                                                                     GType          value_type,
                                                                     ...);
void                    gtd_transition_set_to                       (GtdTransition *transition,
                                                                     GType          value_type,
                                                                     ...);

void                    gtd_transition_set_animatable               (GtdTransition *transition,
                                                                     GtdAnimatable *animatable);
GtdAnimatable *         gtd_transition_get_animatable               (GtdTransition *transition);
void                    gtd_transition_set_remove_on_complete       (GtdTransition *transition,
                                                                     gboolean       remove_complete);
gboolean                gtd_transition_get_remove_on_complete       (GtdTransition *transition);

G_END_DECLS
