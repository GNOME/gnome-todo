/* gtd-keyframe-transition.h
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

#include "gtd-property-transition.h"

G_BEGIN_DECLS

#define GTD_TYPE_KEYFRAME_TRANSITION (gtd_keyframe_transition_get_type())
G_DECLARE_DERIVABLE_TYPE (GtdKeyframeTransition, gtd_keyframe_transition, GTD, KEYFRAME_TRANSITION, GtdPropertyTransition)

/**
 * GtdKeyframeTransitionClass:
 *
 * The `GtdKeyframeTransitionClass` structure contains only
 * private data.
 *
 * Since: 1.12
 */
struct _GtdKeyframeTransitionClass
{
  /*< private >*/
  GtdPropertyTransitionClass parent_class;

  gpointer _padding[8];
};


GtdTransition*       gtd_keyframe_transition_new                 (const gchar *property_name);


void                 gtd_keyframe_transition_set_key_frames      (GtdKeyframeTransition *transition,
                                                                  guint                  n_key_frames,
                                                                  const gdouble         *key_frames);

void                 gtd_keyframe_transition_set_values          (GtdKeyframeTransition *transition,
                                                                  guint                  n_values,
                                                                  const GValue          *values);

void                 gtd_keyframe_transition_set_modes           (GtdKeyframeTransition *transition,
                                                                  guint                  n_modes,
                                                                  const GtdEaseMode     *modes);

void                 gtd_keyframe_transition_set                 (GtdKeyframeTransition *transition,
                                                                  GType                  gtype,
                                                                  guint                  n_key_frames,
                                                                  ...);

void                 gtd_keyframe_transition_set_key_frame       (GtdKeyframeTransition *transition,
                                                                  guint                  index_,
                                                                  double                 key,
                                                                  GtdEaseMode            mode,
                                                                  const GValue          *value);

void                 gtd_keyframe_transition_get_key_frame       (GtdKeyframeTransition *transition,
                                                                  guint                  index_,
                                                                  double                *key,
                                                                  GtdEaseMode           *mode,
                                                                  GValue                *value);

guint                gtd_keyframe_transition_get_n_key_frames    (GtdKeyframeTransition  *transition);

void                 gtd_keyframe_transition_clear               (GtdKeyframeTransition  *transition);

G_END_DECLS
