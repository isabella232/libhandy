/*
 * Copyright (C) 2020 Alexander Mikhaylenko <exalm7659@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"
#include <glib/gi18n-lib.h>

#include "hdy-tab-box-private.h"
#include "hdy-animation-private.h"
#include "hdy-css-private.h"
#include "hdy-tab-private.h"
#include "hdy-tab-bar-private.h"
#include "hdy-tab-view-private.h"
#include <math.h>

/* Border collapsing without glitches */
#define OVERLAP 1
#define DND_THRESHOLD_MULTIPLIER 4

#define AUTOSCROLL_AREA_WIDTH 65
#define AUTOSCROLL_SPEED 2.5

#define OPEN_ANIMATION_DURATION 200
#define CLOSE_ANIMATION_DURATION 200
#define FOCUS_ANIMATION_DURATION 200
#define SCROLL_ANIMATION_DURATION 200
#define RESIZE_ANIMATION_DURATION 200
#define REORDER_ANIMATION_DURATION 250
#define ICON_RESIZE_ANIMATION_DURATION 200

typedef enum {
  TAB_RESIZE_NORMAL,
  TAB_RESIZE_FIXED_TAB_WIDTH,
  TAB_RESIZE_FIXED_END_PADDING
} TabResizeMode;

static const GtkTargetEntry src_targets [] = {
  { "HDY_TAB", GTK_TARGET_SAME_APP, 0 },
  { "application/x-rootwindow-drop", 0, 0 },
};

static const GtkTargetEntry dst_targets [] = {
  { "HDY_TAB", GTK_TARGET_SAME_APP, 0 },
};

typedef struct {
  GtkWidget *window;
  GdkDragContext *context;

  HdyTab *tab;
  GtkBorder tab_margin;

  gint hotspot_x;
  gint hotspot_y;

  gint width;
  gint target_width;
  HdyAnimation *resize_animation;
} DragIcon;

typedef struct {
  HdyTabPage *page;
  HdyTab *tab;

  gint pos;
  gint width;
  gint last_width;

  gdouble end_reorder_offset;
  gdouble reorder_offset;

  HdyAnimation *reorder_animation;
  gboolean reorder_ignore_bounds;

  gdouble appear_progress;
  HdyAnimation *appear_animation;

  gulong notify_needs_attention_id;
} TabInfo;

struct _HdyTabBox
{
  GtkContainer parent_instance;

  gboolean pinned;
  HdyTabBar *tab_bar;
  HdyTabView *view;
  GtkAdjustment *adjustment;
  gboolean needs_attention_left;
  gboolean needs_attention_right;

  GList *tabs;
  gint n_tabs;

  GdkWindow *window;
  GdkWindow *reorder_window;

  GtkMenu *context_menu;
  GtkPopover *touch_menu;
  GtkGesture *touch_menu_gesture;

  gint allocated_width;
  gint last_width;
  gint end_padding;
  gint initial_end_padding;
  TabResizeMode tab_resize_mode;
  HdyAnimation *resize_animation;

  TabInfo *selected_tab;

  gboolean hovering;
  gdouble hover_x;
  gdouble hover_y;
  TabInfo *hovered_tab;

  TabInfo *reordered_tab;
  HdyAnimation *reorder_animation;
  gboolean pressed;

  gint reorder_x;
  gint reorder_y;
  gint reorder_index;
  gint reorder_window_x;
  gboolean continue_reorder;
  gboolean indirect_reordering;
  gint pressed_button;

  gboolean dragging;
  gdouble drag_begin_x;
  gdouble drag_begin_y;
  gdouble drag_offset_x;
  gdouble drag_offset_y;
  GdkSeat *drag_seat;

  guint drag_autoscroll_cb_id;
  gint64 drag_autoscroll_prev_time;

  HdyTabPage *detached_page;
  gint detached_index;
  TabInfo *reorder_placeholder;
  HdyTabPage *placeholder_page;
  gboolean can_remove_placeholder;
  DragIcon *drag_icon;
  gboolean should_detach_into_new_window;
  GtkTargetList *source_targets;

  HdyAnimation *scroll_animation;
  gboolean scroll_animation_done;
  gdouble scroll_animation_from;
  gdouble scroll_animation_offset;
  TabInfo *scroll_animation_tab;
  gboolean block_scrolling;
  gdouble adjustment_prev_value;
};

G_DEFINE_TYPE (HdyTabBox, hdy_tab_box, GTK_TYPE_CONTAINER)

enum {
  PROP_0,
  PROP_PINNED,
  PROP_TAB_BAR,
  PROP_VIEW,
  PROP_ADJUSTMENT,
  PROP_NEEDS_ATTENTION_LEFT,
  PROP_NEEDS_ATTENTION_RIGHT,
  LAST_PROP
};

static GParamSpec *props[LAST_PROP];

enum {
  SIGNAL_STOP_KINETIC_SCROLLING,
  SIGNAL_ACTIVATE_TAB,
  SIGNAL_FOCUS_TAB,
  SIGNAL_REORDER_TAB,
  SIGNAL_LAST_SIGNAL,
};

static guint signals[SIGNAL_LAST_SIGNAL];

/* Helpers */

static void
remove_and_free_tab_info (TabInfo *info)
{
  gtk_widget_unparent (GTK_WIDGET (info->tab));

  g_free (info);
}

static inline gint
get_tab_position (HdyTabBox *self,
                  TabInfo   *info)
{
  if (info == self->reordered_tab) {
    gint pos = 0;
    gdk_window_get_position (self->reorder_window, &pos, NULL);

    return pos;
  }

  return info->pos;
}

static inline TabInfo *
find_tab_info_at (HdyTabBox *self,
                  double     x)
{
  GList *l;

  if (self->reordered_tab) {
    gint pos = get_tab_position (self, self->reordered_tab);

    if (pos <= x && x < pos + self->reordered_tab->width)
      return self->reordered_tab;
  }

  for (l = self->tabs; l; l = l->next) {
    TabInfo *info = l->data;

    if (info != self->reordered_tab &&
        info->pos <= x && x < info->pos + info->width)
      return info;
  }

  return NULL;
}

static inline GList *
find_link_for_page (HdyTabBox  *self,
                    HdyTabPage *page)
{
  GList *l;

  for (l = self->tabs; l; l = l->next) {
    TabInfo *info = l->data;

    if (info->page == page)
      return l;
  }

  return NULL;
}

static inline TabInfo *
find_info_for_page (HdyTabBox  *self,
                    HdyTabPage *page)
{
  GList *l = find_link_for_page (self, page);

  return l ? l->data : NULL;
}

static GList *
find_nth_alive_tab (HdyTabBox *self,
                    uint       position)
{
  GList *l;

  for (l = self->tabs; l; l = l->next) {
    TabInfo *info = l->data;

    if (!info->page)
        continue;

    if (!position--)
        return l;
  }

  return NULL;
}

static inline gint
calculate_tab_width (TabInfo *info,
                     gint     base_width)
{
  return OVERLAP + (gint) ((base_width - OVERLAP) * info->appear_progress);
}

static gint
get_base_tab_width (HdyTabBox *self)
{
  gdouble max_progress = 0;
  gdouble n = 0;
  gdouble used_width;
  GList *l;

  for (l = self->tabs; l; l = l->next) {
    TabInfo *info = l->data;

    max_progress = MAX (max_progress, info->appear_progress);
    n += info->appear_progress;
  }

  used_width = (self->allocated_width + (n + 1) * OVERLAP - self->end_padding) * max_progress;

  return (gint) floor (used_width / n);
}

static gint
predict_tab_width (HdyTabBox *self,
                   TabInfo   *info,
                   gboolean   assume_placeholder)
{
  gint n = self->n_tabs;
  gint width = self->allocated_width;
  gint min;

  if (assume_placeholder && !self->reorder_placeholder)
      n++;

  width += OVERLAP * (n + 1) - self->end_padding;

  min = hdy_tab_get_child_min_width (info->tab);

  return MAX ((gint) floor (width / (gdouble) n), min);
}

static gint
calculate_tab_offset (HdyTabBox *self,
                      TabInfo   *info)
{
  gint width;

  if (!self->reordered_tab)
      return 0;

  width = self->reordered_tab->width - OVERLAP;

  if (gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL)
      width = -width;

  return (gint) round (width * info->reorder_offset);
}

static gboolean
get_widget_coordinates (HdyTabBox *self,
                        GdkEvent  *event,
                        gdouble   *x,
                        gdouble   *y)
{
  GdkWindow *window = gdk_event_get_window (event);
  gdouble tx, ty, out_x = -1, out_y = -1;

  if (!gdk_event_get_coords (event, &tx, &ty))
    goto out;

  while (window && window != self->window) {
    gint window_x, window_y;

    gdk_window_get_position (window, &window_x, &window_y);

    tx += window_x;
    ty += window_y;

    window = gdk_window_get_parent (window);
  }

  if (window) {
    out_x = tx;
    out_y = ty;
    goto out;
  }

out:
  if (x)
    *x = out_x;

  if (y)
    *y = out_y;

  return out_x >= 0 && out_y >= 0;
}

static void
get_visible_range (HdyTabBox *self,
                   gint      *lower,
                   gint      *upper)
{
  gint min, max;
  GtkStyleContext *context = gtk_widget_get_style_context (GTK_WIDGET (self));
  GtkStateFlags flags = gtk_widget_get_state_flags (GTK_WIDGET (self));
  GtkBorder border, padding;

  gtk_style_context_get_border (context, flags, &border);
  gtk_style_context_get_padding (context, flags, &padding);

  min = border.left + padding.left - OVERLAP;
  max = border.left + padding.left + self->allocated_width + OVERLAP;

  if (self->adjustment) {
    GtkBorder margin;
    gint scroll_min, scroll_max;
    gdouble value, page_size;

    gtk_style_context_get_margin (context, flags, &margin);

    value = gtk_adjustment_get_value (self->adjustment);
    page_size = gtk_adjustment_get_page_size (self->adjustment);

    scroll_min = (gint) floor (value);
    scroll_max = (gint) ceil (value + page_size);

    min = MAX (min, scroll_min - margin.left - OVERLAP);
    max = MIN (max, scroll_max - margin.left + OVERLAP);
  }

  if (lower)
    *lower = min;

  if (upper)
    *upper = max;
}

/* Tab resize delay */

static void
resize_animation_value_cb (gdouble  value,
                           gpointer user_data)
{
  HdyTabBox *self = HDY_TAB_BOX (user_data);

  self->end_padding = (gint) floor (self->initial_end_padding * value);

  gtk_widget_queue_resize (GTK_WIDGET (self));
}

static void
resize_animation_done_cb (gpointer user_data)
{
  HdyTabBox *self = HDY_TAB_BOX (user_data);

  self->end_padding = 0;
  gtk_widget_queue_resize (GTK_WIDGET (self));

  g_clear_object (&self->resize_animation);
}

static void
set_tab_resize_mode (HdyTabBox     *self,
                     TabResizeMode  mode)
{
  if (self->tab_resize_mode == mode)
    return;

  if (mode == TAB_RESIZE_FIXED_TAB_WIDTH) {
    GList *l;

    self->last_width = self->allocated_width;

    for (l = self->tabs; l; l = l->next) {
      TabInfo *info = l->data;

      if (info->appear_animation)
        info->last_width = hdy_tab_get_display_width (info->tab);
      else
        info->last_width = info->width;
    }
  } else {
    self->last_width = 0;
  }

  if (mode == TAB_RESIZE_NORMAL) {
    self->initial_end_padding = self->end_padding;

    self->resize_animation =
      hdy_animation_new (GTK_WIDGET (self), 1, 0,
                         RESIZE_ANIMATION_DURATION,
                         resize_animation_value_cb,
                         resize_animation_done_cb,
                         self);

    hdy_animation_start (self->resize_animation);
  }

  self->tab_resize_mode = mode;
}

/* Hover */

static void
update_hover (HdyTabBox *self)
{
  TabInfo *info;

  if (self->dragging)
    return;

  if (!self->hovering) {
    set_tab_resize_mode (self, TAB_RESIZE_NORMAL);

    if (self->hovered_tab) {
      hdy_tab_set_hovering (self->hovered_tab->tab, FALSE);
      self->hovered_tab = NULL;
    }

    return;
  }

  info = find_tab_info_at (self, self->hover_x);

  if (info != self->hovered_tab) {
    if (self->hovered_tab)
      hdy_tab_set_hovering (self->hovered_tab->tab, FALSE);

    self->hovered_tab = info;

    if (self->hovered_tab)
      hdy_tab_set_hovering (self->hovered_tab->tab, TRUE);
  }
}

/* Keybindings */

static void
add_focus_bindings (GtkBindingSet    *binding_set,
                    guint             keysym,
                    GtkDirectionType  direction,
                    gboolean          last)
{
  /* All keypad keysyms are aligned at the same order as non-keypad ones */
  guint keypad_keysym = keysym - GDK_KEY_Left + GDK_KEY_KP_Left;

  gtk_binding_entry_add_signal (binding_set, keysym, 0,
                                "focus-tab", 2,
                                GTK_TYPE_DIRECTION_TYPE, direction,
                                G_TYPE_BOOLEAN, last);
  gtk_binding_entry_add_signal (binding_set, keypad_keysym, 0,
                                "focus-tab", 2,
                                GTK_TYPE_DIRECTION_TYPE, direction,
                                G_TYPE_BOOLEAN, last);
}

static void
add_reorder_bindings (GtkBindingSet    *binding_set,
                      guint             keysym,
                      GtkDirectionType  direction,
                      gboolean          last)
{
  /* All keypad keysyms are aligned at the same order as non-keypad ones */
  guint keypad_keysym = keysym - GDK_KEY_Left + GDK_KEY_KP_Left;

  gtk_binding_entry_add_signal (binding_set, keysym, GDK_MOD1_MASK,
                                "reorder-tab", 2,
                                GTK_TYPE_DIRECTION_TYPE, direction,
                                G_TYPE_BOOLEAN, last);
  gtk_binding_entry_add_signal (binding_set, keypad_keysym, GDK_MOD1_MASK,
                                "reorder-tab", 2,
                                GTK_TYPE_DIRECTION_TYPE, direction,
                                G_TYPE_BOOLEAN, last);
}

static void
activate_tab (HdyTabBox *self)
{
  GtkWidget *content;

  if (!self->selected_tab || !self->selected_tab->page)
    return;

  content = hdy_tab_page_get_content (self->selected_tab->page);

  // FIXME this does nothing somehow
  gtk_widget_grab_focus (content);
}

static void
focus_tab_cb (HdyTabBox        *self,
              GtkDirectionType  direction,
              gboolean          last)
{
  gboolean is_rtl, success = last;

  if (!self->view || !self->selected_tab)
    return;

  is_rtl = gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL;

  if (direction == GTK_DIR_LEFT)
    direction = is_rtl ? GTK_DIR_TAB_FORWARD : GTK_DIR_TAB_BACKWARD;
  else if (direction == GTK_DIR_RIGHT)
    direction = is_rtl ? GTK_DIR_TAB_BACKWARD : GTK_DIR_TAB_FORWARD;

  if (direction == GTK_DIR_TAB_BACKWARD) {
    if (last)
      success = hdy_tab_view_select_first_page (self->view);
    else
      success = hdy_tab_view_select_previous_page (self->view);
  } else if (direction == GTK_DIR_TAB_FORWARD) {
    if (last)
      success = hdy_tab_view_select_last_page (self->view);
    else
      success = hdy_tab_view_select_next_page (self->view);
  }

  if (!success)
    gtk_widget_error_bell (GTK_WIDGET (self));
}

static void
reorder_tab_cb (HdyTabBox        *self,
                GtkDirectionType  direction,
                gboolean          last)
{
  gboolean is_rtl, success = last;

  if (!self->view || !self->selected_tab || !self->selected_tab->page)
    return;

  is_rtl = gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL;

  if (direction == GTK_DIR_LEFT)
    direction = is_rtl ? GTK_DIR_TAB_FORWARD : GTK_DIR_TAB_BACKWARD;
  else if (direction == GTK_DIR_RIGHT)
    direction = is_rtl ? GTK_DIR_TAB_BACKWARD : GTK_DIR_TAB_FORWARD;

  if (direction == GTK_DIR_TAB_BACKWARD) {
    if (last)
      success = hdy_tab_view_reorder_first (self->view, self->selected_tab->page);
    else
      success = hdy_tab_view_reorder_backward (self->view, self->selected_tab->page);
  } else if (direction == GTK_DIR_TAB_FORWARD) {
    if (last)
      success = hdy_tab_view_reorder_last (self->view, self->selected_tab->page);
    else
      success = hdy_tab_view_reorder_forward (self->view, self->selected_tab->page);
  }

  if (!success)
    gtk_widget_error_bell (GTK_WIDGET (self));
}

/* Scrolling */

static void
update_needs_attention (HdyTabBox *self)
{
  gboolean left = FALSE, right = FALSE;
  GList *l;
  gdouble value, page_size;

  if (!self->adjustment)
    return;

  value = gtk_adjustment_get_value (self->adjustment);
  page_size = gtk_adjustment_get_page_size (self->adjustment);

  if (!self->adjustment)
      return;

  for (l = self->tabs; l; l = l->next) {
    TabInfo *info = l->data;
    gint pos;

    if (!info->page)
      continue;

    if (!hdy_tab_page_get_needs_attention (info->page))
      continue;

    pos = get_tab_position (self, info);

    if (pos + info->width / 2.0 <= value)
      left = TRUE;

    if (pos + info->width / 2.0 >= value + page_size)
      right = TRUE;
  }

  if (self->needs_attention_left != left) {
    self->needs_attention_left = left;
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_NEEDS_ATTENTION_LEFT]);
  }

  if (self->needs_attention_right != right) {
    self->needs_attention_right = right;
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_NEEDS_ATTENTION_RIGHT]);
  }
}

static gdouble
get_scroll_animation_value (HdyTabBox *self)
{
  gdouble to, value;

  g_assert (self->scroll_animation);

  to = self->scroll_animation_offset;

  if (self->scroll_animation_tab) {
    gdouble lower, upper, page_size;

    to += get_tab_position (self, self->scroll_animation_tab);

    g_object_get (self->adjustment,
                  "lower", &lower,
                  "upper", &upper,
                  "page-size", &page_size,
                  NULL);

    to = CLAMP (to, lower, upper - page_size);
  }

  value = hdy_animation_get_value (self->scroll_animation);

  return round (hdy_lerp (self->scroll_animation_from, to, value));
}

static void
adjustment_value_changed_cb (HdyTabBox *self)
{
  gdouble value = gtk_adjustment_get_value (self->adjustment);

  self->hover_x += (value - self->adjustment_prev_value);

  update_hover (self);
  update_needs_attention (self);

  self->adjustment_prev_value = value;

  if (self->block_scrolling)
      return;

  if (self->scroll_animation)
    hdy_animation_stop (self->scroll_animation);
}

static void
scroll_animation_value_cb (gdouble  value,
                           gpointer user_data)
{
  gtk_widget_queue_resize (GTK_WIDGET (user_data));
}

static void
scroll_animation_done_cb (gpointer user_data)
{
  HdyTabBox *self = HDY_TAB_BOX (user_data);

  self->scroll_animation_done = TRUE;
  gtk_widget_queue_resize (GTK_WIDGET (self));
}

static void
animate_scroll (HdyTabBox *self,
                TabInfo   *info,
                gdouble    offset,
                gint64     duration)
{
  if (!self->adjustment)
    return;

  g_signal_emit (self, signals[SIGNAL_STOP_KINETIC_SCROLLING], 0);

  if (self->scroll_animation)
    hdy_animation_stop (self->scroll_animation);

  g_clear_object (&self->scroll_animation);
  self->scroll_animation_done = FALSE;
  self->scroll_animation_from = gtk_adjustment_get_value (self->adjustment);
  self->scroll_animation_tab = info;
  self->scroll_animation_offset = offset;

  /* The actual update will be done in size_allocate (). After the animation
   * finishes, don't remove it right away, it will be done in size-allocate as
   * well after one last update, so that we don't miss the last frame.
   */

  self->scroll_animation =
    hdy_animation_new (GTK_WIDGET (self), 0, 1, duration,
                       scroll_animation_value_cb,
                       scroll_animation_done_cb,
                       self);

  hdy_animation_start (self->scroll_animation);
}

static void
animate_scroll_relative (HdyTabBox *self,
                         gdouble    delta,
                         gint64     duration)
{
  gdouble current_value = gtk_adjustment_get_value (self->adjustment);

  if (self->scroll_animation) {
    current_value = self->scroll_animation_offset;

    if (self->scroll_animation_tab)
      current_value += get_tab_position (self, self->scroll_animation_tab);
  }

  animate_scroll (self, NULL, current_value + delta, duration);
}

static void
scroll_to_tab_with_pos (HdyTabBox *self,
                        TabInfo   *info,
                        gint       pos,
                        gint64     duration)
{
  gint tab_width;
  gdouble padding, value, page_size;

  if (!self->adjustment)
    return;

  tab_width = info->width;

  if (info->appear_animation)
    tab_width = hdy_tab_get_display_width (info->tab);

  value = gtk_adjustment_get_value (self->adjustment);
  page_size = gtk_adjustment_get_page_size (self->adjustment);

  padding = MIN (info->width, page_size - tab_width) / 2.0;

  if (pos + OVERLAP < value)
    animate_scroll (self, info, -padding, duration);
  else if (pos + tab_width - OVERLAP > value + page_size)
    animate_scroll (self, info, tab_width + padding - page_size, duration);
}

static void
scroll_to_tab (HdyTabBox *self,
               TabInfo   *info,
               gint64     duration)
{
  scroll_to_tab_with_pos (self, info, get_tab_position (self, info), duration);
}

/* Reordering */

static void
force_end_reordering (HdyTabBox *self)
{
  GList *l;

  if (self->dragging || !self->reordered_tab)
    return;

  if (self->reorder_animation)
    hdy_animation_stop (self->reorder_animation);

  for (l = self->tabs; l; l = l->next) {
    TabInfo *info = l->data;

    if (info->reorder_animation)
      hdy_animation_stop (info->reorder_animation);
  }
}

static void
check_end_reordering (HdyTabBox *self)
{
  gboolean should_focus;
  GtkWidget *tab_widget;
  GList *l;

  if (self->dragging || !self->reordered_tab || self->continue_reorder)
    return;

  if (self->reorder_animation)
    return;

  for (l = self->tabs; l; l = l->next) {
    TabInfo *info = l->data;

    if (info->reorder_animation)
      return;
  }

  for (l = self->tabs; l; l = l->next) {
    TabInfo *info = l->data;

    info->end_reorder_offset = 0;
    info->reorder_offset = 0;
  }

  tab_widget = GTK_WIDGET (self->reordered_tab->tab);

  should_focus = gtk_widget_has_visible_focus (tab_widget);

  gtk_widget_set_child_visible (tab_widget, FALSE);
  gtk_widget_unrealize (tab_widget);
  gtk_widget_set_parent_window (tab_widget, self->window);
  gtk_widget_set_child_visible (tab_widget, TRUE);
  gtk_widget_set_has_tooltip (tab_widget, TRUE);

  self->reordered_tab->reorder_ignore_bounds = FALSE;

  if (should_focus)
    gtk_widget_grab_focus (tab_widget);

  gdk_window_hide (self->reorder_window);

  self->tabs = g_list_remove (self->tabs, self->reordered_tab);
  self->tabs = g_list_insert (self->tabs, self->reordered_tab, self->reorder_index);

  gtk_widget_queue_allocate (GTK_WIDGET (self));

  self->reordered_tab = NULL;
}

static void
start_reordering (HdyTabBox *self,
                  TabInfo   *info)
{
  gboolean should_focus;
  GtkWidget *tab_widget;

  self->reordered_tab = info;

  tab_widget = GTK_WIDGET (self->reordered_tab->tab);

  should_focus = gtk_widget_has_visible_focus (tab_widget);

  gtk_widget_set_has_tooltip (tab_widget, FALSE);
  gtk_widget_set_child_visible (tab_widget, FALSE);
  gtk_widget_unrealize (tab_widget);
  gtk_widget_set_parent_window (tab_widget, self->reorder_window);
  gtk_widget_set_child_visible (tab_widget, TRUE);

  if (should_focus)
    gtk_widget_grab_focus (tab_widget);

  gtk_widget_queue_allocate (GTK_WIDGET (self));
}

static gint
get_reorder_position (HdyTabBox *self)
{
  gint lower, upper;

  if (self->reordered_tab->reorder_ignore_bounds)
    return self->reorder_x;

  get_visible_range (self, &lower, &upper);

  return CLAMP (self->reorder_x, lower, upper - self->reordered_tab->width);
}

static void
reorder_animation_value_cb (gdouble  value,
                            gpointer user_data)
{
  TabInfo *dest_tab = user_data;
  GtkWidget *parent = gtk_widget_get_parent (GTK_WIDGET (dest_tab->tab));
  HdyTabBox *self = HDY_TAB_BOX (parent);
  gboolean is_rtl = gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL;
  gdouble x1, x2;

  x1 = get_reorder_position (self);
  x2 = dest_tab->pos - calculate_tab_offset (self, dest_tab);

  if (dest_tab->end_reorder_offset * (is_rtl ? 1 : -1) > 0)
    x2 += dest_tab->width - self->reordered_tab->width;

  self->reorder_window_x = (gint) round (hdy_lerp (x1, x2, value));

  gdk_window_move_resize (self->reorder_window,
                          self->reorder_window_x,
                          0,
                          self->reordered_tab->width,
                          gtk_widget_get_allocated_height (GTK_WIDGET (self)));

  update_hover (self);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
reorder_animation_done_cb (gpointer user_data)
{
  TabInfo *dest_tab = user_data;
  GtkWidget *parent = gtk_widget_get_parent (GTK_WIDGET (dest_tab->tab));
  HdyTabBox *self = HDY_TAB_BOX (parent);

  g_clear_object (&self->reorder_animation);
  check_end_reordering (self);
}

static void
animate_reordering (HdyTabBox *self,
                    TabInfo   *dest_tab)
{
  if (self->reorder_animation)
    hdy_animation_stop (self->reorder_animation);

  self->reorder_animation =
    hdy_animation_new (GTK_WIDGET (self), 0, 1,
                       REORDER_ANIMATION_DURATION,
                       reorder_animation_value_cb,
                       reorder_animation_done_cb,
                       dest_tab);

  hdy_animation_start (self->reorder_animation);

  check_end_reordering (self);
}

static void
reorder_offset_animation_value_cb (gdouble  value,
                                   gpointer user_data)
{
  TabInfo *info = user_data;
  GtkWidget *parent = gtk_widget_get_parent (GTK_WIDGET (info->tab));

  info->reorder_offset = value;
  gtk_widget_queue_allocate (parent);
}

static void
reorder_offset_animation_done_cb (gpointer user_data)
{
  TabInfo *info = user_data;
  GtkWidget *parent = gtk_widget_get_parent (GTK_WIDGET (info->tab));
  HdyTabBox *self = HDY_TAB_BOX (parent);

  g_clear_object (&info->reorder_animation);
  check_end_reordering (self);
}

static void
animate_reorder_offset (HdyTabBox *self,
                        TabInfo   *info,
                        gdouble    offset)
{
  gboolean is_rtl = gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL;

  offset *= (is_rtl ? -1 : 1);

  if (info->end_reorder_offset == offset)
    return;

  info->end_reorder_offset = offset;

  if (info->reorder_animation)
    hdy_animation_stop (info->reorder_animation);

  info->reorder_animation =
    hdy_animation_new (GTK_WIDGET (self), info->reorder_offset, offset,
                       REORDER_ANIMATION_DURATION,
                       reorder_offset_animation_value_cb,
                       reorder_offset_animation_done_cb,
                       info);

  hdy_animation_start (info->reorder_animation);
}

static void
reset_reorder_animations (HdyTabBox *self)
{
  gint i, original_index;
  GList *l;

  if (!hdy_get_enable_animations (GTK_WIDGET (self)))
      return;

  l = find_link_for_page (self, self->reordered_tab->page);
  original_index = g_list_position (self->tabs, l);

  if (self->reorder_index > original_index)
    for (i = 0; i < self->reorder_index - original_index; i++) {
      l = l->next;
      animate_reorder_offset (self, l->data, 0);
    }

  if (self->reorder_index < original_index)
    for (i = 0; i < original_index - self->reorder_index; i++) {
      l = l->prev;
      animate_reorder_offset (self, l->data, 0);
    }
}

static void
reorder_page (HdyTabBox  *self,
              HdyTabPage *page,
              gint        index)
{
  GList *link;
  gint original_index;
  TabInfo *info, *dest_tab;
  gboolean is_rtl;

  if (hdy_tab_page_get_pinned (page) != self->pinned)
    return;

  self->continue_reorder = self->reordered_tab && page == self->reordered_tab->page;

  if (self->continue_reorder)
    reset_reorder_animations (self);
  else
    force_end_reordering (self);

  link = find_link_for_page (self, page);
  info = link->data;
  original_index = g_list_position (self->tabs, link);

  if (!self->continue_reorder)
    start_reordering (self, info);

  gdk_window_show (self->reorder_window);

  if (self->continue_reorder)
    self->reorder_x = self->reorder_window_x;
  else
    self->reorder_x = info->pos;

  self->reorder_index = index;

  if (!self->pinned)
    self->reorder_index -= hdy_tab_view_get_n_pinned_pages (self->view);

  dest_tab = g_list_nth_data (self->tabs, self->reorder_index);

  if (info == self->selected_tab)
    scroll_to_tab_with_pos (self, self->selected_tab, dest_tab->pos, REORDER_ANIMATION_DURATION);

  animate_reordering (self, dest_tab);

  is_rtl = gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL;

  /* If animations are disabled, animate_reordering() animation will have
   * already finished and called check_end_reordering () by this point, so
   * it's too late to animate these, so we get a crash.
   */

  if (hdy_get_enable_animations (GTK_WIDGET (self)) &&
      gtk_widget_get_mapped (GTK_WIDGET (self))) {
    gint i;

    if (self->reorder_index > original_index)
      for (i = 0; i < self->reorder_index - original_index; i++) {
        link = link->next;
        animate_reorder_offset (self, link->data, is_rtl ? 1 : -1);
      }

    if (self->reorder_index < original_index)
      for (i = 0; i < original_index - self->reorder_index; i++) {
        link = link->prev;
        animate_reorder_offset (self, link->data, is_rtl ? -1 : 1);
      }
  }

  self->continue_reorder = FALSE;
}

static void
prepare_drag_window (GdkSeat   *seat,
                     GdkWindow *window,
                     gpointer   user_data)
{
  gdk_window_show (window);
}

static void
update_dragging (HdyTabBox *self)
{
  gboolean is_rtl, after_selected, found_index;
  gint x;
  gint i = 0;
  GList *l;

  if (!self->dragging)
    return;

  x = get_reorder_position (self);

  gdk_window_move_resize (self->reorder_window,
                          x, 0,
                          self->reordered_tab->width,
                          gtk_widget_get_allocated_height (GTK_WIDGET (self)));

  gtk_widget_queue_draw (GTK_WIDGET (self));

  is_rtl = gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL;
  after_selected = FALSE;
  found_index = FALSE;

  for (l = self->tabs; l; l = l->next) {
    TabInfo *info = l->data;
    gint center = info->pos - calculate_tab_offset (self, info) + info->width / 2;
    gdouble offset = 0;

    if (x + self->reordered_tab->width > center &&
        x < center &&
        (!found_index || after_selected)) {
      self->reorder_index = i;
      found_index = TRUE;
    }

    i++;

    if (info == self->reordered_tab) {
        after_selected = TRUE;
        continue;
    }

    if (after_selected != is_rtl && x + self->reordered_tab->width > center)
        offset = -1;
    else if (after_selected == is_rtl && x < center)
        offset = 1;

    animate_reorder_offset (self, info, offset);
  }
}

static gboolean
drag_autoscroll_cb (GtkWidget     *widget,
                    GdkFrameClock *frame_clock,
                    HdyTabBox     *self)
{
  gdouble value, lower, upper, page_size;
  gdouble x, delta_ms, start_threshold, end_threshold, autoscroll_factor;
  gint64 time;
  gint offset = 0;

  g_object_get (self->adjustment,
                "value", &value,
                "lower", &lower,
                "upper", &upper,
                "page-size", &page_size,
                NULL);

  x = CLAMP ((gdouble) self->reorder_x,
             lower + AUTOSCROLL_AREA_WIDTH,
             upper - self->reordered_tab->width - AUTOSCROLL_AREA_WIDTH);

  time = gdk_frame_clock_get_frame_time (frame_clock);
  delta_ms = (time - self->drag_autoscroll_prev_time) / 1000.0;

  start_threshold = value + AUTOSCROLL_AREA_WIDTH;
  end_threshold = value + page_size - self->reordered_tab->width - AUTOSCROLL_AREA_WIDTH;
  autoscroll_factor = 0;

  if (x < start_threshold)
    autoscroll_factor = -(start_threshold - x) / AUTOSCROLL_AREA_WIDTH;
  else if (x > end_threshold)
    autoscroll_factor = (x - end_threshold) / AUTOSCROLL_AREA_WIDTH;

  autoscroll_factor = CLAMP (autoscroll_factor, -1, 1);
  autoscroll_factor = hdy_ease_in_cubic (autoscroll_factor);
  self->drag_autoscroll_prev_time = time;

  if (autoscroll_factor == 0)
    return G_SOURCE_CONTINUE;

  if (autoscroll_factor > 0)
    offset = (gint) ceil (autoscroll_factor * delta_ms * AUTOSCROLL_SPEED);
  else
    offset = (gint) floor (autoscroll_factor * delta_ms * AUTOSCROLL_SPEED);

  self->reorder_x += offset;
  gtk_adjustment_set_value (self->adjustment, value + offset);
  update_dragging (self);

  return G_SOURCE_CONTINUE;
}

static void
start_dragging (HdyTabBox *self,
                GdkEvent  *event,
                TabInfo   *info)
{
  if (self->dragging)
    return;

  if (!info)
    return;

  self->continue_reorder = info == self->reordered_tab;

  if (self->continue_reorder) {
      if (self->reorder_animation)
        hdy_animation_stop (self->reorder_animation);

    reset_reorder_animations (self);

    self->reorder_x = (gint) round (self->hover_x - self->drag_offset_x);
    self->reorder_y = (gint) round (self->hover_y - self->drag_offset_y);
  } else
    force_end_reordering (self);

  if (self->adjustment) {
    GdkFrameClock *frame_clock = gtk_widget_get_frame_clock (GTK_WIDGET (self));

    self->drag_autoscroll_prev_time = gdk_frame_clock_get_frame_time (frame_clock);
    self->drag_autoscroll_cb_id =
      gtk_widget_add_tick_callback (GTK_WIDGET (self),
                                    (GtkTickCallback) drag_autoscroll_cb,
                                    self, NULL);
  }

  self->dragging = TRUE;

  if (!self->continue_reorder)
    start_reordering (self, info);

  if (!self->indirect_reordering) {
    GdkDevice *device = gdk_event_get_device (event);

    self->drag_seat = gdk_device_get_seat (device);
    gdk_seat_grab (self->drag_seat,
                   self->reorder_window,
                   GDK_SEAT_CAPABILITY_ALL,
                   FALSE,
                   NULL, // FIXME maybe use an actual cursor
                   event,
                   prepare_drag_window,
                   self);
  }
}

static void
end_dragging (HdyTabBox *self)
{
  TabInfo *dest_tab;

  if (!self->dragging)
    return;

  self->dragging = FALSE;

  if (self->drag_autoscroll_cb_id) {
    gtk_widget_remove_tick_callback (GTK_WIDGET (self),
                                     self->drag_autoscroll_cb_id);
    self->drag_autoscroll_cb_id = 0;
  }

  dest_tab = g_list_nth_data (self->tabs, self->reorder_index);

  if (!self->indirect_reordering) {
    gint index;

    gdk_seat_ungrab (self->drag_seat);
    self->drag_seat = NULL;

    index = self->reorder_index;

    if (!self->pinned)
      index += hdy_tab_view_get_n_pinned_pages (self->view);

    /* We've already reordered the tab here, no need to do it again */
    g_signal_handlers_block_by_func (self->view, reorder_page, self);

    hdy_tab_view_reorder_page (self->view, self->reordered_tab->page, index);

    g_signal_handlers_unblock_by_func (self->view, reorder_page, self);
  }

  animate_reordering (self, dest_tab);
}

/* Selection */

static void
select_page (HdyTabBox  *self,
             HdyTabPage *page)
{
  if (!page) {
    self->selected_tab = NULL;

    gtk_container_set_focus_child (GTK_CONTAINER (self), NULL);

    return;
  }

  self->selected_tab = find_info_for_page (self, page);

  if (!self->selected_tab)
    return;

  if (hdy_tab_bar_tabs_have_visible_focus (self->tab_bar))
    gtk_widget_grab_focus (GTK_WIDGET (self->selected_tab->tab));

  gtk_container_set_focus_child (GTK_CONTAINER (self),
                                 GTK_WIDGET (self->selected_tab->tab));

  if (self->selected_tab->width >= 0)
    scroll_to_tab (self, self->selected_tab, FOCUS_ANIMATION_DURATION);
}

/* Opening */

static void
appear_animation_value_cb (gdouble  value,
                           gpointer user_data)
{
  TabInfo *info = user_data;
  GtkWidget *parent;

  if (!GTK_IS_WIDGET (info->tab)) {
    info->appear_progress = value;
    return;
  }

  parent = gtk_widget_get_parent (GTK_WIDGET (info->tab));

  info->appear_progress = value;
  gtk_widget_queue_resize (parent);
}

static void
open_animation_done_cb (gpointer user_data)
{
  TabInfo *info = user_data;

  g_clear_object (&info->appear_animation);
}

static TabInfo *
create_tab_info (HdyTabBox  *self,
                 HdyTabPage *page)
{
  TabInfo *info;

  info = g_new0 (TabInfo, 1);
  info->page = page;
  info->pos = -1;
  info->width = -1;
  info->tab = hdy_tab_new (self->view, self->pinned, FALSE);

  hdy_tab_set_page (info->tab, page);

  gtk_widget_set_parent (GTK_WIDGET (info->tab), GTK_WIDGET (self));

  if (self->window)
    gtk_widget_set_parent_window (GTK_WIDGET (info->tab), self->window);

  return info;
}

static void
add_page (HdyTabBox  *self,
          HdyTabPage *page,
          gint        position)
{
  TabInfo *info;
  GList *l;

  if (hdy_tab_page_get_pinned (page) != self->pinned)
    return;

  if (!self->pinned)
    position -= hdy_tab_view_get_n_pinned_pages (self->view);

  force_end_reordering (self);

  info = create_tab_info (self, page);

  gtk_widget_show (GTK_WIDGET (info->tab));

  if (!self->pinned)
    info->notify_needs_attention_id =
      g_signal_connect_object (page,
                               "notify::needs-attention",
                               G_CALLBACK (update_needs_attention),
                               self,
                               G_CONNECT_SWAPPED);

  info->appear_animation =
    hdy_animation_new (GTK_WIDGET (self), 0, 1,
                       OPEN_ANIMATION_DURATION,
                       appear_animation_value_cb,
                       open_animation_done_cb,
                       info);

  l = find_nth_alive_tab (self, position);
  self->tabs = g_list_insert_before (self->tabs, l, info);

  self->n_tabs++;

  hdy_animation_start (info->appear_animation);

  if (page == hdy_tab_view_get_selected_page (self->view))
    hdy_tab_box_select_page (self, page);
}

/* Closing */

static void
close_animation_done_cb (gpointer user_data)
{
  TabInfo *info = user_data;
  GtkWidget *parent = gtk_widget_get_parent (GTK_WIDGET (info->tab));
  HdyTabBox *self = HDY_TAB_BOX (parent);

  g_clear_object (&info->appear_animation);

  self->tabs = g_list_remove (self->tabs, info);

  if (info == self->hovered_tab)
    self->hovered_tab = NULL;

  remove_and_free_tab_info (info);

  self->n_tabs--;
}

static void
remove_page (HdyTabBox  *self,
             HdyTabPage *page)
{
  TabInfo *info;
  GList *page_link;

  page_link = find_link_for_page (self, page);

  if (!page_link)
    return;

  info = page_link->data;
  page_link = page_link->next;

  force_end_reordering (self);

  if (self->hovering) {
    gboolean is_last = TRUE;

    while (page_link) {
      TabInfo *i = page_link->data;
      page_link = page_link->next;

      if (i->page) {
        is_last = FALSE;
        break;
      }
    }

    if (is_last && self->tab_resize_mode != TAB_RESIZE_NORMAL)
      set_tab_resize_mode (self, TAB_RESIZE_FIXED_END_PADDING);
    else if (!is_last)
      set_tab_resize_mode (self, TAB_RESIZE_FIXED_TAB_WIDTH);
  }

  g_assert (info->page);

  if (gtk_widget_is_focus (GTK_WIDGET (info->tab)))
    hdy_tab_box_try_focus_selected_tab (self);

  if (info == self->selected_tab)
    hdy_tab_box_select_page (self, NULL);

  hdy_tab_set_page (info->tab, NULL);

  if (info->notify_needs_attention_id > 0) {
    g_signal_handler_disconnect (info->page, info->notify_needs_attention_id);
    info->notify_needs_attention_id = 0;
  }

  info->page = NULL;

  if (info->appear_animation)
    hdy_animation_stop (info->appear_animation);

  info->appear_animation =
    hdy_animation_new (GTK_WIDGET (self), info->appear_progress, 0,
                       CLOSE_ANIMATION_DURATION,
                       appear_animation_value_cb,
                       close_animation_done_cb,
                       info);

  hdy_animation_start (info->appear_animation);
}

/* DND */

static gboolean
check_dnd_threshold (HdyTabBox *self)
{
  gint threshold;
  GtkAllocation alloc;

  g_object_get (gtk_settings_get_default (),
                "gtk-dnd-drag-threshold", &threshold,
                NULL);

  threshold *= DND_THRESHOLD_MULTIPLIER;

  gtk_widget_get_allocation (GTK_WIDGET (self), &alloc);

  alloc.x -= threshold;
  alloc.y -= threshold;
  alloc.width += 2 * threshold;
  alloc.height += 2 * threshold;

  return self->hover_x < alloc.x - threshold ||
         self->hover_y < alloc.y - threshold ||
         self->hover_x > alloc.x + alloc.width + threshold ||
         self->hover_y > alloc.x + alloc.height + threshold;
}

static gint
calculate_placeholder_index (HdyTabBox *self,
                             gint       x)
{
  gint lower, upper, pos, i;
  gboolean is_rtl;
  GList *l;

  get_visible_range (self, &lower, &upper);

  x = CLAMP (x, lower, upper);

  is_rtl = gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL;

  pos = (is_rtl ? self->allocated_width + OVERLAP : -OVERLAP);
  i = 0;

  for (l = self->tabs; l; l = l->next) {
    TabInfo *info = l->data;
    int tab_width = predict_tab_width (self, info, TRUE) * (is_rtl ? -1 : 1);

    int end = pos + tab_width + calculate_tab_offset (self, info);

    if ((x <= end && !is_rtl) || (x >= end && is_rtl))
      break;

    pos += tab_width + (is_rtl ? OVERLAP : -OVERLAP);
    i++;
  }

  return i;
}

static void
insert_placeholder (HdyTabBox  *self,
                    HdyTabPage *page,
                    gint        pos)
{
  gdouble initial_progress = 0;
  TabInfo *info = self->reorder_placeholder;

  if (info) {
    initial_progress = info->appear_progress;

    if (info->appear_animation)
      hdy_animation_stop (info->appear_animation);
  } else {
    gint index;

    self->placeholder_page = page;

    info = create_tab_info (self, page);

    info->reorder_ignore_bounds = TRUE;

    index = calculate_placeholder_index (self, pos);

    self->tabs = g_list_insert (self->tabs, info, index);
    self->n_tabs++;

    self->reorder_placeholder = info;
    self->reorder_index = g_list_index (self->tabs, info);
  }

  info->appear_animation =
    hdy_animation_new (GTK_WIDGET (self), initial_progress, 1,
                       OPEN_ANIMATION_DURATION,
                       appear_animation_value_cb,
                       open_animation_done_cb,
                       info);

  hdy_animation_start (info->appear_animation);
}

static void
replace_animation_done_cb (gpointer user_data)
{
  TabInfo *info = user_data;
  GtkWidget *parent = gtk_widget_get_parent (GTK_WIDGET (info->tab));
  HdyTabBox *self = HDY_TAB_BOX (parent);

  g_clear_object (&info->appear_animation);
  self->reorder_placeholder = NULL;
  self->can_remove_placeholder = TRUE;
}

static void
replace_placeholder (HdyTabBox  *self,
                     HdyTabPage *page)
{
  TabInfo *info = self->reorder_placeholder;
  gdouble initial_progress;

  gtk_widget_show (GTK_WIDGET (self->reorder_placeholder->tab));

  if (!info->appear_animation) {
    self->reorder_placeholder = NULL;

    return;
  }

  initial_progress = info->appear_progress;

  self->can_remove_placeholder = FALSE;

  hdy_tab_set_page (info->tab, page);
  info->page = page;

  hdy_animation_stop (info->appear_animation);

  info->appear_animation =
    hdy_animation_new (GTK_WIDGET (self), initial_progress, 1,
                       OPEN_ANIMATION_DURATION,
                       appear_animation_value_cb,
                       replace_animation_done_cb,
                       info);

  hdy_animation_start (info->appear_animation);
}

static void
remove_animation_done_cb (gpointer user_data)
{
  TabInfo *info = user_data;
  GtkWidget *parent = gtk_widget_get_parent (GTK_WIDGET (info->tab));
  HdyTabBox *self = HDY_TAB_BOX (parent);

  g_clear_object (&info->appear_animation);

  if (!self->can_remove_placeholder) {
    hdy_tab_set_page (info->tab, self->placeholder_page);
    info->page = self->placeholder_page;

    return;
  }

  if (self->reordered_tab == info) {
    force_end_reordering (self);

    if (self->reorder_animation)
      hdy_animation_stop (info->reorder_animation);

    self->reordered_tab = NULL;
  }

  self->tabs = g_list_remove (self->tabs, info);

  remove_and_free_tab_info (info);

  self->n_tabs--;

  self->reorder_placeholder = NULL;
}

static void
remove_placeholder (HdyTabBox *self)
{
  TabInfo *info = self->reorder_placeholder;

  if (!info || !info->page)
    return;

  hdy_tab_set_page (info->tab, NULL);
  info->page = NULL;

  if (info->appear_animation)
    hdy_animation_stop (info->appear_animation);

  info->appear_animation =
    hdy_animation_new (GTK_WIDGET (self), info->appear_progress, 0,
                       CLOSE_ANIMATION_DURATION,
                       appear_animation_value_cb,
                       remove_animation_done_cb,
                       info);

  hdy_animation_start (info->appear_animation);
}

static HdyTabBox *
get_source_tab_box (GdkDragContext *context)
{
  GtkWidget *source = gtk_drag_get_source_widget (context);

  if (!source || !HDY_IS_TAB_BOX (source))
    return NULL;

  return HDY_TAB_BOX (source);
}

static gboolean
do_drag_drop (HdyTabBox      *self,
              GdkDragContext *context,
              guint           time)
{
  GdkAtom target, tab_target;
  HdyTabBox *source_tab_box;
  HdyTabPage *page;
  gint offset;

  target = gtk_drag_dest_find_target (GTK_WIDGET (self), context, NULL);
  tab_target = gdk_atom_intern_static_string ("HDY_TAB");

  if (target != tab_target)
    return GDK_EVENT_PROPAGATE;

  source_tab_box = get_source_tab_box (context);

  if (!source_tab_box)
    return GDK_EVENT_PROPAGATE;

  page = source_tab_box->detached_page;
  offset = (self->pinned ? 0 : hdy_tab_view_get_n_pinned_pages (self->view));

  if (self->reorder_placeholder) {
    replace_placeholder (self, page);
    end_dragging (self);

    g_signal_handlers_block_by_func (self->view, add_page, self);

    hdy_tab_view_attach_page (self->view, page, self->reorder_index + offset);

    g_signal_handlers_unblock_by_func (self->view, add_page, self);
  } else {
    hdy_tab_view_attach_page (self->view, page, self->reorder_index + offset);
  }

  source_tab_box->detached_page = NULL;

  self->indirect_reordering = FALSE;
  gtk_drag_finish (context, TRUE, FALSE, time);

  return GDK_EVENT_STOP;
}

static void
detach_into_new_window (HdyTabBox      *self,
                        GdkDragContext *context)
{
  HdyTabBox *source_tab_box;
  HdyTabPage *page;
  HdyTabView *new_view;

  source_tab_box = get_source_tab_box (context);

  if (!source_tab_box)
    return;

  page = source_tab_box->detached_page;

  // FIXME
  g_signal_emit_by_name (source_tab_box->view, "create-window", &new_view);

  if (!HDY_IS_TAB_VIEW (new_view))
    g_error ("Cannot detach into new window");

  hdy_tab_view_attach_page (new_view, page, 0);

  self->should_detach_into_new_window = FALSE;
}

static gboolean
is_view_in_the_same_group (HdyTabBox  *self,
                           HdyTabView *other_view)
{
  GSList *group = hdy_tab_view_get_group (self->view);

  return g_slist_index (group, other_view) >= 0;
}

static gboolean
view_drag_drop_cb (HdyTabBox      *self,
                   GdkDragContext *context,
                   gint            x,
                   gint            y,
                   guint           time)
{
  HdyTabBox *source_tab_box;

  if (self->pinned)
    return GDK_EVENT_PROPAGATE;

  source_tab_box = get_source_tab_box (context);

  if (!source_tab_box)
    return GDK_EVENT_PROPAGATE;

  if (!self->view || !is_view_in_the_same_group (self, source_tab_box->view))
    return GDK_EVENT_PROPAGATE;

  self->reorder_index = hdy_tab_view_get_n_pages (self->view) -
                        hdy_tab_view_get_n_pinned_pages (self->view);

  return do_drag_drop (self, context, time);
}

static void
create_drag_icon (HdyTabBox      *self,
                  GdkDragContext *context)
{
  DragIcon *icon;

  icon = g_new0 (DragIcon, 1);

  icon->window = gtk_window_new (GTK_WINDOW_POPUP);
  icon->context = context;

  icon->width = predict_tab_width (self, self->reordered_tab, FALSE);
  icon->target_width = icon->width;

  gtk_widget_set_app_paintable (icon->window, TRUE);
  gtk_window_set_resizable (GTK_WINDOW (icon->window), FALSE);
  gtk_window_set_decorated (GTK_WINDOW (icon->window), FALSE);

  gtk_style_context_add_class (gtk_widget_get_style_context (icon->window),
                               "tab-drag-icon");

  icon->tab = hdy_tab_new (self->view, FALSE, TRUE);
  hdy_tab_set_page (icon->tab, self->reordered_tab->page);
  gtk_widget_show (GTK_WIDGET (icon->tab));
  gtk_widget_set_halign (GTK_WIDGET (icon->tab), GTK_ALIGN_START);

  gtk_container_add (GTK_CONTAINER (icon->window), GTK_WIDGET (icon->tab));

  gtk_style_context_get_margin (gtk_widget_get_style_context (GTK_WIDGET (icon->tab)),
                                gtk_widget_get_state_flags (GTK_WIDGET (icon->tab)),
                                &icon->tab_margin);

  hdy_tab_set_display_width (icon->tab, icon->width);
  gtk_widget_set_size_request (GTK_WIDGET (icon->tab),
                               icon->width + icon->tab_margin.left + icon->tab_margin.right,
                               -1);

  gtk_window_set_screen (GTK_WINDOW (icon->window),
                         gtk_widget_get_screen (GTK_WIDGET (self)));

  icon->hotspot_x = (gint) self->drag_offset_x;
  icon->hotspot_y = (gint) self->drag_offset_y;

  gtk_drag_set_icon_widget (context, icon->window,
                            icon->hotspot_x + icon->tab_margin.left,
                            icon->hotspot_y + icon->tab_margin.top);

  self->drag_icon = icon;
}

static void
icon_resize_animation_value_cb (gdouble  value,
                                gpointer user_data)
{
  DragIcon *icon = user_data;
  gdouble relative_pos;

  relative_pos = (gdouble) icon->hotspot_x / icon->width;

  icon->width = (gint) round (value);

  hdy_tab_set_display_width (icon->tab, icon->width);
  gtk_widget_set_size_request (GTK_WIDGET (icon->tab),
                               icon->width + icon->tab_margin.left + icon->tab_margin.right,
                               -1);

  icon->hotspot_x = (gint) round (icon->width * relative_pos);

  gdk_drag_context_set_hotspot (icon->context,
                                icon->hotspot_x + icon->tab_margin.left,
                                icon->hotspot_y + icon->tab_margin.top);

  gtk_widget_queue_resize (GTK_WIDGET (icon->window));
}

static void
icon_resize_animation_done_cb (gpointer user_data)
{
  DragIcon *icon = user_data;

  g_clear_object (&icon->resize_animation);
}

static void
resize_drag_icon (HdyTabBox *self,
                  gint       width)
{
  DragIcon *icon = self->drag_icon;

  if (width == icon->target_width)
    return;

  if (icon->resize_animation)
    hdy_animation_stop (icon->resize_animation);

  icon->target_width = width;

  icon->resize_animation =
    hdy_animation_new (icon->window, icon->width, width,
                       ICON_RESIZE_ANIMATION_DURATION,
                       icon_resize_animation_value_cb,
                       icon_resize_animation_done_cb,
                       icon);

  hdy_animation_start (icon->resize_animation);
}

/* Context menu */

static void
touch_menu_visible_cb (HdyTabBox *self)
{
  if (!self->touch_menu || gtk_widget_get_visible (GTK_WIDGET (self->touch_menu)))
    return;

  gtk_widget_destroy (GTK_WIDGET (self->touch_menu));
  self->touch_menu = NULL;

  g_signal_emit_by_name (self->view, "setup-menu", NULL);
}

static void
do_touch_popup (HdyTabBox *self,
                TabInfo   *info)
{
  GMenuModel *model = hdy_tab_view_get_menu_model (self->view);

  if (!G_IS_MENU_MODEL (model))
    return;

  // FIXME
  g_signal_emit_by_name (self->view, "setup-menu", info->page);

  if (!self->touch_menu) {
    self->touch_menu = GTK_POPOVER (gtk_popover_new_from_model (GTK_WIDGET (info->tab), model));

    g_signal_connect_object (self->touch_menu, "notify::visible",
                             G_CALLBACK (touch_menu_visible_cb), self,
                             G_CONNECT_SWAPPED);
  } else
    gtk_popover_set_relative_to (self->touch_menu, GTK_WIDGET (info->tab));

  gtk_popover_popup (self->touch_menu);
}

static void
touch_menu_gesture_pressed (HdyTabBox *self)
{
  self->pressed = FALSE;
  end_dragging (self);

  do_touch_popup (self, self->hovered_tab);
  gtk_gesture_set_state (self->touch_menu_gesture,
                         GTK_EVENT_SEQUENCE_CLAIMED);
}

static void
popup_menu_detach (HdyTabBox *self,
                   GtkMenu   *menu)
{
  self->context_menu = NULL;
}

static void
popup_menu_deactivate_cb (HdyTabBox *self)
{
  self->hovering = FALSE;
  update_hover (self);

  g_signal_emit_by_name (self->view, "setup-menu", NULL);
}

static void
do_popup (HdyTabBox *self,
          TabInfo   *info,
          GdkEvent  *event)
{
  GMenuModel *model = hdy_tab_view_get_menu_model (self->view);

  if (!G_IS_MENU_MODEL (model))
    return;

  // FIXME
  g_signal_emit_by_name (self->view, "setup-menu", info->page);

  if (!self->context_menu) {
    self->context_menu = GTK_MENU (gtk_menu_new_from_model (model));
    gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (self->context_menu)),
                                 GTK_STYLE_CLASS_CONTEXT_MENU);

    g_signal_connect_object (self->context_menu,
                             "deactivate",
                             G_CALLBACK (popup_menu_deactivate_cb),
                             self,
                             G_CONNECT_SWAPPED);

    gtk_menu_attach_to_widget (self->context_menu, GTK_WIDGET (self),
                               (GtkMenuDetachFunc) popup_menu_detach);
  }

  if (event && gdk_event_triggers_context_menu (event))
    gtk_menu_popup_at_pointer (self->context_menu, event);
  else {
    gtk_menu_popup_at_widget (self->context_menu,
                              hdy_tab_get_child (info->tab),
                              GDK_GRAVITY_SOUTH_WEST,
                              GDK_GRAVITY_NORTH_WEST,
                              event);

    gtk_menu_shell_select_first (GTK_MENU_SHELL (self->context_menu), FALSE);
  }
}

/* Overrides */

static void
hdy_tab_box_measure (GtkWidget      *widget,
                     GtkOrientation  orientation,
                     gint            for_size,
                     gint           *minimum,
                     gint           *natural,
                     gint           *minimum_baseline,
                     gint           *natural_baseline)
{
  HdyTabBox *self = HDY_TAB_BOX (widget);
  gint min, nat;

  if (self->n_tabs == 0) {
    if (minimum)
      *minimum = 0;

    if (natural)
      *natural = 0;

    if (minimum_baseline)
      *minimum_baseline = -1;

    if (natural_baseline)
      *natural_baseline = -1;

    return;
  }

  if (orientation == GTK_ORIENTATION_HORIZONTAL) {
    gint width = self->end_padding - OVERLAP;
    GList *l;

    for (l = self->tabs; l; l = l->next) {
      TabInfo *info = l->data;

      gint child_width = hdy_tab_get_child_min_width (info->tab);

      width += calculate_tab_width (info, child_width) - OVERLAP;
    }

    min = nat = MAX (self->last_width, width);
  } else {
    GList *l;

    min = nat = 0;

    for (l = self->tabs; l; l = l->next) {
      TabInfo *info = l->data;
      gint child_min, child_nat;

      gtk_widget_get_preferred_height (GTK_WIDGET (info->tab),
                                       &child_min,
                                       &child_nat);

      if (child_min > min)
        min = child_min;

      if (child_nat > nat)
        nat = child_nat;
    }
  }

  hdy_css_measure (widget, orientation, &min, &nat);

  if (minimum)
    *minimum = min;

  if (natural)
    *natural = nat;

  if (minimum_baseline)
    *minimum_baseline = -1;

  if (natural_baseline)
    *natural_baseline = -1;
}

static void
hdy_tab_box_get_preferred_width (GtkWidget *widget,
                                 gint      *minimum,
                                 gint      *natural)
{
  hdy_tab_box_measure (widget, GTK_ORIENTATION_HORIZONTAL, -1,
                       minimum, natural,
                       NULL, NULL);
}

static void
hdy_tab_box_get_preferred_height (GtkWidget *widget,
                                  gint      *minimum,
                                  gint      *natural)
{
  hdy_tab_box_measure (widget, GTK_ORIENTATION_VERTICAL, -1,
                       minimum, natural,
                       NULL, NULL);
}

static void
hdy_tab_box_get_preferred_width_for_height (GtkWidget *widget,
                                            gint       height,
                                            gint      *minimum,
                                            gint      *natural)
{
  hdy_tab_box_measure (widget, GTK_ORIENTATION_HORIZONTAL, height,
                       minimum, natural,
                       NULL, NULL);
}

static void
hdy_tab_box_get_preferred_height_for_width (GtkWidget *widget,
                                            gint       width,
                                            gint      *minimum,
                                            gint      *natural)
{
  hdy_tab_box_measure (widget, GTK_ORIENTATION_VERTICAL, width,
                       minimum, natural,
                       NULL, NULL);
}

static void
hdy_tab_box_size_allocate (GtkWidget     *widget,
                           GtkAllocation *allocation)
{
  HdyTabBox *self = HDY_TAB_BOX (widget);
  gboolean is_rtl, should_scroll_to_selected;
  GList *l;
  GtkAllocation child_allocation;
  gint pos;

  is_rtl = gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL;
  should_scroll_to_selected = self->selected_tab && self->selected_tab->width < 0;

  hdy_css_size_allocate_self (widget, allocation);

  GTK_WIDGET_CLASS (hdy_tab_box_parent_class)->size_allocate (widget, allocation);

  if (gtk_widget_get_realized (widget))
    gdk_window_move_resize (self->window,
                            allocation->x, allocation->y,
                            allocation->width, allocation->height);

  allocation->x = 0;
  allocation->y = 0;
  hdy_css_size_allocate_children (widget, allocation);

  self->allocated_width = allocation->width;

  if (!self->n_tabs)
      return;

  if (self->pinned) {
    for (l = self->tabs; l; l = l->next) {
      TabInfo *info = l->data;
      gdouble min_width = hdy_tab_get_child_min_width (info->tab);

      info->width = calculate_tab_width (info, min_width);
    }
  } else if (self->tab_resize_mode == TAB_RESIZE_FIXED_TAB_WIDTH) {
    self->end_padding = allocation->width + OVERLAP;

    for (l = self->tabs; l; l = l->next) {
      TabInfo *info = l->data;

      info->width = calculate_tab_width (info, info->last_width);
      self->end_padding -= info->width - OVERLAP;
    }
  } else {
    gint tab_width = get_base_tab_width (self);
    gint excess = allocation->width + OVERLAP - self->end_padding;

    for (l = self->tabs; l; l = l->next) {
      TabInfo *info = l->data;

      info->width = calculate_tab_width (info, tab_width);
      excess -= info->width - OVERLAP;
    }

    /* Now spread excess width across the tabs */
    for (l = self->tabs; l; l = l->next) {
      TabInfo *info = l->data;

      if (excess <= 0)
          break;

      info->width++;
      excess--;
    }
  }

  pos = allocation->x + (is_rtl ? allocation->width + OVERLAP : -OVERLAP);

  for (l = self->tabs; l; l = l->next) {
    TabInfo *info = l->data;

    if (!info->appear_animation)
      hdy_tab_set_display_width (info->tab, info->width);
    else if (info->page)
      hdy_tab_set_display_width (info->tab, predict_tab_width (self, info, FALSE));

    info->pos = pos + calculate_tab_offset (self, info);

    if (is_rtl)
      info->pos -= info->width;

    child_allocation.x = (info == self->reordered_tab) ? 0 : info->pos;
    child_allocation.y = allocation->y;
    child_allocation.width = info->width;
    child_allocation.height = allocation->height;

    gtk_widget_size_allocate (GTK_WIDGET (info->tab), &child_allocation);

    pos += (is_rtl ? -1 : 1) * (info->width - OVERLAP);
  }

  if (should_scroll_to_selected)
      scroll_to_tab (self, self->selected_tab, FOCUS_ANIMATION_DURATION);

  if (self->scroll_animation) {
    hdy_tab_box_set_block_scrolling (self, TRUE);
    gtk_adjustment_set_value (self->adjustment,
                              get_scroll_animation_value (self));
    hdy_tab_box_set_block_scrolling (self, FALSE);

    if (self->scroll_animation_done) {
        self->scroll_animation_done = FALSE;
        self->scroll_animation_tab = NULL;
        g_clear_object (&self->scroll_animation);
    }
  }

  update_hover (self);
  update_needs_attention (self);
}

static gboolean
hdy_tab_box_focus (GtkWidget        *widget,
                   GtkDirectionType  direction)
{
  HdyTabBox *self = HDY_TAB_BOX (widget);

  if (!self->selected_tab)
    return GDK_EVENT_PROPAGATE;

  return gtk_widget_child_focus (GTK_WIDGET (self->selected_tab->tab), direction);
}

static void
hdy_tab_box_realize (GtkWidget *widget)
{
  HdyTabBox *self = HDY_TAB_BOX (widget);
  GtkAllocation allocation;
  GdkWindowAttr attributes;
  GdkWindowAttributesType attributes_mask;
  GList *l;

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

  self->reorder_window = gdk_window_new (self->window, &attributes, attributes_mask);
  gtk_widget_register_window (widget, self->reorder_window);

  for (l = self->tabs; l; l = l->next) {
    TabInfo *info = l->data;

    gtk_widget_set_parent_window (GTK_WIDGET (info->tab), self->window);
  }
}

static void
hdy_tab_box_unrealize (GtkWidget *widget)
{
  HdyTabBox *self = HDY_TAB_BOX (widget);

  self->window = NULL;

  if (self->reorder_window) {
    gtk_widget_unregister_window (widget, self->reorder_window);
    gdk_window_destroy (self->reorder_window);
    self->reorder_window = NULL;
  }

  if (self->context_menu) {
    gtk_widget_destroy (GTK_WIDGET (self->context_menu));
    self->context_menu = NULL;
  }

  if (self->touch_menu) {
    gtk_widget_destroy (GTK_WIDGET (self->touch_menu));
    self->touch_menu = NULL;
  }

  GTK_WIDGET_CLASS (hdy_tab_box_parent_class)->unrealize (widget);
}

static void
hdy_tab_box_map (GtkWidget *widget)
{
  HdyTabBox *self = HDY_TAB_BOX (widget);

  GTK_WIDGET_CLASS (hdy_tab_box_parent_class)->map (widget);

  gdk_window_show_unraised (self->window);

  if (self->reordered_tab)
    gdk_window_show (self->reorder_window);
}

static void
hdy_tab_box_unmap (GtkWidget *widget)
{
  HdyTabBox *self = HDY_TAB_BOX (widget);

  force_end_reordering (self);

  if (self->drag_autoscroll_cb_id) {
    gtk_widget_remove_tick_callback (widget, self->drag_autoscroll_cb_id);
    self->drag_autoscroll_cb_id = 0;
  }

  if (self->reordered_tab)
    gdk_window_hide (self->reorder_window);

  self->hovering = FALSE;
  update_hover (self);

  gdk_window_hide (self->window);

  GTK_WIDGET_CLASS (hdy_tab_box_parent_class)->unmap (widget);
}

static void
hdy_tab_box_direction_changed (GtkWidget        *widget,
                               GtkTextDirection  previous_direction)
{
  HdyTabBox *self = HDY_TAB_BOX (widget);
  gdouble upper, page_size;

  if (!self->adjustment)
      return;

  if (gtk_widget_get_direction (widget) == previous_direction)
      return;

  upper = gtk_adjustment_get_value (self->adjustment);
  page_size = gtk_adjustment_get_page_size (self->adjustment);

  gtk_adjustment_set_value (self->adjustment,
                            upper - page_size - self->adjustment_prev_value);
}

static gboolean
hdy_tab_box_draw (GtkWidget *widget,
                  cairo_t   *cr)
{
  HdyTabBox *self = HDY_TAB_BOX (widget);

  if (!self->n_tabs)
    return GDK_EVENT_PROPAGATE;

  hdy_css_draw (widget, cr);

  return GTK_WIDGET_CLASS (hdy_tab_box_parent_class)->draw (widget, cr);
}

static gboolean
hdy_tab_box_popup_menu (GtkWidget *widget)
{
  HdyTabBox *self = HDY_TAB_BOX (widget);

  if (self->selected_tab && self->selected_tab->page) {
    do_popup (self, self->selected_tab, NULL);

    return GDK_EVENT_STOP;
  }

  return GDK_EVENT_PROPAGATE;
}

static gboolean
hdy_tab_box_enter_notify_event (GtkWidget        *widget,
                                GdkEventCrossing *event)
{
  HdyTabBox *self = HDY_TAB_BOX (widget);

  if (event->window != self->window || event->detail == GDK_NOTIFY_INFERIOR)
    return GDK_EVENT_PROPAGATE;

  self->hovering = TRUE;

  get_widget_coordinates (self, (GdkEvent *) event, &self->hover_x, &self->hover_y);
  update_hover (self);

  return GDK_EVENT_PROPAGATE;
}

static gboolean
hdy_tab_box_leave_notify_event (GtkWidget        *widget,
                                GdkEventCrossing *event)
{
  HdyTabBox *self = HDY_TAB_BOX (widget);

  if (event->window != self->window || event->detail == GDK_NOTIFY_INFERIOR)
    return GDK_EVENT_PROPAGATE;

  self->hovering = FALSE;
  update_hover (self);

  return GDK_EVENT_PROPAGATE;
}

static gboolean
hdy_tab_box_motion_notify_event (GtkWidget      *widget,
                                 GdkEventMotion *event)
{
  HdyTabBox *self = HDY_TAB_BOX (widget);

  self->hovering = TRUE;

  get_widget_coordinates (self, (GdkEvent *) event, &self->hover_x, &self->hover_y);

  update_hover (self);

  if (!self->pressed)
    return GDK_EVENT_PROPAGATE;

  if (self->hovered_tab &&
      gtk_drag_check_threshold (widget,
                                (gint) self->drag_begin_x,
                                (gint) self->drag_begin_y,
                                (gint) self->hover_x,
                                (gint) self->hover_y))
    start_dragging (self, (GdkEvent *) event, self->hovered_tab);

  if (!self->dragging)
    return GDK_EVENT_PROPAGATE;

  self->reorder_x = (gint) round (self->hover_x - self->drag_offset_x);
  self->reorder_y = (gint) round (self->hover_y - self->drag_offset_y);

  if (!self->pinned &&
      self->hovered_tab &&
      self->hovered_tab != self->reorder_placeholder &&
      self->hovered_tab->page &&
      hdy_tab_view_get_n_pages (self->view) > 1 &&
      check_dnd_threshold (self)) {
    gtk_drag_begin_with_coordinates (widget,
                                     self->source_targets,
                                     GDK_ACTION_MOVE,
                                     (gint) self->pressed_button,
                                     (GdkEvent *) event,
                                     self->reorder_x,
                                     self->reorder_y);

    return GDK_EVENT_STOP;
  }

  update_dragging (self);

  return GDK_EVENT_STOP;
}

static gboolean
hdy_tab_box_button_press_event (GtkWidget      *widget,
                                GdkEventButton *event)
{
  HdyTabBox *self = HDY_TAB_BOX (widget);
  gboolean can_grab_focus;

  get_widget_coordinates (self, (GdkEvent *) event, &self->hover_x, &self->hover_y);

  update_hover (self);

  if (!self->hovered_tab || !self->hovered_tab->page)
      return GDK_EVENT_PROPAGATE;

  if (gdk_event_triggers_context_menu ((GdkEvent *) event)) {
    do_popup (self, self->hovered_tab, (GdkEvent *) event);

    return GDK_EVENT_STOP;
  }

  self->pressed_button = event->button;

  if (self->pressed_button == GDK_BUTTON_MIDDLE && !self->pinned) {
    hdy_tab_view_close_page (self->view, self->hovered_tab->page);

    return GDK_EVENT_STOP;
  }

  if (self->pressed_button != GDK_BUTTON_PRIMARY)
    return GDK_EVENT_PROPAGATE;

  if (self->adjustment) {
    gint pos = get_tab_position (self, self->hovered_tab);
    gdouble value = gtk_adjustment_get_value (self->adjustment);
    gdouble page_size = gtk_adjustment_get_page_size (self->adjustment);

    if (pos + OVERLAP < value ||
        pos + self->hovered_tab->width - OVERLAP > value + page_size) {
      scroll_to_tab (self, self->hovered_tab, SCROLL_ANIMATION_DURATION);

      return GDK_EVENT_STOP;
    }
  }

  can_grab_focus = hdy_tab_bar_tabs_have_visible_focus (self->tab_bar);

  if (self->hovered_tab == self->selected_tab)
    can_grab_focus = TRUE;
  else
    hdy_tab_view_set_selected_page (self->view, self->hovered_tab->page);

  if (can_grab_focus)
    gtk_widget_grab_focus (GTK_WIDGET (self->hovered_tab->tab));
  else
    activate_tab (self);

  self->pressed = TRUE;
  self->drag_begin_x = self->hover_x;
  self->drag_begin_y = self->hover_y;
  self->drag_offset_x = self->drag_begin_x - get_tab_position (self, self->hovered_tab);
  self->drag_offset_y = self->drag_begin_y;

  if (!self->reorder_animation) {
    self->reorder_x = (gint) round (self->hover_x - self->drag_offset_x);
    self->reorder_y = (gint) round (self->hover_y - self->drag_offset_y);
  }

  return GDK_EVENT_STOP;
}

static gboolean
hdy_tab_box_button_release_event (GtkWidget      *widget,
                                  GdkEventButton *event)
{
  HdyTabBox *self = HDY_TAB_BOX (widget);

  self->pressed = FALSE;
  self->pressed_button = 0;

  end_dragging (self);

  return GDK_EVENT_PROPAGATE;
}

static gboolean
hdy_tab_box_scroll_event (GtkWidget      *widget,
                          GdkEventScroll *event)
{
  HdyTabBox *self = HDY_TAB_BOX (widget);
  gdouble page_size, pow_unit, scroll_unit;
  GdkDevice *source_device;
  GdkInputSource input_source;
  gdouble dx, dy;

  if (!self->adjustment)
    return GDK_EVENT_PROPAGATE;

  source_device = gdk_event_get_source_device ((GdkEvent *) event);
  input_source = gdk_device_get_source (source_device);

  if (input_source != GDK_SOURCE_MOUSE)
    return GDK_EVENT_PROPAGATE;

  if (!gdk_event_get_scroll_deltas ((GdkEvent *) event, &dx, &dy)) {
    switch (event->direction) {
    case GDK_SCROLL_UP:
      dy = -1;
      break;

    case GDK_SCROLL_DOWN:
      dy = 1;
      break;

    case GDK_SCROLL_LEFT:
      dx = -1;
      break;

    case GDK_SCROLL_RIGHT:
      dx = 1;
      break;

    case GDK_SCROLL_SMOOTH:
    default:
      g_assert_not_reached ();
    }
  }

  if (dx != 0)
    return GDK_EVENT_PROPAGATE;

  page_size = gtk_adjustment_get_page_size (self->adjustment);

  /* Copied from gtkrange.c, _gtk_range_get_wheel_delta() */
  pow_unit = pow (page_size, 2.0 / 3.0);
  scroll_unit = MIN (pow_unit, page_size / 2.0);

  if (gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL)
    dy = -dy;

  animate_scroll_relative (self, dy * scroll_unit, SCROLL_ANIMATION_DURATION);

  return GDK_EVENT_PROPAGATE;
}

static void
hdy_tab_box_drag_begin (GtkWidget      *widget,
                        GdkDragContext *context)
{
  HdyTabBox *self = HDY_TAB_BOX (widget);
  TabInfo *detached_tab;

  if (self->pinned)
    return;

  create_drag_icon (self, context);

  self->hovering = TRUE;
  self->pressed = FALSE;
  self->pressed_button = 0;

  detached_tab = self->reordered_tab;
  self->detached_page = detached_tab->page;

  self->indirect_reordering = TRUE;

  end_dragging (self);
  update_hover (self);

  gtk_widget_hide (GTK_WIDGET (detached_tab->tab));
  self->detached_index = hdy_tab_view_get_page_position (self->view, detached_tab->page);

  hdy_tab_view_start_drag (self->view);
  hdy_tab_view_detach_page (self->view, self->detached_page);

  self->indirect_reordering = FALSE;
}

static void
hdy_tab_box_drag_end (GtkWidget      *widget,
                      GdkDragContext *context)
{
  HdyTabBox *self = HDY_TAB_BOX (widget);

  if (self->pinned)
    return;

  if (self->should_detach_into_new_window)
    detach_into_new_window (self, context);

  self->detached_page = NULL;

  gtk_widget_destroy (self->drag_icon->window);
  g_clear_pointer (&self->drag_icon, g_free);

  hdy_tab_view_end_drag (self->view);
}

static gboolean
hdy_tab_box_drag_motion (GtkWidget      *widget,
                         GdkDragContext *context,
                         gint            x,
                         gint            y,
                         guint           time)
{
  HdyTabBox *self = HDY_TAB_BOX (widget);
  HdyTabBox *source_tab_box;
  GdkAtom target, tab_target;
  gdouble center, display_width;

  if (self->pinned)
    return GDK_EVENT_PROPAGATE;

  target = gtk_drag_dest_find_target (GTK_WIDGET (self), context, NULL);
  tab_target = gdk_atom_intern_static_string ("HDY_TAB");

  if (target != tab_target)
    return GDK_EVENT_PROPAGATE;

  source_tab_box = get_source_tab_box (context);

  if (!source_tab_box)
    return GDK_EVENT_PROPAGATE;

  if (!self->view || !is_view_in_the_same_group (self, source_tab_box->view))
    return GDK_EVENT_PROPAGATE;

  center = x - source_tab_box->drag_icon->hotspot_x + source_tab_box->drag_icon->width / 2;

  self->can_remove_placeholder = FALSE;

  if (!self->reorder_placeholder || !self->reorder_placeholder->page) {
    HdyTabPage *page = source_tab_box->detached_page;

    insert_placeholder (self, page, center);

    self->indirect_reordering = TRUE;

    self->drag_offset_x = source_tab_box->drag_icon->hotspot_x;
    self->drag_offset_y = source_tab_box->drag_icon->hotspot_y;

    display_width = hdy_tab_get_display_width (self->reorder_placeholder->tab);
    self->reorder_x = (gint) round (center - display_width / 2);

    start_dragging (self, gtk_get_current_event (), self->reorder_placeholder);

    resize_drag_icon (source_tab_box, predict_tab_width (self, self->reorder_placeholder, TRUE));

    gdk_drag_status (context, GDK_ACTION_MOVE, time);

    return GDK_EVENT_PROPAGATE;
  }

  display_width = hdy_tab_get_display_width (self->reorder_placeholder->tab);
  self->reorder_x = (gint) round (center - display_width / 2);

  update_dragging (self);

  gdk_drag_status (context, GDK_ACTION_MOVE, time);

  return GDK_EVENT_PROPAGATE;
}

static void
hdy_tab_box_drag_leave (GtkWidget      *widget,
                        GdkDragContext *context,
                        guint           time)
{
  HdyTabBox *self = HDY_TAB_BOX (widget);
  HdyTabBox *source_tab_box;
  GdkAtom target, tab_target;

  if (self->pinned)
    return;

  source_tab_box = get_source_tab_box (context);

  if (!source_tab_box)
    return;

  if (!self->view || !is_view_in_the_same_group (self, source_tab_box->view))
    return;

  target = gtk_drag_dest_find_target (GTK_WIDGET (self), context, NULL);
  tab_target = gdk_atom_intern_static_string ("HDY_TAB");

  if (target != tab_target)
    return;

  self->can_remove_placeholder = TRUE;

  end_dragging (self);
  remove_placeholder (self);

  self->indirect_reordering = FALSE;
}

static gboolean
hdy_tab_box_drag_drop (GtkWidget      *widget,
                       GdkDragContext *context,
                       gint            x,
                       gint            y,
                       guint           time)
{
  HdyTabBox *self = HDY_TAB_BOX (widget);
  HdyTabBox *source_tab_box;

  if (self->pinned)
    return GDK_EVENT_PROPAGATE;

  source_tab_box = get_source_tab_box (context);

  if (!source_tab_box)
    return GDK_EVENT_PROPAGATE;

  if (!self->view || !is_view_in_the_same_group (self, source_tab_box->view))
    return GDK_EVENT_PROPAGATE;

  return do_drag_drop (self, context, time);
}

static gboolean
hdy_tab_box_drag_failed (GtkWidget *widget,
                         GdkDragContext *context,
                         GtkDragResult result)
{
  HdyTabBox *self = HDY_TAB_BOX (widget);

  self->should_detach_into_new_window = FALSE;

  if (result == GTK_DRAG_RESULT_NO_TARGET) {
    detach_into_new_window (self, context);

    return GDK_EVENT_STOP;
  }

  hdy_tab_view_attach_page (self->view,
                            self->detached_page,
                            self->detached_index);

  self->indirect_reordering = FALSE;

  return GDK_EVENT_STOP;
}

static void
hdy_tab_box_drag_data_get (GtkWidget        *widget,
                           GdkDragContext   *context,
                           GtkSelectionData *data,
                           guint             info,
                           guint             time)
{
  HdyTabBox *self = HDY_TAB_BOX (widget);
  GdkAtom target, rootwindow_target;

  target = gtk_selection_data_get_target (data);
  rootwindow_target = gdk_atom_intern_static_string ("application/x-rootwindow-drop");

  if (target == rootwindow_target) {
    self->should_detach_into_new_window = TRUE;
    gtk_selection_data_set (data, target, 8, NULL, 0);
  }
}

static void
hdy_tab_box_forall (GtkContainer *container,
                    gboolean      include_internals,
                    GtkCallback   callback,
                    gpointer      callback_data)
{
  HdyTabBox *self = HDY_TAB_BOX (container);
  GList *l;

  if (!include_internals)
    return;

  for (l = self->tabs; l; l = l->next) {
    TabInfo *info = l->data;

    callback (GTK_WIDGET (info->tab), callback_data);
  }
}

static void
hdy_tab_box_dispose (GObject *object)
{
  HdyTabBox *self = HDY_TAB_BOX (object);

  self->tab_bar = NULL;
  hdy_tab_box_set_view (self, NULL);
  hdy_tab_box_set_adjustment (self, NULL);

  G_OBJECT_CLASS (hdy_tab_box_parent_class)->dispose (object);
}

static void
hdy_tab_box_finalize (GObject *object)
{
  HdyTabBox *self = (HdyTabBox *) object;

  g_clear_object (&self->touch_menu_gesture);
  g_clear_pointer (&self->source_targets, gtk_target_list_unref);

  G_OBJECT_CLASS (hdy_tab_box_parent_class)->finalize (object);
}

static void
hdy_tab_box_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  HdyTabBox *self = HDY_TAB_BOX (object);

  switch (prop_id) {
  case PROP_PINNED:
    g_value_set_boolean (value, self->pinned);
    break;

  case PROP_TAB_BAR:
    g_value_set_object (value, self->tab_bar);
    break;

  case PROP_VIEW:
    g_value_set_object (value, self->view);
    break;

  case PROP_ADJUSTMENT:
    g_value_set_object (value, self->adjustment);
    break;

  case PROP_NEEDS_ATTENTION_LEFT:
    g_value_set_boolean (value, self->needs_attention_left);
    break;

  case PROP_NEEDS_ATTENTION_RIGHT:
    g_value_set_boolean (value, self->needs_attention_right);
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
hdy_tab_box_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  HdyTabBox *self = HDY_TAB_BOX (object);

  switch (prop_id) {
  case PROP_PINNED:
    self->pinned = g_value_get_boolean (value);
    break;

  case PROP_TAB_BAR:
    self->tab_bar = g_value_get_object (value);
    break;

  case PROP_VIEW:
    hdy_tab_box_set_view (self, g_value_get_object (value));
    break;

  case PROP_ADJUSTMENT:
    hdy_tab_box_set_adjustment (self, g_value_get_object (value));
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
hdy_tab_box_class_init (HdyTabBoxClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GtkContainerClass *container_class = GTK_CONTAINER_CLASS (klass);
  GtkBindingSet *binding_set;

  object_class->dispose = hdy_tab_box_dispose;
  object_class->finalize = hdy_tab_box_finalize;
  object_class->get_property = hdy_tab_box_get_property;
  object_class->set_property = hdy_tab_box_set_property;

  widget_class->get_preferred_width = hdy_tab_box_get_preferred_width;
  widget_class->get_preferred_height = hdy_tab_box_get_preferred_height;
  widget_class->get_preferred_width_for_height = hdy_tab_box_get_preferred_width_for_height;
  widget_class->get_preferred_height_for_width = hdy_tab_box_get_preferred_height_for_width;
  widget_class->size_allocate = hdy_tab_box_size_allocate;
  widget_class->focus = hdy_tab_box_focus;
  widget_class->realize = hdy_tab_box_realize;
  widget_class->unrealize = hdy_tab_box_unrealize;
  widget_class->map = hdy_tab_box_map;
  widget_class->unmap = hdy_tab_box_unmap;
  widget_class->direction_changed = hdy_tab_box_direction_changed;
  widget_class->draw = hdy_tab_box_draw;
  widget_class->popup_menu = hdy_tab_box_popup_menu;
  widget_class->enter_notify_event = hdy_tab_box_enter_notify_event;
  widget_class->leave_notify_event = hdy_tab_box_leave_notify_event;
  widget_class->motion_notify_event = hdy_tab_box_motion_notify_event;
  widget_class->button_press_event = hdy_tab_box_button_press_event;
  widget_class->button_release_event = hdy_tab_box_button_release_event;
  widget_class->scroll_event = hdy_tab_box_scroll_event;
  widget_class->drag_begin = hdy_tab_box_drag_begin;
  widget_class->drag_end = hdy_tab_box_drag_end;
  widget_class->drag_motion = hdy_tab_box_drag_motion;
  widget_class->drag_leave = hdy_tab_box_drag_leave;
  widget_class->drag_drop = hdy_tab_box_drag_drop;
  widget_class->drag_failed = hdy_tab_box_drag_failed;
  widget_class->drag_data_get = hdy_tab_box_drag_data_get;

  container_class->forall = hdy_tab_box_forall;

  props[PROP_PINNED] =
    g_param_spec_boolean ("pinned",
                          _("Pinned"),
                          _("Pinned"),
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  props[PROP_TAB_BAR] =
    g_param_spec_object ("tab-bar",
                         _("Tab Bar"),
                         _("Tab Bar"),
                         HDY_TYPE_TAB_BAR,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  props[PROP_VIEW] =
    g_param_spec_object ("view",
                         _("View"),
                         _("View"),
                         HDY_TYPE_TAB_VIEW,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_ADJUSTMENT] =
    g_param_spec_object ("adjustment",
                         _("Adjustment"),
                         _("Adjustment"),
                         GTK_TYPE_ADJUSTMENT,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_NEEDS_ATTENTION_LEFT] =
    g_param_spec_boolean ("needs-attention-left",
                          _("Needs Attention Left"),
                          _("Needs Attention Left"),
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_NEEDS_ATTENTION_RIGHT] =
    g_param_spec_boolean ("needs-attention-right",
                          _("Needs Attention Right"),
                          _("Needs Attention Right"),
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  signals[SIGNAL_STOP_KINETIC_SCROLLING] =
    g_signal_new ("stop-kinetic-scrolling",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  0);

  signals[SIGNAL_ACTIVATE_TAB] =
    g_signal_new ("activate-tab",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  0);

  signals[SIGNAL_FOCUS_TAB] =
    g_signal_new ("focus-tab",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  2, GTK_TYPE_DIRECTION_TYPE, G_TYPE_BOOLEAN);

  signals[SIGNAL_REORDER_TAB] =
    g_signal_new ("reorder-tab",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  2, GTK_TYPE_DIRECTION_TYPE, G_TYPE_BOOLEAN);

  binding_set = gtk_binding_set_by_class (klass);

  gtk_binding_entry_add_signal (binding_set, GDK_KEY_space,     0, "activate-tab", 0);
  gtk_binding_entry_add_signal (binding_set, GDK_KEY_KP_Space,  0, "activate-tab", 0);
  gtk_binding_entry_add_signal (binding_set, GDK_KEY_Return,    0, "activate-tab", 0);
  gtk_binding_entry_add_signal (binding_set, GDK_KEY_ISO_Enter, 0, "activate-tab", 0);
  gtk_binding_entry_add_signal (binding_set, GDK_KEY_KP_Enter,  0, "activate-tab", 0);

  add_focus_bindings (binding_set, GDK_KEY_Page_Up,   GTK_DIR_TAB_BACKWARD, FALSE);
  add_focus_bindings (binding_set, GDK_KEY_Page_Down, GTK_DIR_TAB_FORWARD,  FALSE);
  add_focus_bindings (binding_set, GDK_KEY_Home,      GTK_DIR_TAB_BACKWARD, TRUE);
  add_focus_bindings (binding_set, GDK_KEY_End,       GTK_DIR_TAB_FORWARD,  TRUE);

  add_reorder_bindings (binding_set, GDK_KEY_Left,      GTK_DIR_LEFT,         FALSE);
  add_reorder_bindings (binding_set, GDK_KEY_Right,     GTK_DIR_RIGHT,        FALSE);
  add_reorder_bindings (binding_set, GDK_KEY_Page_Up,   GTK_DIR_TAB_BACKWARD, FALSE);
  add_reorder_bindings (binding_set, GDK_KEY_Page_Down, GTK_DIR_TAB_FORWARD,  FALSE);
  add_reorder_bindings (binding_set, GDK_KEY_Home,      GTK_DIR_TAB_BACKWARD, TRUE);
  add_reorder_bindings (binding_set, GDK_KEY_End,       GTK_DIR_TAB_FORWARD,  TRUE);

  gtk_widget_class_set_css_name (widget_class, "tabbox");
}

static void
hdy_tab_box_init (HdyTabBox *self)
{
  self->can_remove_placeholder = TRUE;

  gtk_widget_add_events (GTK_WIDGET (self),
                         GDK_BUTTON_PRESS_MASK |
                         GDK_BUTTON_RELEASE_MASK |
                         GDK_BUTTON_MOTION_MASK |
                         GDK_POINTER_MOTION_MASK |
                         GDK_TOUCH_MASK |
                         GDK_ENTER_NOTIFY_MASK |
                         GDK_LEAVE_NOTIFY_MASK |
                         GDK_SCROLL_MASK |
                         GDK_SMOOTH_SCROLL_MASK);

  self->touch_menu_gesture = g_object_new (GTK_TYPE_GESTURE_LONG_PRESS,
                                           "widget", GTK_WIDGET (self),
                                           "propagation-phase", GTK_PHASE_TARGET,
                                           "touch-only", TRUE,
                                           NULL);

  g_signal_connect_object (self->touch_menu_gesture,
                           "pressed",
                           G_CALLBACK (touch_menu_gesture_pressed),
                           self,
                           G_CONNECT_SWAPPED);

  gtk_drag_dest_set (GTK_WIDGET (self),
                     GTK_DEST_DEFAULT_MOTION,
                     dst_targets,
                     G_N_ELEMENTS (dst_targets),
                     GDK_ACTION_MOVE);

  self->source_targets = gtk_target_list_new (src_targets,
                                              G_N_ELEMENTS (src_targets));

  g_signal_connect_object (self, "activate-tab", G_CALLBACK (activate_tab), self, G_CONNECT_AFTER);
  g_signal_connect_object (self, "focus-tab", G_CALLBACK (focus_tab_cb), self, 0);
  g_signal_connect_object (self, "reorder-tab", G_CALLBACK (reorder_tab_cb), self, 0);
}

void
hdy_tab_box_set_view (HdyTabBox  *self,
                      HdyTabView *view)
{
  g_return_if_fail (HDY_IS_TAB_BOX (self));
  g_return_if_fail (HDY_IS_TAB_VIEW (view) || view == NULL);

  if (view == self->view)
    return;

  if (self->view) {
    force_end_reordering (self);

    g_signal_handlers_disconnect_by_func (self->view, add_page, self);
    g_signal_handlers_disconnect_by_func (self->view, remove_page, self);
    g_signal_handlers_disconnect_by_func (self->view, reorder_page, self);

    if (!self->pinned)
      g_signal_handlers_disconnect_by_func (self->view, view_drag_drop_cb, self);

    g_list_free_full (self->tabs, (GDestroyNotify) remove_and_free_tab_info);
    self->tabs = NULL;
    self->n_tabs = 0;

    gtk_widget_queue_allocate (GTK_WIDGET (self));
  }

  self->view = view;

  if (self->view) {
    int i, n_pages = hdy_tab_view_get_n_pages (self->view);

    for (i = n_pages - 1; i >= 0; i--)
      add_page (self, hdy_tab_view_get_nth_page (self->view, i), 0);

    g_signal_connect_object (self->view, "page-added", G_CALLBACK (add_page), self, G_CONNECT_SWAPPED);
    g_signal_connect_object (self->view, "page-removed", G_CALLBACK (remove_page), self, G_CONNECT_SWAPPED);
    g_signal_connect_object (self->view, "page-reordered", G_CALLBACK (reorder_page), self, G_CONNECT_SWAPPED);

    if (!self->pinned)
      g_signal_connect_object (self->view, "drag-drop", G_CALLBACK (view_drag_drop_cb), self, G_CONNECT_SWAPPED);
  }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_VIEW]);
}

void
hdy_tab_box_set_adjustment (HdyTabBox     *self,
                            GtkAdjustment *adjustment)
{
  g_return_if_fail (HDY_IS_TAB_BOX (self));
  g_return_if_fail (GTK_IS_ADJUSTMENT (adjustment) || adjustment == NULL);

  if (adjustment == self->adjustment)
    return;

  if (self->adjustment) {
    g_signal_handlers_disconnect_by_func (self->adjustment, adjustment_value_changed_cb, self);
    g_signal_handlers_disconnect_by_func (self->adjustment, update_needs_attention, self);
  }

  g_set_object (&self->adjustment, adjustment);

  if (self->adjustment) {
    g_signal_connect_object (self->adjustment, "value-changed", G_CALLBACK (adjustment_value_changed_cb), self, G_CONNECT_SWAPPED);
    g_signal_connect_object (self->adjustment, "notify::page-size", G_CALLBACK (update_needs_attention), self, G_CONNECT_SWAPPED);
  }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ADJUSTMENT]);
}

void
hdy_tab_box_set_block_scrolling (HdyTabBox *self,
                                 gboolean   block_scrolling)
{
  self->block_scrolling = block_scrolling;
}

void
hdy_tab_box_add_page (HdyTabBox  *self,
                      HdyTabPage *page,
                      gint        position)
{
  add_page (self, page, position);
}

void
hdy_tab_box_remove_page (HdyTabBox  *self,
                         HdyTabPage *page)
{
  remove_page (self, page);
}

void
hdy_tab_box_select_page (HdyTabBox  *self,
                         HdyTabPage *page)
{
  select_page (self, page);
}

void
hdy_tab_box_try_focus_selected_tab (HdyTabBox  *self)
{
  if (self->selected_tab)
    gtk_widget_grab_focus (GTK_WIDGET (self->selected_tab->tab));
}

gboolean
hdy_tab_box_is_page_focused (HdyTabBox  *self,
                             HdyTabPage *page)
{
  TabInfo *info = find_info_for_page (self, page);

  return info && gtk_widget_is_focus (GTK_WIDGET (info->tab));
}
