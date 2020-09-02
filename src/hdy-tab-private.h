/*
 * Copyright (C) 2020 Alexander Mikhaylenko <exalm7659@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#if !defined(_HANDY_INSIDE) && !defined(HANDY_COMPILATION)
#error "Only <handy.h> can be included directly."
#endif

#include <gtk/gtk.h>
#include "hdy-tab-view.h"

G_BEGIN_DECLS

#define HDY_TYPE_TAB (hdy_tab_get_type())

G_DECLARE_FINAL_TYPE (HdyTab, hdy_tab, HDY, TAB, GtkContainer)

HdyTab *hdy_tab_new (HdyTabView *view,
                     gboolean    pinned,
                     gboolean    dragging);

void hdy_tab_set_page (HdyTab     *self,
                       HdyTabPage *page);

GtkWidget *hdy_tab_get_child (HdyTab *self);

gint hdy_tab_get_child_min_width (HdyTab *self);

gint hdy_tab_get_display_width (HdyTab *self);
void hdy_tab_set_display_width (HdyTab *self,
                                gint    width);

gboolean hdy_tab_get_hovering (HdyTab *self);
void     hdy_tab_set_hovering (HdyTab   *self,
                               gboolean  hovering);

gboolean hdy_tab_get_selected (HdyTab *self);
void     hdy_tab_set_selected (HdyTab   *self,
                               gboolean  selected);

G_END_DECLS
