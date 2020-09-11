/*
 * Copyright (C) 2020 Alexander Mikhaylenko <exalm7659@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"
#include <glib/gi18n-lib.h>

#include "hdy-tab-bar-private.h"
#include "hdy-tab-box-private.h"

/**
 * SECTION:hdy-tab-bar
 * @short_description: A tab bar for #HdyTabView
 * @title: HdyTabBar
 * @See_also: #HdyTabView
 *
 * The #HdyTabBar widget is a tab bar that can be used with conjunction with
 * #HdyTabView.
 *
 * #HdyTabBar can autohide and can optionally contain action widgets on both
 * sides of the tabs.
 *
 * When there's not enough space to show all the tabs, #HdyTabBar will scroll
 * them. Pinned tabs always stay visible and aren't a part of the scrollable
 * area.
 *
 * # CSS nodes
 *
 * #HdyTabBar has a single CSS node with name tabbar.
 *
 * |[<!-- language="plain" -->
 * tabbar
 * ╰── revealer
 *     ╰── box
 *         ├── widget.start-action
 *         │   ╰── [ Start Action Widget ]
 *         ├── tabbox.pinned
 *         ├── scrolledwindow
 *         │   ╰── tabbox
 *         ╰── widget.end-action
 *             ╰── [ End Action Widget ]
 * ]|
 *
 * TODO this is all wrong; need to describe tabs
 *
 * Since: 1.2
 */

struct _HdyTabBar
{
  GtkBin parent_instance;

  GtkRevealer *revealer;
  HdyTabBox *pinned_box;
  HdyTabBox *scroll_box;
  GtkViewport *viewport;
  GtkScrolledWindow *scrolled_window;
  GtkBin *start_action_bin;
  GtkBin *end_action_bin;

  HdyTabView *view;
  gboolean autohide;
};

static void hdy_tab_bar_buildable_init (GtkBuildableIface *iface);

G_DEFINE_TYPE_WITH_CODE (HdyTabBar, hdy_tab_bar, GTK_TYPE_BIN,
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_BUILDABLE,
                         hdy_tab_bar_buildable_init))

enum {
  PROP_0,
  PROP_VIEW,
  PROP_START_ACTION_WIDGET,
  PROP_END_ACTION_WIDGET,
  PROP_AUTOHIDE,
  PROP_TABS_REVEALED,
  LAST_PROP
};

static GParamSpec *props[LAST_PROP];

static void
set_tabs_revealed (HdyTabBar *self,
                   gboolean   tabs_revealed)
{
  if (tabs_revealed == hdy_tab_bar_get_tabs_revealed (self))
    return;

  gtk_revealer_set_reveal_child (self->revealer, tabs_revealed);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_TABS_REVEALED]);
}

static void
update_autohide_cb (HdyTabBar *self)
{
  gint n_tabs = 0, n_pinned_tabs = 0;
  gboolean is_transferring_tab;

  if (!self->view) {
    set_tabs_revealed (self, FALSE);

    return;
  }

  if (!self->autohide) {
    set_tabs_revealed (self, TRUE);

    return;
  }

  n_tabs = hdy_tab_view_get_n_pages (self->view);
  n_pinned_tabs = hdy_tab_view_get_n_pinned_pages (self->view);
  is_transferring_tab = hdy_tab_view_get_is_transferring_tab (self->view);

  set_tabs_revealed (self, n_tabs > 1 || n_pinned_tabs >= 1 || is_transferring_tab);
}

static void
notify_selected_page_cb (HdyTabBar *self)
{
  HdyTabPage *page = hdy_tab_view_get_selected_page (self->view);

  if (hdy_tab_page_get_pinned (page)) {
    hdy_tab_box_select_page (self->pinned_box, page);
    hdy_tab_box_select_page (self->scroll_box, page);
  } else {
    hdy_tab_box_select_page (self->scroll_box, page);
    hdy_tab_box_select_page (self->pinned_box, page);
  }

  /* FIXME HACK elementary active tab shadows */
/*
  var animation = new Animation (this, 0, 1, 300, () => {
      pinned_box.queue_draw ();
      scroll_box.queue_draw ();
  }, null);
  animation.start ();
*/
}

static void
notify_pinned_cb (HdyTabPage *page,
                  GParamSpec *pspec,
                  HdyTabBar  *self)
{
  HdyTabBox *from, *to;
  gboolean should_focus;

  if (hdy_tab_page_get_pinned (page)) {
    from = self->scroll_box;
    to = self->pinned_box;
  } else {
    from = self->pinned_box;
    to = self->scroll_box;
  }

  should_focus = hdy_tab_box_is_page_focused (from, page);

  hdy_tab_box_remove_page (from, page);
  hdy_tab_box_add_page (to, page, hdy_tab_view_get_n_pinned_pages (self->view));

  if (should_focus)
    hdy_tab_box_try_focus_selected_tab (to);
}

static void
page_added_cb (HdyTabBar  *self,
               HdyTabPage *page,
               gint        position)
{
  g_signal_connect_object (page, "notify::pinned",
                           G_CALLBACK (notify_pinned_cb), self,
                           0);
}

static void
page_removed_cb (HdyTabBox  *self,
                 HdyTabPage *page,
                 gint        position)
{
  g_signal_handlers_disconnect_by_func (page, notify_pinned_cb, self);
}

static void
notify_needs_attention_cb (HdyTabBar *self)
{
  GtkStyleContext *context = gtk_widget_get_style_context (GTK_WIDGET (self));
  gboolean left, right;

  g_object_get (self->scroll_box,
                "needs-attention-left", &left,
                "needs-attention-right", &right,
                NULL);

  if (left)
      gtk_style_context_add_class (context, "needs-attention-left");
  else
      gtk_style_context_remove_class (context, "needs-attention-left");

  if (right)
      gtk_style_context_add_class (context, "needs-attention-right");
  else
      gtk_style_context_remove_class (context, "needs-attention-right");

  /* FIXME HACK: Undershoot indicator doesn't redraw on ist own, do a
   * manual animation */
/*
  var animation = new Animation (this, 0, 1, 300, () => {
      pinned_box.queue_draw ();
      scroll_box.queue_draw ();
  }, null);
  animation.start ();
*/
}

static void
stop_kinetic_scrolling_cb (HdyTabBar *self)
{
  /* HACK: Need to cancel kinetic scrolling. If only the built-in adjustment
   * animation API was public, we wouldn't have to do any of this... */
  gtk_scrolled_window_set_kinetic_scrolling (self->scrolled_window, FALSE);
  gtk_scrolled_window_set_kinetic_scrolling (self->scrolled_window, TRUE);
}

static void
view_destroy_cb (HdyTabBar *self)
{
  hdy_tab_bar_set_view (self, NULL);
}

static void
destroy_widget (GtkWidget *widget,
                gpointer   user_data)
{
  gtk_widget_destroy (widget);
}

static void
hdy_tab_bar_destroy (GtkWidget *widget)
{
  gtk_container_forall (GTK_CONTAINER (widget), destroy_widget, NULL);

  GTK_WIDGET_CLASS (hdy_tab_bar_parent_class)->destroy (widget);
}

static gboolean
hdy_tab_bar_focus (GtkWidget        *widget,
                   GtkDirectionType  direction)
{
  HdyTabBar *self = HDY_TAB_BAR (widget);
  gboolean is_rtl;
  GtkDirectionType start, end;

  if (!hdy_tab_bar_get_tabs_revealed (self))
    return GDK_EVENT_PROPAGATE;

  if (!gtk_container_get_focus_child (GTK_CONTAINER (self)))
    return gtk_widget_child_focus (GTK_WIDGET (self->pinned_box), direction) ||
           gtk_widget_child_focus (GTK_WIDGET (self->scroll_box), direction);

  is_rtl = gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL;
  start = is_rtl ? GTK_DIR_RIGHT : GTK_DIR_LEFT;
  end = is_rtl ? GTK_DIR_LEFT : GTK_DIR_RIGHT;

  if (direction == start) {
      if (hdy_tab_view_select_previous_page (self->view))
          return GDK_EVENT_STOP;

      return gtk_widget_keynav_failed (widget, direction);
  }

  if (direction == end) {
      if (hdy_tab_view_select_next_page (self->view))
          return GDK_EVENT_STOP;

      return gtk_widget_keynav_failed (widget, direction);
  }

  return GDK_EVENT_PROPAGATE;
}

static void
hdy_tab_bar_size_allocate (GtkWidget     *widget,
                           GtkAllocation *allocation)
{
  HdyTabBar *self = HDY_TAB_BAR (widget);

  /* On RTL, the adjustment value is modified and will interfere with animations */
  hdy_tab_box_set_block_scrolling (self->scroll_box, TRUE);

  GTK_WIDGET_CLASS (hdy_tab_bar_parent_class)->size_allocate (widget,
                                                              allocation);

  hdy_tab_box_set_block_scrolling (self->scroll_box, FALSE);
}

static void
hdy_tab_bar_forall (GtkContainer *container,
                    gboolean      include_internals,
                    GtkCallback   callback,
                    gpointer      callback_data)
{
  HdyTabBar *self = HDY_TAB_BAR (container);
  GtkWidget *start, *end;

  if (include_internals) {
    GTK_CONTAINER_CLASS (hdy_tab_bar_parent_class)->forall (container,
                                                            include_internals,
                                                            callback,
                                                            callback_data);

    return;
  }

  start = hdy_tab_bar_get_start_action_widget (self);
  end = hdy_tab_bar_get_end_action_widget (self);

  if (start)
    callback (start, callback_data);

  if (end)
    callback (end, callback_data);
}

static void
hdy_tab_bar_dispose (GObject *object)
{
  HdyTabBar *self = HDY_TAB_BAR (object);

  hdy_tab_bar_set_view (self, NULL);

  G_OBJECT_CLASS (hdy_tab_bar_parent_class)->dispose (object);
}

static void
hdy_tab_bar_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  HdyTabBar *self = HDY_TAB_BAR (object);

  switch (prop_id) {
  case PROP_VIEW:
    g_value_set_object (value, hdy_tab_bar_get_view (self));
    break;

  case PROP_START_ACTION_WIDGET:
    g_value_set_object (value, hdy_tab_bar_get_start_action_widget (self));
    break;

  case PROP_END_ACTION_WIDGET:
    g_value_set_object (value, hdy_tab_bar_get_end_action_widget (self));
    break;

  case PROP_AUTOHIDE:
    g_value_set_boolean (value, hdy_tab_bar_get_autohide (self));
    break;

  case PROP_TABS_REVEALED:
    g_value_set_boolean (value, hdy_tab_bar_get_tabs_revealed (self));
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
hdy_tab_bar_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  HdyTabBar *self = HDY_TAB_BAR (object);

  switch (prop_id) {
  case PROP_VIEW:
    hdy_tab_bar_set_view (self, g_value_get_object (value));
    break;

  case PROP_START_ACTION_WIDGET:
    hdy_tab_bar_set_start_action_widget (self, g_value_get_object (value));
    break;

  case PROP_END_ACTION_WIDGET:
    hdy_tab_bar_set_end_action_widget (self, g_value_get_object (value));
    break;

  case PROP_AUTOHIDE:
    hdy_tab_bar_set_autohide (self, g_value_get_boolean (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
hdy_tab_bar_class_init (HdyTabBarClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GtkContainerClass *container_class = GTK_CONTAINER_CLASS (klass);

  object_class->dispose = hdy_tab_bar_dispose;
  object_class->get_property = hdy_tab_bar_get_property;
  object_class->set_property = hdy_tab_bar_set_property;

  widget_class->destroy = hdy_tab_bar_destroy;
  widget_class->focus = hdy_tab_bar_focus;
  widget_class->size_allocate = hdy_tab_bar_size_allocate;

  container_class->forall = hdy_tab_bar_forall;

  /**
   * HdyTabBar:view:
   *
   * The #HdyTabView the tab bar controls.
   *
   * Since: 1.2
   */
  props[PROP_VIEW] =
    g_param_spec_object ("view",
                         _("View"),
                         _("The view the tab bar controls."),
                         HDY_TYPE_TAB_VIEW,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * HdyTabBar:start-action-widget:
   *
   * The widget shown before the tabs.
   *
   * Since: 1.2
   */
  props[PROP_START_ACTION_WIDGET] =
    g_param_spec_object ("start-action-widget",
                         _("Start action widget"),
                         _("The widget shown before the tabs"),
                         GTK_TYPE_WIDGET,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * HdyTabBar:end-action-widget:
   *
   * The widget shown after the tabs.
   *
   * Since: 1.2
   */
  props[PROP_END_ACTION_WIDGET] =
    g_param_spec_object ("end-action-widget",
                         _("End action widget"),
                         _("The widget shown after the tabs"),
                         GTK_TYPE_WIDGET,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * HdyTabBar:autohide:
   *
   * Whether the tabs automatically hide.
   *
   * If set to %TRUE, the tab bar disappears when the associated #HdyTabView
   * has 0 or 1 tab, no pinned tabs, and no tab is being transferred.
   *
   * See #HdyTabBar:tabs-revealed.
   *
   * Since: 1.2
   */
  props[PROP_AUTOHIDE] =
    g_param_spec_boolean ("autohide",
                          _("Autohide"),
                          _("Whether the tabs automatically hide"),
                          TRUE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * HdyTabBar:tabs-revealed:
   *
   * Whether the tabs are currently revealed.
   *
   * See HdyTabBar:autohide.
   *
   * Since: 1.2
   */
  props[PROP_TABS_REVEALED] =
    g_param_spec_boolean ("tabs-revealed",
                          _("Tabs revealed"),
                          _("Whether the tabs are currently revealed"),
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/sm/puri/handy/ui/hdy-tab-bar.ui");
  gtk_widget_class_bind_template_child (widget_class, HdyTabBar, revealer);
  gtk_widget_class_bind_template_child (widget_class, HdyTabBar, pinned_box);
  gtk_widget_class_bind_template_child (widget_class, HdyTabBar, scroll_box);
  gtk_widget_class_bind_template_child (widget_class, HdyTabBar, viewport);
  gtk_widget_class_bind_template_child (widget_class, HdyTabBar, scrolled_window);
  gtk_widget_class_bind_template_child (widget_class, HdyTabBar, start_action_bin);
  gtk_widget_class_bind_template_child (widget_class, HdyTabBar, end_action_bin);
  gtk_widget_class_bind_template_callback (widget_class, notify_needs_attention_cb);
  gtk_widget_class_bind_template_callback (widget_class, stop_kinetic_scrolling_cb);

  gtk_widget_class_set_css_name (widget_class, "tabbar");
}

static void
hdy_tab_bar_init (HdyTabBar *self)
{
  g_type_ensure (HDY_TYPE_TAB_BOX);

  gtk_widget_init_template (GTK_WIDGET (self));

  self->autohide = TRUE;

  hdy_tab_box_set_adjustment (self->scroll_box,
                              gtk_scrolled_window_get_hadjustment (self->scrolled_window));

  /* HdyTabBox scrolls on focus itself, and does it better than GtkViewport */
  gtk_container_set_focus_hadjustment (GTK_CONTAINER (self->viewport), NULL);
}

static void
hdy_tab_bar_buildable_add_child (GtkBuildable *buildable,
                                 GtkBuilder   *builder,
                                 GObject      *child,
                                 const gchar  *type)
{
  HdyTabBar *self = HDY_TAB_BAR (buildable);

  if (!self->revealer) {
    gtk_container_add (GTK_CONTAINER (self), GTK_WIDGET (child));

    return;
  }

  if (!type || !g_strcmp0 (type, "start"))
    hdy_tab_bar_set_start_action_widget (self, GTK_WIDGET (child));
  else if (!g_strcmp0 (type, "end"))
    hdy_tab_bar_set_end_action_widget (self, GTK_WIDGET (child));
  else
    GTK_BUILDER_WARN_INVALID_CHILD_TYPE (HDY_TAB_BAR (self), type);
}

static void
hdy_tab_bar_buildable_init (GtkBuildableIface *iface)
{
  iface->add_child = hdy_tab_bar_buildable_add_child;
}

gboolean
hdy_tab_bar_tabs_have_visible_focus (HdyTabBar *self)
{
  GtkWidget *pinned_focus_child, *scroll_focus_child;

  pinned_focus_child = gtk_container_get_focus_child (GTK_CONTAINER (self->pinned_box));
  scroll_focus_child = gtk_container_get_focus_child (GTK_CONTAINER (self->scroll_box));

  if (pinned_focus_child && gtk_widget_has_visible_focus (pinned_focus_child))
    return TRUE;

  if (scroll_focus_child && gtk_widget_has_visible_focus (scroll_focus_child))
    return TRUE;

  return FALSE;
}

/**
 * hdy_tab_bar_new:
 *
 * Creates a new #HdyTabBar widget.
 *
 * Returns: a new #HdyTabBar
 *
 * Since: 1.2
 */
HdyTabBar *
hdy_tab_bar_new (void)
{
  return g_object_new (HDY_TYPE_TAB_BAR, NULL);
}

/**
 * hdy_tab_bar_get_view:
 * @self: a #HdyTabBar
 *
 * Gets the #HdyTabView @self controls.
 *
 * Returns: (transfer none) (nullable): the #HdyTabView @self controls
 *
 * Since: 1.2
 */
HdyTabView *
hdy_tab_bar_get_view (HdyTabBar *self)
{
  g_return_val_if_fail (HDY_IS_TAB_BAR (self), NULL);

  return self->view;
}

/**
 * hdy_tab_bar_set_view:
 * @self: a #HdyTabBar
 * @view: (nullable): a #HdyTabView
 *
 * Sets the #HdyTabView @self controls.
 *
 * Since: 1.2
 */
void
hdy_tab_bar_set_view (HdyTabBar  *self,
                      HdyTabView *view)
{
  g_return_if_fail (HDY_IS_TAB_BAR (self));
  g_return_if_fail (HDY_IS_TAB_VIEW (view) || view == NULL);

  if (self->view == view)
    return;

  if (self->view) {
    g_signal_handlers_disconnect_by_func (self->view, update_autohide_cb, self);
    g_signal_handlers_disconnect_by_func (self->view, notify_selected_page_cb, self);
  }

  g_set_object (&self->view, view);

  if (self->view) {
    g_signal_connect_object (self->view, "notify::is-transferring-tab",
                             G_CALLBACK (update_autohide_cb), self,
                             G_CONNECT_SWAPPED);
    g_signal_connect_object (self->view, "notify::n-pages",
                             G_CALLBACK (update_autohide_cb), self,
                             G_CONNECT_SWAPPED);
    g_signal_connect_object (self->view, "notify::n-pinned-pages",
                             G_CALLBACK (update_autohide_cb), self,
                             G_CONNECT_SWAPPED);
    g_signal_connect_object (self->view, "notify::selected-page",
                             G_CALLBACK (notify_selected_page_cb), self,
                             G_CONNECT_SWAPPED);
    g_signal_connect_object (self->view, "page-added",
                             G_CALLBACK (page_added_cb), self,
                             G_CONNECT_SWAPPED);
    g_signal_connect_object (self->view, "page-removed",
                             G_CALLBACK (page_removed_cb), self,
                             G_CONNECT_SWAPPED);
    g_signal_connect_object (self->view, "destroy",
                             G_CALLBACK (view_destroy_cb), self,
                             G_CONNECT_SWAPPED);
  }

  hdy_tab_box_set_view (self->pinned_box, view);
  hdy_tab_box_set_view (self->scroll_box, view);

  update_autohide_cb (self);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_VIEW]);
}

/**
 * hdy_tab_bar_get_start_action_widget:
 * @self: a #HdyTabBar
 *
 * Gets the widget shown before the tabs.
 *
 * Returns: (transfer none) (nullable): the widget shown before the tabs, or %NULL
 *
 * Since: 1.2
 */
GtkWidget *
hdy_tab_bar_get_start_action_widget (HdyTabBar *self)
{
  g_return_val_if_fail (HDY_IS_TAB_BAR (self), NULL);

  return gtk_bin_get_child (self->start_action_bin);
}

/**
 * hdy_tab_bar_set_start_action_widget:
 * @self: a #HdyTabBar
 * @widget: (transfer none) (nullable): the widget to show before the tabs, or %NULL
 *
 * Sets the widget to show before the tabs.
 *
 * Since: 1.2
 */
void
hdy_tab_bar_set_start_action_widget (HdyTabBar *self,
                                     GtkWidget *widget)
{
  GtkWidget *old_widget;

  g_return_if_fail (HDY_IS_TAB_BAR (self));
  g_return_if_fail (GTK_IS_WIDGET (widget) || widget == NULL);

  old_widget = gtk_bin_get_child (self->start_action_bin);

  if (old_widget == widget)
    return;

  if (old_widget)
    gtk_container_remove (GTK_CONTAINER (self->start_action_bin), old_widget);

  if (widget)
    gtk_container_add (GTK_CONTAINER (self->start_action_bin), widget);

  gtk_widget_set_visible (GTK_WIDGET (self->start_action_bin), widget != NULL);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_START_ACTION_WIDGET]);
}

/**
 * hdy_tab_bar_get_end_action_widget:
 * @self: a #HdyTabBar
 *
 * Gets the widget shown after the tabs.
 *
 * Returns: (transfer none) (nullable): the widget shown after the tabs, or %NULL
 *
 * Since: 1.2
 */
GtkWidget *
hdy_tab_bar_get_end_action_widget (HdyTabBar *self)
{
  g_return_val_if_fail (HDY_IS_TAB_BAR (self), NULL);

  return gtk_bin_get_child (self->end_action_bin);
}

/**
 * hdy_tab_bar_set_end_action_widget:
 * @self: a #HdyTabBar
 * @widget: (transfer none) (nullable): the widget to show after the tabs, or %NULL
 *
 * Sets the widget to show before the tabs.
 *
 * Since: 1.2
 */
void
hdy_tab_bar_set_end_action_widget (HdyTabBar *self,
                                   GtkWidget *widget)
{
  GtkWidget *old_widget;

  g_return_if_fail (HDY_IS_TAB_BAR (self));
  g_return_if_fail (GTK_IS_WIDGET (widget) || widget == NULL);

  old_widget = gtk_bin_get_child (self->end_action_bin);

  if (old_widget == widget)
    return;

  if (old_widget)
    gtk_container_remove (GTK_CONTAINER (self->end_action_bin), old_widget);

  if (widget)
    gtk_container_add (GTK_CONTAINER (self->end_action_bin), widget);

  gtk_widget_set_visible (GTK_WIDGET (self->end_action_bin), widget != NULL);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_END_ACTION_WIDGET]);
}

/**
 * hdy_tab_bar_get_autohide:
 * @self: a #HdyTabBar
 *
 * Gets whether the tabs automatically hide, see hdy_tab_bar_set_autohide().
 *
 * Returns: whether the tabs automatically hide
 *
 * Since: 1.2
 */
gboolean
hdy_tab_bar_get_autohide (HdyTabBar *self)
{
  g_return_val_if_fail (HDY_IS_TAB_BAR (self), FALSE);

  return self->autohide;
}

/**
 * hdy_tab_bar_set_autohide:
 * @self: a #HdyTabBar
 * @autohide: whether the tabs automatically hide
 *
 * Sets whether the tabs automatically hide.
 *
 * If @autohide is %TRUE, the tab bar disappears when the associated #HdyTabView
 * has 0 or 1 tab, no pinned tabs, and no tab is being transferred.
 *
 * See #HdyTabBar:tabs-revealed.
 *
 * Since: 1.2
 */
void
hdy_tab_bar_set_autohide (HdyTabBar *self,
                          gboolean   autohide)
{
  g_return_if_fail (HDY_IS_TAB_BAR (self));

  autohide = !!autohide;

  if (autohide == self->autohide)
    return;

  self->autohide = autohide;

  update_autohide_cb (self);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_AUTOHIDE]);
}

/**
 * hdy_tab_bar_get_tabs_revealed:
 * @self: a #HdyTabBar
 *
 * Gets the value of the #HdyTabBar:tabs-revealed property.
 *
 * Returns: whether the tabs are current revealed
 *
 * Since: 1.2
 */
gboolean
hdy_tab_bar_get_tabs_revealed (HdyTabBar *self)
{
  g_return_val_if_fail (HDY_IS_TAB_BAR (self), FALSE);

  return gtk_revealer_get_reveal_child (self->revealer);
}
