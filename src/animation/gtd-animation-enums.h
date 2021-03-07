/* gtd-animation-enums.h
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

#include <glib.h>

G_BEGIN_DECLS


/**
 * GtdEaseMode:
 * @GTD_CUSTOM_MODE: custom progress function
 * @GTD_EASE_LINEAR: linear tweening
 * @GTD_EASE_IN_QUAD: quadratic tweening
 * @GTD_EASE_OUT_QUAD: quadratic tweening, inverse of
 *    %GTD_EASE_IN_QUAD
 * @GTD_EASE_IN_OUT_QUAD: quadratic tweening, combininig
 *    %GTD_EASE_IN_QUAD and %GTD_EASE_OUT_QUAD
 * @GTD_EASE_IN_CUBIC: cubic tweening
 * @GTD_EASE_OUT_CUBIC: cubic tweening, invers of
 *    %GTD_EASE_IN_CUBIC
 * @GTD_EASE_IN_OUT_CUBIC: cubic tweening, combining
 *    %GTD_EASE_IN_CUBIC and %GTD_EASE_OUT_CUBIC
 * @GTD_EASE_IN_QUART: quartic tweening
 * @GTD_EASE_OUT_QUART: quartic tweening, inverse of
 *    %GTD_EASE_IN_QUART
 * @GTD_EASE_IN_OUT_QUART: quartic tweening, combining
 *    %GTD_EASE_IN_QUART and %GTD_EASE_OUT_QUART
 * @GTD_EASE_IN_QUINT: quintic tweening
 * @GTD_EASE_OUT_QUINT: quintic tweening, inverse of
 *    %GTD_EASE_IN_QUINT
 * @GTD_EASE_IN_OUT_QUINT: fifth power tweening, combining
 *    %GTD_EASE_IN_QUINT and %GTD_EASE_OUT_QUINT
 * @GTD_EASE_IN_SINE: sinusoidal tweening
 * @GTD_EASE_OUT_SINE: sinusoidal tweening, inverse of
 *    %GTD_EASE_IN_SINE
 * @GTD_EASE_IN_OUT_SINE: sine wave tweening, combining
 *    %GTD_EASE_IN_SINE and %GTD_EASE_OUT_SINE
 * @GTD_EASE_IN_EXPO: exponential tweening
 * @GTD_EASE_OUT_EXPO: exponential tweening, inverse of
 *    %GTD_EASE_IN_EXPO
 * @GTD_EASE_IN_OUT_EXPO: exponential tweening, combining
 *    %GTD_EASE_IN_EXPO and %GTD_EASE_OUT_EXPO
 * @GTD_EASE_IN_CIRC: circular tweening
 * @GTD_EASE_OUT_CIRC: circular tweening, inverse of
 *    %GTD_EASE_IN_CIRC
 * @GTD_EASE_IN_OUT_CIRC: circular tweening, combining
 *    %GTD_EASE_IN_CIRC and %GTD_EASE_OUT_CIRC
 * @GTD_EASE_IN_ELASTIC: elastic tweening, with offshoot on start
 * @GTD_EASE_OUT_ELASTIC: elastic tweening, with offshoot on end
 * @GTD_EASE_IN_OUT_ELASTIC: elastic tweening with offshoot on both ends
 * @GTD_EASE_IN_BACK: overshooting cubic tweening, with
 *   backtracking on start
 * @GTD_EASE_OUT_BACK: overshooting cubic tweening, with
 *   backtracking on end
 * @GTD_EASE_IN_OUT_BACK: overshooting cubic tweening, with
 *   backtracking on both ends
 * @GTD_EASE_IN_BOUNCE: exponentially decaying parabolic (bounce)
 *   tweening, with bounce on start
 * @GTD_EASE_OUT_BOUNCE: exponentially decaying parabolic (bounce)
 *   tweening, with bounce on end
 * @GTD_EASE_IN_OUT_BOUNCE: exponentially decaying parabolic (bounce)
 *   tweening, with bounce on both ends
 * @GTD_ANIMATION_LAST: last animation mode, used as a guard for
 *   registered global alpha functions
 *
 * The animation modes used by #ClutterAnimatable. This
 * enumeration can be expanded in later versions of Clutter.
 *
 * <figure id="easing-modes">
 *   <title>Easing modes provided by Clutter</title>
 *   <graphic fileref="easing-modes.png" format="PNG"/>
 * </figure>
 *
 * Every global alpha function registered using clutter_alpha_register_func()
 * or clutter_alpha_register_closure() will have a logical id greater than
 * %GTD_ANIMATION_LAST.
 *
 * Since: 1.0
 */
typedef enum
{
  GTD_CUSTOM_MODE = 0,

  /* linear */
  GTD_EASE_LINEAR,

  /* quadratic */
  GTD_EASE_IN_QUAD,
  GTD_EASE_OUT_QUAD,
  GTD_EASE_IN_OUT_QUAD,

  /* cubic */
  GTD_EASE_IN_CUBIC,
  GTD_EASE_OUT_CUBIC,
  GTD_EASE_IN_OUT_CUBIC,

  /* quartic */
  GTD_EASE_IN_QUART,
  GTD_EASE_OUT_QUART,
  GTD_EASE_IN_OUT_QUART,

  /* quintic */
  GTD_EASE_IN_QUINT,
  GTD_EASE_OUT_QUINT,
  GTD_EASE_IN_OUT_QUINT,

  /* sinusoidal */
  GTD_EASE_IN_SINE,
  GTD_EASE_OUT_SINE,
  GTD_EASE_IN_OUT_SINE,

  /* exponential */
  GTD_EASE_IN_EXPO,
  GTD_EASE_OUT_EXPO,
  GTD_EASE_IN_OUT_EXPO,

  /* circular */
  GTD_EASE_IN_CIRC,
  GTD_EASE_OUT_CIRC,
  GTD_EASE_IN_OUT_CIRC,

  /* elastic */
  GTD_EASE_IN_ELASTIC,
  GTD_EASE_OUT_ELASTIC,
  GTD_EASE_IN_OUT_ELASTIC,

  /* overshooting cubic */
  GTD_EASE_IN_BACK,
  GTD_EASE_OUT_BACK,
  GTD_EASE_IN_OUT_BACK,

  /* exponentially decaying parabolic */
  GTD_EASE_IN_BOUNCE,
  GTD_EASE_OUT_BOUNCE,
  GTD_EASE_IN_OUT_BOUNCE,

  /* guard, before registered alpha functions */
  GTD_EASE_LAST
} GtdEaseMode;

/**
 * GtdTimelineDirection:
 * @GTD_TIMELINE_FORWARD: forward direction for a timeline
 * @GTD_TIMELINE_BACKWARD: backward direction for a timeline
 *
 * The direction of a #GtdTimeline
 */
typedef enum
{
  GTD_TIMELINE_FORWARD,
  GTD_TIMELINE_BACKWARD
} GtdTimelineDirection;

G_END_DECLS
