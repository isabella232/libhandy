/*
 * Copyright (C) 2020 Alexander Mikhaylenko <exalm7659@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#if !defined(_HANDY_INSIDE) && !defined(HANDY_COMPILATION)
#error "Only <handy.h> can be included directly."
#endif

#include "hdy-tab-view.h"

G_BEGIN_DECLS

void hdy_tab_view_detach_page   (HdyTabView *self,
                                 HdyTabPage *page);
void hdy_tab_view_attach_page   (HdyTabView *self,
                                 HdyTabPage *page,
                                 gint        position);

G_END_DECLS
