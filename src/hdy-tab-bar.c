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
 * @short_description: TBD
 * @title: HdyTabBar
 * @See_also: #HdyTabView
 *
 * TBD
 *
 * Since: 1.2
 */

/**
 * HdyTabBarPosition:
 * @HDY_TAB_BAR_POSITION_TOP: TBD
 * @HDY_TAB_BAR_POSITION_BOTTOM: TBD
 *
 * TBD
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
  HdyTabBarPosition position;
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
  PROP_POSITION,
  LAST_PROP
};

static GParamSpec *props[LAST_PROP];

static void
update_autohide_cb (HdyTabBar *self)
{
  guint n_tabs = 0, n_pinned_tabs = 0;
  gboolean is_dragging;

  if (!self->view) {
    gtk_revealer_set_reveal_child (self->revealer, FALSE);

    return;
  }

  n_tabs = hdy_tab_view_get_n_pages (self->view);
  n_pinned_tabs = hdy_tab_view_get_n_pinned_pages (self->view);
  is_dragging = hdy_tab_view_get_is_dragging (self->view);

  gtk_revealer_set_reveal_child (self->revealer,
                                 n_tabs > 1 ||
                                 n_pinned_tabs >= 1 ||
                                 is_dragging);
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
page_pinned_cb (HdyTabBar  *self,
                HdyTabPage *page)
{
  gboolean should_focus = hdy_tab_box_is_page_focused (self->scroll_box, page);

  hdy_tab_box_remove_page (self->scroll_box, page);
  hdy_tab_box_add_page (self->pinned_box, page,
                        hdy_tab_view_get_n_pinned_pages (self->view));

  if (should_focus)
    hdy_tab_box_try_focus_selected_tab (self->pinned_box);
}

static void
page_unpinned_cb (HdyTabBar  *self,
                  HdyTabPage *page)
{
  gboolean should_focus = hdy_tab_box_is_page_focused (self->pinned_box, page);

  hdy_tab_box_remove_page (self->pinned_box, page);
  hdy_tab_box_add_page (self->scroll_box, page,
                        hdy_tab_view_get_n_pinned_pages (self->view));

  if (should_focus)
    hdy_tab_box_try_focus_selected_tab (self->scroll_box);
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

  if (!self->view)
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

  case PROP_POSITION:
    g_value_set_enum (value, hdy_tab_bar_get_position (self));
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

  case PROP_POSITION:
    hdy_tab_bar_set_position (self, g_value_get_enum (value));
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
   * TBD
   *
   * Since: 1.2
   */
  props[PROP_VIEW] =
    g_param_spec_object ("view",
                         _("View"),
                         _("View"),
                         HDY_TYPE_TAB_VIEW,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * HdyTabBar:start-action-widget:
   *
   * TBD
   *
   * Since: 1.2
   */
  props[PROP_START_ACTION_WIDGET] =
    g_param_spec_object ("start-action-widget",
                         _("Start Action Widget"),
                         _("Start Action Widget"),
                         GTK_TYPE_WIDGET,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * HdyTabBar:end-action-widget:
   *
   * TBD
   *
   * Since: 1.2
   */
  props[PROP_END_ACTION_WIDGET] =
    g_param_spec_object ("end-action-widget",
                         _("End Action Widget"),
                         _("End Action Widget"),
                         GTK_TYPE_WIDGET,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * HdyTabBar:position:
   *
   * TBD
   *
   * Since: 1.2
   */
  props[PROP_POSITION] =
    g_param_spec_enum ("position",
                       _("Position"),
                       _("Position"),
                       HDY_TYPE_TAB_BAR_POSITION,
                       HDY_TAB_BAR_POSITION_TOP,
                       G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

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
 * TBD
 *
 * Returns: (transfer none) (nullable): TBD
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
 * @view: (nullable): TBD
 *
 * TBD
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
    g_signal_handlers_disconnect_by_func (self->view, page_pinned_cb, self);
    g_signal_handlers_disconnect_by_func (self->view, page_unpinned_cb, self);
  }

  g_set_object (&self->view, view);

  if (self->view) {
    g_signal_connect_object (self->view, "notify::is-dragging",
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
    g_signal_connect_object (self->view, "page-pinned",
                             G_CALLBACK (page_pinned_cb), self,
                             G_CONNECT_SWAPPED);
    g_signal_connect_object (self->view, "page-unpinned",
                             G_CALLBACK (page_unpinned_cb), self,
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
 * TBD
 *
 * Returns: (transfer none) (nullable): TBD
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
 * @widget: (transfer none) (nullable): TBD
 *
 * TBD
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

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_START_ACTION_WIDGET]);
}

/**
 * hdy_tab_bar_get_end_action_widget:
 * @self: a #HdyTabBar
 *
 * TBD
 *
 * Returns: (transfer none) (nullable): TBD
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
 * @widget: (transfer none) (nullable): TBD
 *
 * TBD
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

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_END_ACTION_WIDGET]);
}

/**
 * hdy_tab_bar_get_position:
 * @self: a #HdyTabBar
 *
 * TBD
 *
 * Returns: TBD
 *
 * Since: 1.2
 */
HdyTabBarPosition
hdy_tab_bar_get_position (HdyTabBar *self)
{
  g_return_val_if_fail (HDY_IS_TAB_BAR (self), 0);

  return self->position;
}

/**
 * hdy_tab_bar_set_position:
 * @self: a #HdyTabBar
 * @position: TBD
 *
 * TBD
 *
 * Since: 1.2
 */
void
hdy_tab_bar_set_position (HdyTabBar         *self,
                          HdyTabBarPosition  position)
{
  GtkStyleContext *context;

  g_return_if_fail (HDY_IS_TAB_BAR (self));

  if (self->position == position)
    return;

  context = gtk_widget_get_style_context (GTK_WIDGET (self));

  self->position = position;

  switch (position) {
  case HDY_TAB_BAR_POSITION_TOP:
    gtk_style_context_add_class (context, "top");
    gtk_style_context_remove_class (context, "bottom");
    gtk_revealer_set_transition_type (self->revealer,
                                      GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    break;

  case HDY_TAB_BAR_POSITION_BOTTOM:
    gtk_style_context_add_class (context, "bottom");
    gtk_style_context_remove_class (context, "top");
    gtk_revealer_set_transition_type (self->revealer,
                                      GTK_REVEALER_TRANSITION_TYPE_SLIDE_UP);
    break;

  default:
    g_assert_not_reached ();
  }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_POSITION]);
}
