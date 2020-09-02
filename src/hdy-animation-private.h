/*
 * Copyright (C) 2019 Purism SPC
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#if !defined(_HANDY_INSIDE) && !defined(HANDY_COMPILATION)
#error "Only <handy.h> can be included directly."
#endif

#include "hdy-animation.h"

G_BEGIN_DECLS

#define HDY_TYPE_ANIMATION (hdy_animation_get_type())

G_DECLARE_FINAL_TYPE (HdyAnimation, hdy_animation, HDY, ANIMATION, GObject)

typedef void (*HdyAnimationValueCallback) (gdouble value, gpointer user_data);
typedef void (*HdyAnimationDoneCallback) (gpointer user_data);

HdyAnimation *hdy_animation_new (GtkWidget                 *widget,
                                 gdouble                    from,
                                 gdouble                    to,
                                 gint64                     duration,
                                 HdyAnimationValueCallback  value_cb,
                                 HdyAnimationDoneCallback   done_cb,
                                 gpointer                   user_data);

void hdy_animation_start (HdyAnimation *self);
void hdy_animation_stop (HdyAnimation *self);

gdouble hdy_animation_get_value (HdyAnimation *self);

gdouble hdy_lerp (gdouble a, gdouble b, gdouble t);
gdouble hdy_ease_in_cubic (gdouble t);

G_END_DECLS
