/*
 * Copyright (C) 2020 Alexander Mikhaylenko <exalm7659@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"
#include <glib/gi18n-lib.h>

#include "hdy-tab-view-group.h"

/**
 * SECTION:hdy-tab-view-group
 * @short_description: TBD
 * @title: HdyTabViewGroup
 * @See_also: #HdyTabViewGroup
 *
 * TBD
 *
 * Since: 1.2
 */

struct _HdyTabView
{
  GObject parent_instance;

  GList *views;
  gboolean is_dragging;
};

G_DEFINE_TYPE (HdyTabViewGroup, hdy_tab_view_group, G_TYPE_OBJECT)

static void
hdy_tab_view_group_class_init (HdyTabViewGroup *klass)
{
}

static void
hdy_tab_view_group_init (HdyTabViewGroup *self)
{
}
/**
 * hdy_tab_view_group_new:
 *
 * Creates a new #HdyTabViewGroup widget.
 *
 * Returns: a new #HdyTabViewGroup
 *
 * Since: 1.2
 */
HdyTabViewGroup *
hdy_tab_view_group_new (void)
{
  return g_object_new (HDY_TYPE_TAB_VIEW_GROUP, NULL);
}

/**
 * hdy_tab_view_group_add_view:
 * @self: a #HdyTabView
 * @view: TBD
 *
 * TBD
 *
 * Since: 1.2
 */
void
hdy_tab_view_group_add_view (HdyTabViewGroup *self,
                             HdyTabView      *view)
{
  g_return_if_fail (HDY_IS_TAB_VIEW_GROUP (self));
  g_return_if_fail (HDY_IS_TAB_VIEW (view));

  // remove from the old group here first


}

/**
 * hdy_tab_view_group_remove_view:
 * @self: a #HdyTabView
 * @view: TBD
 *
 * TBD
 *
 * Since: 1.2
 */
void
hdy_tab_view_group_remove_view (HdyTabViewGroup *self,
                                HdyTabView      *view)
{
  g_return_if_fail (HDY_IS_TAB_VIEW_GROUP (self));
  g_return_if_fail (HDY_IS_TAB_VIEW (view));


}

/**
 * hdy_tab_view_group_list_views:
 * @self: a #HdyTabView
 *
 * TBD
 *
 * Returns: (element-type HdyTabView) (transfer container): TBD
 *
 * Since: 1.2
 */
GList *
hdy_tab_view_group_list_views (HdyTabViewGroup *self)
{
  g_return_val_if_fail (HDY_IS_TAB_VIEW_GROUP (self), NULL);

  return g_list_copy (self->views);
}
