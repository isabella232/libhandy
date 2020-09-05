/*
 * Copyright (C) 2020 Alexander Mikhaylenko <exalm7659@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"
#include <glib/gi18n-lib.h>

#include "hdy-tab-private.h"
#include "hdy-css-private.h"

struct _HdyTab
{
  GtkContainer parent_instance;

  GtkLabel *title;
  GtkStack *stack;
  GtkStack *icon_stack;
  GtkImage *icon;
  GtkImage *secondary_icon;
  GtkButton *secondary_icon_btn;
  GtkStack *pinned_icon_stack;
  GtkImage *pinned_icon;
  GtkImage *pinned_secondary_icon;
  GtkButton *pinned_secondary_icon_btn;

  GtkWidget *child;
  GdkWindow *window;
  gboolean draw_child;

  HdyTabView *view;
  HdyTabPage *page;
  gboolean pinned;
  gboolean dragging;
  gint display_width;

  gboolean hovering;
  gboolean selected;

  GBinding *selected_binding;
  GBinding *title_binding;
};

G_DEFINE_TYPE (HdyTab, hdy_tab, GTK_TYPE_CONTAINER)

enum {
  PROP_0,
  PROP_VIEW,
  PROP_PINNED,
  PROP_DRAGGING,
  PROP_PAGE,
  PROP_DISPLAY_WIDTH,
  PROP_HOVERING,
  PROP_SELECTED,
  LAST_PROP
};

static GParamSpec *props[LAST_PROP];

static inline void
set_style_class (GtkWidget   *widget,
                 const gchar *style_class,
                 gboolean     enabled)
{
  GtkStyleContext *context = gtk_widget_get_style_context (widget);

  if (enabled)
    gtk_style_context_add_class (context, style_class);
  else
    gtk_style_context_remove_class (context, style_class);
}

static void
update_state (HdyTab *self)
{
  GtkStateFlags new_state;

  new_state = gtk_widget_get_state_flags (GTK_WIDGET (self)) &
    ~(GTK_STATE_FLAG_PRELIGHT | GTK_STATE_FLAG_CHECKED);

  if (self->hovering)
    new_state |= GTK_STATE_FLAG_PRELIGHT;

  if (self->selected)
    new_state |= GTK_STATE_FLAG_CHECKED;

  gtk_widget_set_state_flags (GTK_WIDGET (self), new_state, TRUE);
}

static void
update_tooltip (HdyTab *self)
{
  const gchar *tooltip = hdy_tab_page_get_tooltip (self->page);

  if (tooltip)
    gtk_widget_set_tooltip_markup (GTK_WIDGET (self), tooltip);
  else
    gtk_widget_set_tooltip_text (GTK_WIDGET (self),
                                 hdy_tab_page_get_title (self->page));
}

static void
update_icon (HdyTab *self)
{
  GIcon *gicon = hdy_tab_page_get_icon (self->page);
  GIcon *secondary_gicon = hdy_tab_page_get_secondary_icon (self->page);
  gboolean loading = hdy_tab_page_get_loading (self->page);
  const gchar *name = loading ? "spinner" : "icon";
  const gchar *pinned_name = secondary_gicon ? "secondary-icon" : name;

  gtk_image_set_from_gicon (self->icon, gicon, GTK_ICON_SIZE_BUTTON);
  gtk_widget_set_visible (GTK_WIDGET (self->icon_stack),
                          gicon != NULL || loading);
  gtk_stack_set_visible_child_name (self->icon_stack, name);

  gtk_image_set_from_gicon (self->secondary_icon, secondary_gicon, GTK_ICON_SIZE_BUTTON);
  gtk_widget_set_visible (GTK_WIDGET (self->secondary_icon_btn), secondary_gicon != NULL);

  if (gicon)
    gtk_image_set_from_gicon (self->pinned_icon, gicon, GTK_ICON_SIZE_BUTTON);
  else {
    gicon = hdy_tab_view_get_default_icon (self->view);

    gtk_image_set_from_gicon (self->pinned_icon, gicon, GTK_ICON_SIZE_BUTTON);
  }

  gtk_image_set_from_gicon (self->pinned_secondary_icon, secondary_gicon, GTK_ICON_SIZE_BUTTON);
  gtk_stack_set_visible_child_name (self->pinned_icon_stack, pinned_name);
}

static void
update_secondary_icon (HdyTab *self)
{
  gboolean activatable = self->page && hdy_tab_page_get_secondary_icon_activatable (self->page);
  gboolean clickable = activatable && (self->selected || !self->pinned);

  set_style_class (GTK_WIDGET (self->secondary_icon_btn), "clickable", clickable);
  set_style_class (GTK_WIDGET (self->pinned_secondary_icon_btn), "clickable", clickable);
}

static void
update_needs_attention (HdyTab *self)
{
  GtkStyleContext *context = gtk_widget_get_style_context (GTK_WIDGET (self));

  if (hdy_tab_page_get_needs_attention (self->page))
    gtk_style_context_add_class (context, "needs-attention");
  else
    gtk_style_context_remove_class (context, "needs-attention");
}

static void
update_loading (HdyTab *self)
{
  GtkStyleContext *context = gtk_widget_get_style_context (GTK_WIDGET (self));
  update_icon (self);

  if (hdy_tab_page_get_loading (self->page))
    gtk_style_context_add_class (context, "loading");
  else
    gtk_style_context_remove_class (context, "loading");
}

static gboolean
close_idle_cb (HdyTab *self)
{
  hdy_tab_view_close_page (self->view, self->page);

  return G_SOURCE_REMOVE;
}

static void
close_clicked_cb (HdyTab *self)
{
  if (!self->page)
    return;

  /* When animations are disabled, we don't want to immediately remove the
   * whole tab mid-click. Instead, defer it until the click has happened.
   */
  g_idle_add ((GSourceFunc) close_idle_cb, self);
}

static void
secondary_icon_clicked_cb (HdyTab *self)
{
  gboolean clickable;

  if (!self->page)
    return;

  clickable = hdy_tab_page_get_secondary_icon_activatable (self->page) &&
              (self->selected || !self->pinned);

  if (!clickable) {
    hdy_tab_view_set_selected_page (self->view, self->page);

    return;
  }

  g_signal_emit_by_name (self->view, "secondary-icon-activated", self->page);
}

static void
hdy_tab_measure (GtkWidget      *widget,
                 GtkOrientation  orientation,
                 gint            for_size,
                 gint           *minimum,
                 gint           *natural,
                 gint           *minimum_baseline,
                 gint           *natural_baseline)
{
  HdyTab *self = HDY_TAB (widget);

  if (!self->child || orientation == GTK_ORIENTATION_HORIZONTAL) {
    if (minimum)
      *minimum = 0;

    if (natural)
      *natural = 0;
  } else {
    gtk_widget_get_preferred_height (self->child, minimum, natural);
    hdy_css_measure (widget, orientation, minimum, natural);
  }

  if (minimum_baseline)
    *minimum_baseline = -1;

  if (natural_baseline)
    *natural_baseline = -1;
}

static void
hdy_tab_get_preferred_width (GtkWidget *widget,
                             gint      *minimum,
                             gint      *natural)
{
  hdy_tab_measure (widget, GTK_ORIENTATION_HORIZONTAL, -1,
                   minimum, natural,
                   NULL, NULL);
}

static void
hdy_tab_get_preferred_height (GtkWidget *widget,
                              gint      *minimum,
                              gint      *natural)
{
  hdy_tab_measure (widget, GTK_ORIENTATION_VERTICAL, -1,
                   minimum, natural,
                   NULL, NULL);
}

static void
hdy_tab_get_preferred_width_for_height (GtkWidget *widget,
                                        gint       height,
                                        gint      *minimum,
                                        gint      *natural)
{
  hdy_tab_measure (widget, GTK_ORIENTATION_HORIZONTAL, height,
                   minimum, natural,
                   NULL, NULL);
}

static void
hdy_tab_get_preferred_height_for_width (GtkWidget *widget,
                                        gint       width,
                                        gint      *minimum,
                                        gint      *natural)
{
  hdy_tab_measure (widget, GTK_ORIENTATION_VERTICAL, width,
                   minimum, natural,
                   NULL, NULL);
}

static void
hdy_tab_size_allocate (GtkWidget     *widget,
                       GtkAllocation *allocation)
{
  HdyTab *self = HDY_TAB (widget);
  gint width_diff = allocation->width;
  GtkAllocation child_alloc, clip;

  hdy_css_size_allocate_self (widget, allocation);

  gtk_widget_set_allocation (widget, allocation);

  if (self->window)
    gdk_window_move_resize (self->window,
                            allocation->x, allocation->y,
                            allocation->width, allocation->height);

  child_alloc = *allocation;
  child_alloc.x = 0;
  child_alloc.y = 0;

  hdy_css_size_allocate_children (widget, &child_alloc);

  width_diff = MAX (0, width_diff - child_alloc.width);

  if (self->child) {
    gint width = MAX (child_alloc.width, self->display_width - width_diff);
    gint min;

    gtk_widget_get_preferred_width (self->child, &min, NULL);

    self->draw_child = width >= min && child_alloc.height > 0;

    gtk_widget_set_child_visible (self->child, self->draw_child);

    if (self->draw_child) {
      child_alloc.x += (child_alloc.width - width) / 2;
      child_alloc.width = width;

      gtk_widget_size_allocate (self->child, &child_alloc);
    }
  } else
    self->draw_child = FALSE;

  if (self->draw_child) {
    child_alloc.x = 0;
    child_alloc.y = 0;
    child_alloc.width = allocation->width;
    child_alloc.height = allocation->height;

    gtk_widget_set_clip (self->child, &child_alloc);
  }

  gtk_render_background_get_clip (gtk_widget_get_style_context (widget),
                                  allocation->x, allocation->y,
                                  allocation->width, allocation->height,
                                  &clip);

  gtk_widget_set_clip (widget, &clip);
}

static void
hdy_tab_realize (GtkWidget *widget)
{
  HdyTab *self = HDY_TAB (widget);
  GtkAllocation allocation;
  GdkWindowAttr attributes;
  GdkWindowAttributesType attributes_mask;

  gtk_widget_set_realized (widget, TRUE);

  gtk_widget_get_allocation (widget, &allocation);

  attributes.x = allocation.x;
  attributes.y = allocation.y;
  attributes.width = allocation.width;
  attributes.height = allocation.height;
  attributes.window_type = GDK_WINDOW_CHILD;
  attributes.wclass = GDK_INPUT_OUTPUT;
  attributes.visual = gtk_widget_get_visual (widget);
  attributes.event_mask = gtk_widget_get_events (widget);
  attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL;

  self->window = gdk_window_new (gtk_widget_get_parent_window (widget),
                                 &attributes,
                                 attributes_mask);

  gtk_widget_set_window (widget, self->window);
  gtk_widget_register_window (widget, self->window);

  if (self->child)
    gtk_widget_set_parent_window (self->child, self->window);
}

static void
hdy_tab_unrealize (GtkWidget *widget)
{
  HdyTab *self = HDY_TAB (widget);

  GTK_WIDGET_CLASS (hdy_tab_parent_class)->unrealize (widget);

  self->window = NULL;
}

static gboolean
hdy_tab_draw (GtkWidget *widget,
              cairo_t   *cr)
{
  HdyTab *self = HDY_TAB (widget);

  hdy_css_draw (widget, cr);

  if (self->draw_child)
    gtk_container_propagate_draw (GTK_CONTAINER (self), self->child, cr);

  return GDK_EVENT_PROPAGATE;
}

static void
hdy_tab_add (GtkContainer *container,
             GtkWidget    *widget)
{
  HdyTab *self = HDY_TAB (container);

  if (self->child)
    return;

  self->child = widget;

  if (self->child) {
    gtk_widget_set_parent (self->child, GTK_WIDGET (self));

    if (self->window)
      gtk_widget_set_parent_window (self->child, self->window);
  }

  gtk_widget_queue_resize (GTK_WIDGET (self));
}

static void
hdy_tab_remove (GtkContainer *container,
                GtkWidget    *widget)
{
  HdyTab *self = HDY_TAB (container);

  if (widget != self->child)
    return;

  g_clear_pointer (&self->child, gtk_widget_unparent);

  gtk_widget_queue_resize (GTK_WIDGET (self));
}

static void
hdy_tab_forall (GtkContainer *container,
                gboolean      include_internals,
                GtkCallback   callback,
                gpointer      callback_data)
{
  HdyTab *self = HDY_TAB (container);

  if (!include_internals)
    return;

  if (self->child)
    callback (self->child, callback_data);
}

static void
hdy_tab_constructed (GObject *object)
{
  HdyTab *self = HDY_TAB (object);

  G_OBJECT_CLASS (hdy_tab_parent_class)->constructed (object);

  gtk_stack_set_visible_child_name (self->stack,
                                    self->pinned ? "pinned" : "regular");

  if (self->pinned)
    gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (self)),
                                 "pinned");

  if (self->dragging)
    g_object_set (self, "selected", TRUE, NULL);

  g_signal_connect_object (self->view, "notify::default-icon",
                           G_CALLBACK (update_icon), self,
                           G_CONNECT_SWAPPED);
}

static void
hdy_tab_get_property (GObject    *object,
                      guint       prop_id,
                      GValue     *value,
                      GParamSpec *pspec)
{
  HdyTab *self = HDY_TAB (object);

  switch (prop_id) {
  case PROP_VIEW:
    g_value_set_object (value, self->view);
    break;

  case PROP_PAGE:
    g_value_set_object (value, self->page);
    break;

  case PROP_PINNED:
    g_value_set_boolean (value, self->pinned);
    break;

  case PROP_DRAGGING:
    g_value_set_boolean (value, self->dragging);
    break;

  case PROP_DISPLAY_WIDTH:
    g_value_set_int (value, hdy_tab_get_display_width (self));
    break;

  case PROP_HOVERING:
    g_value_set_boolean (value, hdy_tab_get_hovering (self));
    break;

  case PROP_SELECTED:
    g_value_set_boolean (value, hdy_tab_get_selected (self));
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
hdy_tab_set_property (GObject      *object,
                      guint         prop_id,
                      const GValue *value,
                      GParamSpec   *pspec)
{
  HdyTab *self = HDY_TAB (object);

  switch (prop_id) {
  case PROP_VIEW:
    self->view = g_value_get_object (value);
    break;

  case PROP_PAGE:
    hdy_tab_set_page (self, g_value_get_object (value));
    break;

  case PROP_PINNED:
    self->pinned = g_value_get_boolean (value);
    update_secondary_icon (self);
    break;

  case PROP_DRAGGING:
    self->dragging = g_value_get_boolean (value);
    break;

  case PROP_DISPLAY_WIDTH:
    hdy_tab_set_display_width (self, g_value_get_int (value));
    break;

  case PROP_HOVERING:
    hdy_tab_set_hovering (self, g_value_get_boolean (value));
    break;

  case PROP_SELECTED:
    hdy_tab_set_selected (self, g_value_get_boolean (value));
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
hdy_tab_dispose (GObject *object)
{
  HdyTab *self = HDY_TAB (object);

  hdy_tab_set_page (self, NULL);

  G_OBJECT_CLASS (hdy_tab_parent_class)->dispose (object);
}

static void
hdy_tab_class_init (HdyTabClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GtkContainerClass *container_class = GTK_CONTAINER_CLASS (klass);

  object_class->dispose = hdy_tab_dispose;
  object_class->constructed = hdy_tab_constructed;
  object_class->get_property = hdy_tab_get_property;
  object_class->set_property = hdy_tab_set_property;

  widget_class->get_preferred_width = hdy_tab_get_preferred_width;
  widget_class->get_preferred_height = hdy_tab_get_preferred_height;
  widget_class->get_preferred_width_for_height = hdy_tab_get_preferred_width_for_height;
  widget_class->get_preferred_height_for_width = hdy_tab_get_preferred_height_for_width;
  widget_class->size_allocate = hdy_tab_size_allocate;
  widget_class->realize = hdy_tab_realize;
  widget_class->unrealize = hdy_tab_unrealize;
  widget_class->draw = hdy_tab_draw;

  container_class->add = hdy_tab_add;
  container_class->remove = hdy_tab_remove;
  container_class->forall = hdy_tab_forall;

  props[PROP_VIEW] =
    g_param_spec_object ("view",
                         _("View"),
                         _("View"),
                         HDY_TYPE_TAB_VIEW,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  props[PROP_PINNED] =
    g_param_spec_boolean ("pinned",
                          _("Pinned"),
                          _("Pinned"),
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  props[PROP_DRAGGING] =
    g_param_spec_boolean ("dragging",
                          _("Dragging"),
                          _("Dragging"),
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  props[PROP_PAGE] =
    g_param_spec_object ("page",
                         _("Page"),
                         _("Page"),
                         HDY_TYPE_TAB_PAGE,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_DISPLAY_WIDTH] =
    g_param_spec_int ("display-width",
                      _("Display Width"),
                      _("Display Width"),
                      0, G_MAXINT, 0,
                      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_HOVERING] =
    g_param_spec_boolean ("hovering",
                          _("Hovering"),
                          _("Hovering"),
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_SELECTED] =
    g_param_spec_boolean ("selected",
                          _("Selected"),
                          _("Selected"),
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/sm/puri/handy/ui/hdy-tab.ui");
  gtk_widget_class_bind_template_child (widget_class, HdyTab, title);
  gtk_widget_class_bind_template_child (widget_class, HdyTab, stack);
  gtk_widget_class_bind_template_child (widget_class, HdyTab, icon_stack);
  gtk_widget_class_bind_template_child (widget_class, HdyTab, icon);
  gtk_widget_class_bind_template_child (widget_class, HdyTab, secondary_icon);
  gtk_widget_class_bind_template_child (widget_class, HdyTab, secondary_icon_btn);
  gtk_widget_class_bind_template_child (widget_class, HdyTab, pinned_icon_stack);
  gtk_widget_class_bind_template_child (widget_class, HdyTab, pinned_icon);
  gtk_widget_class_bind_template_child (widget_class, HdyTab, pinned_secondary_icon);
  gtk_widget_class_bind_template_child (widget_class, HdyTab, pinned_secondary_icon_btn);
  gtk_widget_class_bind_template_callback (widget_class, close_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, secondary_icon_clicked_cb);

  gtk_widget_class_set_css_name (widget_class, "tab");
}

static void
hdy_tab_init (HdyTab *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

HdyTab *
hdy_tab_new (HdyTabView *view,
             gboolean    pinned,
             gboolean    dragging)
{
  g_assert (HDY_IS_TAB_VIEW (view));

  return g_object_new (HDY_TYPE_TAB,
                       "view", view,
                       "pinned", pinned,
                       "dragging", dragging,
                       NULL);
}

void
hdy_tab_set_page (HdyTab     *self,
                  HdyTabPage *page)
{
  if (self->page == page)
    return;

  if (self->page) {
    g_signal_handlers_disconnect_by_func (self->page, update_tooltip, self);
    g_signal_handlers_disconnect_by_func (self->page, update_icon, self);
    g_signal_handlers_disconnect_by_func (self->page, update_secondary_icon, self);
    g_signal_handlers_disconnect_by_func (self->page, update_needs_attention, self);
    g_signal_handlers_disconnect_by_func (self->page, update_loading, self);
    g_clear_pointer (&self->selected_binding, g_binding_unbind);
    g_clear_pointer (&self->title_binding, g_binding_unbind);
  }

  g_set_object (&self->page, page);

  if (self->page) {
    update_state (self);
    update_tooltip (self);
    update_icon (self);
    update_secondary_icon (self);
    update_needs_attention (self);
    update_loading (self);

    g_signal_connect_object (self->page, "notify::title",
                             G_CALLBACK (update_tooltip), self,
                             G_CONNECT_SWAPPED);
    g_signal_connect_object (self->page, "notify::tooltip",
                             G_CALLBACK (update_tooltip), self,
                             G_CONNECT_SWAPPED);
    g_signal_connect_object (self->page, "notify::icon",
                             G_CALLBACK (update_icon), self,
                             G_CONNECT_SWAPPED);
    g_signal_connect_object (self->page, "notify::secondary-icon",
                             G_CALLBACK (update_icon), self,
                             G_CONNECT_SWAPPED);
    g_signal_connect_object (self->page, "notify::secondary-icon-activatable",
                             G_CALLBACK (update_secondary_icon), self,
                             G_CONNECT_SWAPPED);
    g_signal_connect_object (self->page, "notify::needs-attention",
                             G_CALLBACK (update_needs_attention), self,
                             G_CONNECT_SWAPPED);
    g_signal_connect_object (self->page, "notify::loading",
                             G_CALLBACK (update_loading), self,
                             G_CONNECT_SWAPPED);

    if (!self->dragging)
      self->selected_binding = g_object_bind_property (self->page, "selected",
                                                       self, "selected",
                                                       G_BINDING_SYNC_CREATE);

    self->title_binding = g_object_bind_property (self->page, "title",
                                                  self->title, "label",
                                                  G_BINDING_SYNC_CREATE);
  }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PAGE]);
}

GtkWidget *
hdy_tab_get_child (HdyTab *self)
{
  return self->child;
}

gint
hdy_tab_get_child_min_width (HdyTab *self)
{
  gint min = 0;

  if (self->child)
    gtk_widget_get_preferred_width (self->child, &min, NULL);

  hdy_css_measure (GTK_WIDGET (self), GTK_ORIENTATION_HORIZONTAL, &min, NULL);

  return min;
}

gint
hdy_tab_get_display_width (HdyTab *self)
{
  return self->display_width;
}

void
hdy_tab_set_display_width (HdyTab *self,
                           gint    width)
{
  if (self->display_width == width)
    return;

  self->display_width = width;

  gtk_widget_queue_resize (GTK_WIDGET (self));

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_DISPLAY_WIDTH]);
}

gboolean
hdy_tab_get_hovering (HdyTab *self)
{
  return self->hovering;
}

void
hdy_tab_set_hovering (HdyTab   *self,
                      gboolean  hovering)
{
  if (self->hovering == hovering)
    return;

  self->hovering = hovering;

  update_state (self);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_HOVERING]);
}

gboolean
hdy_tab_get_selected (HdyTab *self)
{
  return self->selected;
}

void
hdy_tab_set_selected (HdyTab   *self,
                      gboolean  selected)
{
  if (self->selected == selected)
    return;

  self->selected = selected;

  update_state (self);
  update_secondary_icon (self);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SELECTED]);
}
