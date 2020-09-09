/*
 * Copyright (C) 2020 Alexander Mikhaylenko <exalm7659@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#if !defined(_HANDY_INSIDE) && !defined(HANDY_COMPILATION)
#error "Only <handy.h> can be included directly."
#endif

#include "hdy-version.h"

#include <glib-object.h>
#include "hdy-tab-view.h"

G_BEGIN_DECLS

#define HDY_TYPE_TAB_VIEW_GROUP (hdy_tab_view_group_get_type())

HDY_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (HdyTabViewGroup, hdy_tab_view_group, HDY, TAB_VIEW_GROUP, GObject)

HDY_AVAILABLE_IN_ALL
HdyTabViewGroup *hdy_tab_view_group_new (void);

HDY_AVAILABLE_IN_ALL
void             hdy_tab_view_group_add_view    (HdyTabViewGroup *self,
                                                 HdyTabView      *view);
HDY_AVAILABLE_IN_ALL
void             hdy_tab_view_group_remove_view (HdyTabViewGroup *self,
                                                 HdyTabView      *view);
HDY_AVAILABLE_IN_ALL
GList           *hdy_tab_view_group_list_views  (HdyTabViewGroup *self);

G_END_DECLS
