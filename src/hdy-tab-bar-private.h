/*
 * Copyright (C) 2020 Alexander Mikhaylenko <exalm7659@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#if !defined(_HANDY_INSIDE) && !defined(HANDY_COMPILATION)
#error "Only <handy.h> can be included directly."
#endif

#include "hdy-tab-bar.h"

G_BEGIN_DECLS

gboolean hdy_tab_bar_tabs_have_visible_focus (HdyTabBar *self);

G_END_DECLS
