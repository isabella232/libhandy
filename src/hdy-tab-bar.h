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

#include <gtk/gtk.h>
#include "hdy-enums.h"
#include "hdy-tab-view.h"

G_BEGIN_DECLS

#define HDY_TYPE_TAB_BAR (hdy_tab_bar_get_type())

HDY_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (HdyTabBar, hdy_tab_bar, HDY, TAB_BAR, GtkBin)

HDY_AVAILABLE_IN_ALL
HdyTabBar *hdy_tab_bar_new (void);

HDY_AVAILABLE_IN_ALL
HdyTabView *hdy_tab_bar_get_view (HdyTabBar *self);
HDY_AVAILABLE_IN_ALL
void        hdy_tab_bar_set_view (HdyTabBar  *self,
                                  HdyTabView *view);

HDY_AVAILABLE_IN_ALL
GtkWidget *hdy_tab_bar_get_start_action_widget (HdyTabBar *self);
HDY_AVAILABLE_IN_ALL
void       hdy_tab_bar_set_start_action_widget (HdyTabBar *self,
                                                GtkWidget *widget);

HDY_AVAILABLE_IN_ALL
GtkWidget *hdy_tab_bar_get_end_action_widget (HdyTabBar *self);
HDY_AVAILABLE_IN_ALL
void       hdy_tab_bar_set_end_action_widget (HdyTabBar *self,
                                              GtkWidget *widget);

HDY_AVAILABLE_IN_ALL
gboolean hdy_tab_bar_get_autohide (HdyTabBar *self);
HDY_AVAILABLE_IN_ALL
void     hdy_tab_bar_set_autohide (HdyTabBar *self,
                                   gboolean   autohide);

HDY_AVAILABLE_IN_ALL
gboolean hdy_tab_bar_get_tabs_revealed (HdyTabBar *self);

G_END_DECLS
