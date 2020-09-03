/*
 * Copyright (C) 2020 Alexander Mikhaylenko <exalm7659@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"
#include <glib/gi18n-lib.h>

#include "hdy-tab-view-private.h"

static const GtkTargetEntry dst_targets [] = {
  { "HDY_TAB", GTK_TARGET_SAME_APP, 0 },
};

/**
 * SECTION:hdy-tab-view
 * @short_description: TBD
 * @title: HdyTabView
 * @See_also: #HdyTabBar
 *
 * TBD
 *
 * Since: 1.2
 */

struct _HdyTabPage
{
  GObject parent_instance;

  GtkWidget *content;
  gboolean selected;
  gboolean pinned;
  gchar *title;
  gchar *tooltip;
  GIcon *icon;
  gboolean loading;
  GIcon *secondary_icon;
  gboolean needs_attention;
};

G_DEFINE_TYPE (HdyTabPage, hdy_tab_page, G_TYPE_OBJECT)

enum {
  PAGE_PROP_0,
  PAGE_PROP_CONTENT,
  PAGE_PROP_SELECTED,
  PAGE_PROP_PINNED,
  PAGE_PROP_TITLE,
  PAGE_PROP_TOOLTIP,
  PAGE_PROP_ICON,
  PAGE_PROP_LOADING,
  PAGE_PROP_SECONDARY_ICON,
  PAGE_PROP_NEEDS_ATTENTION,
  LAST_PAGE_PROP
};

static GParamSpec *page_props[LAST_PAGE_PROP];

struct _HdyTabView
{
  GtkBin parent_instance;

  GtkStack *stack;
  GListStore *pages;

  guint n_pages;
  guint n_pinned_pages;
  HdyTabPage *selected_page;
  GIcon *default_icon;
  GMenuModel *menu_model;

  GSList *group;
  gboolean is_dragging;
};

G_DEFINE_TYPE (HdyTabView, hdy_tab_view, GTK_TYPE_BIN)

enum {
  PROP_0,
  PROP_N_PAGES,
  PROP_N_PINNED_PAGES,
  PROP_IS_DRAGGING,
  PROP_SELECTED_PAGE,
  PROP_DEFAULT_ICON,
  PROP_MENU_MODEL,
  PROP_GROUP,
  LAST_PROP
};

static GParamSpec *props[LAST_PROP];

enum {
  SIGNAL_PAGE_ADDED,
  SIGNAL_PAGE_REMOVED,
  SIGNAL_PAGE_REORDERED,
  SIGNAL_PAGE_PINNED,
  SIGNAL_PAGE_UNPINNED,
  SIGNAL_SETUP_MENU,
  SIGNAL_CREATE_WINDOW,
  SIGNAL_SELECT_PAGE,
  SIGNAL_REORDER_PAGE,
  SIGNAL_LAST_SIGNAL,
};

static guint signals[SIGNAL_LAST_SIGNAL];

static void
set_page_selected (HdyTabPage *self,
                   gboolean    selected)
{
  g_return_if_fail (HDY_IS_TAB_PAGE (self));

  selected = !!selected;

  if (self->selected == selected)
    return;

  self->selected = selected;

  g_object_notify_by_pspec (G_OBJECT (self), page_props[PAGE_PROP_SELECTED]);
}

static void
set_page_pinned (HdyTabPage *self,
                 gboolean    pinned)
{
  g_return_if_fail (HDY_IS_TAB_PAGE (self));

  pinned = !!pinned;

  if (self->pinned == pinned)
    return;

  self->pinned = pinned;

  g_object_notify_by_pspec (G_OBJECT (self), page_props[PAGE_PROP_PINNED]);
}

static void
hdy_tab_page_dispose (GObject *object)
{
  HdyTabPage *self = HDY_TAB_PAGE (object);

  g_clear_object (&self->content);

  G_OBJECT_CLASS (hdy_tab_page_parent_class)->dispose (object);
}

static void
hdy_tab_page_finalize (GObject *object)
{
  HdyTabPage *self = (HdyTabPage *)object;

  g_clear_pointer (&self->title, g_free);
  g_clear_pointer (&self->tooltip, g_free);
  g_clear_object (&self->icon);
  g_clear_object (&self->secondary_icon);

  G_OBJECT_CLASS (hdy_tab_page_parent_class)->finalize (object);
}

static void
hdy_tab_page_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  HdyTabPage *self = HDY_TAB_PAGE (object);

  switch (prop_id) {
  case PAGE_PROP_CONTENT:
    g_value_set_object (value, hdy_tab_page_get_content (self));
    break;

  case PAGE_PROP_SELECTED:
    g_value_set_boolean (value, hdy_tab_page_get_selected (self));
    break;

  case PAGE_PROP_PINNED:
    g_value_set_boolean (value, hdy_tab_page_get_pinned (self));
    break;

  case PAGE_PROP_TITLE:
    g_value_set_string (value, hdy_tab_page_get_title (self));
    break;

  case PAGE_PROP_TOOLTIP:
    g_value_set_string (value, hdy_tab_page_get_tooltip (self));
    break;

  case PAGE_PROP_ICON:
    g_value_set_object (value, hdy_tab_page_get_icon (self));
    break;

  case PAGE_PROP_LOADING:
    g_value_set_boolean (value, hdy_tab_page_get_loading (self));
    break;

  case PAGE_PROP_SECONDARY_ICON:
    g_value_set_object (value, hdy_tab_page_get_secondary_icon (self));
    break;

  case PAGE_PROP_NEEDS_ATTENTION:
    g_value_set_boolean (value, hdy_tab_page_get_needs_attention (self));
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
hdy_tab_page_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  HdyTabPage *self = HDY_TAB_PAGE (object);

  switch (prop_id) {
  case PAGE_PROP_CONTENT:
    g_set_object (&self->content, g_value_get_object (value));
    break;

  case PAGE_PROP_TITLE:
    hdy_tab_page_set_title (self, g_value_get_string (value));
    break;

  case PAGE_PROP_TOOLTIP:
    hdy_tab_page_set_tooltip (self, g_value_get_string (value));
    break;

  case PAGE_PROP_ICON:
    hdy_tab_page_set_icon (self, g_value_get_object (value));
    break;

  case PAGE_PROP_LOADING:
    hdy_tab_page_set_loading (self, g_value_get_boolean (value));
    break;

  case PAGE_PROP_SECONDARY_ICON:
    hdy_tab_page_set_secondary_icon (self, g_value_get_object (value));
    break;

  case PAGE_PROP_NEEDS_ATTENTION:
    hdy_tab_page_set_needs_attention (self, g_value_get_boolean (value));
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
hdy_tab_page_class_init (HdyTabPageClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = hdy_tab_page_dispose;
  object_class->finalize = hdy_tab_page_finalize;
  object_class->get_property = hdy_tab_page_get_property;
  object_class->set_property = hdy_tab_page_set_property;

  /**
   * HdyTabPage:content:
   *
   * TBD
   *
   * Since: 1.2
   */
  page_props[PAGE_PROP_CONTENT] =
    g_param_spec_object ("content",
                         _("Content"),
                         _("Content"),
                         GTK_TYPE_WIDGET,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  /**
   * HdyTabPage:selected:
   *
   * TBD
   *
   * Since: 1.2
   */
  page_props[PAGE_PROP_SELECTED] =
    g_param_spec_boolean ("selected",
                         _("Selected"),
                         _("Selected"),
                         FALSE,
                         G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * HdyTabPage:pinned:
   *
   * TBD
   *
   * Since: 1.2
   */
  page_props[PAGE_PROP_PINNED] =
    g_param_spec_boolean ("pinned",
                         _("Pinned"),
                         _("Pinned"),
                         FALSE,
                         G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * HdyTabPage:title:
   *
   * TBD
   *
   * Since: 1.2
   */
  page_props[PAGE_PROP_TITLE] =
    g_param_spec_string ("title",
                         _("Title"),
                         _("Title"),
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * HdyTabPage:tooltip:
   *
   * TBD
   *
   * Since: 1.2
   */
  page_props[PAGE_PROP_TOOLTIP] =
    g_param_spec_string ("tooltip",
                         _("Tooltip"),
                         _("Tooltip"),
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * HdyTabPage:icon:
   *
   * TBD
   *
   * Since: 1.2
   */
  page_props[PAGE_PROP_ICON] =
    g_param_spec_object ("icon",
                         _("Icon"),
                         _("Icon"),
                         G_TYPE_ICON,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * HdyTabPage:loading:
   *
   * TBD
   *
   * Since: 1.2
   */
  page_props[PAGE_PROP_LOADING] =
    g_param_spec_boolean ("loading",
                         _("Loading"),
                         _("Loading"),
                         FALSE,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * HdyTabPage:secondary-icon:
   *
   * TBD
   *
   * Since: 1.2
   */
  page_props[PAGE_PROP_SECONDARY_ICON] =
    g_param_spec_object ("secondary-icon",
                         _("Secondary Icon"),
                         _("Secondary Icon"),
                         G_TYPE_ICON,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * HdyTabPage:needs-attention:
   *
   * TBD
   *
   * Since: 1.2
   */
  page_props[PAGE_PROP_NEEDS_ATTENTION] =
    g_param_spec_boolean ("needs-attention",
                         _("Needs Attention"),
                         _("Needs Attention"),
                         FALSE,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PAGE_PROP, page_props);
}

static void
hdy_tab_page_init (HdyTabPage *self)
{
}

static gboolean
object_handled_accumulator (GSignalInvocationHint *ihint,
                            GValue                *return_accu,
                            const GValue          *handler_return,
                            gpointer               data)
{
  GObject *object = g_value_get_object (handler_return);

  g_value_set_object (return_accu, object);

  return !object;
}

static void
set_is_dragging (HdyTabView *self,
                 gboolean    is_dragging)
{
  if (is_dragging == self->is_dragging)
    return;

  self->is_dragging = is_dragging;

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_IS_DRAGGING]);
}

static void
set_n_pages (HdyTabView *self,
             guint       n_pages)
{
  if (n_pages == self->n_pages)
    return;

  self->n_pages = n_pages;

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_N_PAGES]);
}

static void
set_n_pinned_pages (HdyTabView *self,
                    guint       n_pinned_pages)
{
  if (n_pinned_pages == self->n_pinned_pages)
    return;

  self->n_pinned_pages = n_pinned_pages;

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_N_PINNED_PAGES]);
}

static void
check_close_window (HdyTabView *self)
{
  GtkWidget *toplevel;

  if (self->n_pages > 0)
    return;

  toplevel = gtk_widget_get_toplevel (GTK_WIDGET (self));

  if (!GTK_IS_WINDOW (toplevel))
    return;

  gtk_window_close (GTK_WINDOW (toplevel));
}

static void
attach_page (HdyTabView *self,
             HdyTabPage *page,
             guint       position)
{
  gboolean pinned = hdy_tab_page_get_pinned (page);
  GtkWidget *content = hdy_tab_page_get_content (page);

  if (!pinned)
    position += self->n_pinned_pages;

  g_list_store_insert (self->pages, position, page);

  gtk_container_add (GTK_CONTAINER (self->stack), content);
  gtk_container_child_set (GTK_CONTAINER (self->stack), content,
                           "position", position,
                           NULL);

  set_n_pages (self, self->n_pages + 1);
  if (pinned)
    set_n_pinned_pages (self, self->n_pages + 1);

  g_signal_emit (self, signals[SIGNAL_PAGE_ADDED], 0, page, position);
}

static void
detach_page (HdyTabView *self,
             HdyTabPage *page)
{
  guint pos = hdy_tab_view_get_page_position (self, page);

  if (page == self->selected_page)
    if (!hdy_tab_view_select_next_page (self))
      hdy_tab_view_select_previous_page (self);

  g_list_store_remove (self->pages, pos);
  set_n_pages (self, self->n_pages - 1);

  if (hdy_tab_page_get_pinned (page))
    set_n_pinned_pages (self, self->n_pinned_pages - 1);

  gtk_container_remove (GTK_CONTAINER (self->stack),
                        hdy_tab_page_get_content (page));

  g_signal_emit (self, signals[SIGNAL_PAGE_REMOVED], 0, page);

  check_close_window (self);
}

static HdyTabPage *
insert_page (HdyTabView *self,
             GtkWidget  *content,
             guint       position,
             gboolean    pinned)
{
  HdyTabPage *page;

  g_assert (position <= self->n_pages);

  page = g_object_new (HDY_TYPE_TAB_PAGE, "content", content, NULL);

  set_page_pinned (page, pinned);

  attach_page (self, page, position);

  if (!self->selected_page)
    hdy_tab_view_set_selected_page (self, page);

  return page;
}

static void
set_group_from_view (HdyTabView *self,
                     HdyTabView *other_view)
{
  GSList *slist;

  if (other_view)
    slist = hdy_tab_view_get_group (other_view);
  else
    slist = NULL;

  hdy_tab_view_set_group (self, slist);
}

static void
close_page (HdyTabView *self,
            HdyTabPage *page)
{
  detach_page (self, page);

  g_object_unref (page);
}

static void
add_select_bindings (GtkBindingSet    *binding_set,
                     guint             keysym,
                     GtkDirectionType  direction,
                     gboolean          last)
{
  /* All keypad keysyms are aligned at the same order as non-keypad ones */
  guint keypad_keysym = keysym - GDK_KEY_Left + GDK_KEY_KP_Left;

  gtk_binding_entry_add_signal (binding_set, keysym, GDK_CONTROL_MASK,
                                "select-page", 2,
                                GTK_TYPE_DIRECTION_TYPE, direction,
                                G_TYPE_BOOLEAN, last);
  gtk_binding_entry_add_signal (binding_set, keypad_keysym, GDK_CONTROL_MASK,
                                "select-page", 2,
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

  gtk_binding_entry_add_signal (binding_set, keysym,
                                GDK_CONTROL_MASK | GDK_MOD1_MASK,
                                "reorder-page", 2,
                                GTK_TYPE_DIRECTION_TYPE, direction,
                                G_TYPE_BOOLEAN, last);
  gtk_binding_entry_add_signal (binding_set, keypad_keysym,
                                GDK_CONTROL_MASK | GDK_MOD1_MASK,
                                "reorder-page", 2,
                                GTK_TYPE_DIRECTION_TYPE, direction,
                                G_TYPE_BOOLEAN, last);
}

static void
select_page_cb (HdyTabView       *self,
                GtkDirectionType  direction,
                gboolean          last)
{
  gboolean is_rtl, success = last;

  if (!self->selected_page)
    return;

  is_rtl = gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL;

  if (direction == GTK_DIR_LEFT)
    direction = is_rtl ? GTK_DIR_TAB_FORWARD : GTK_DIR_TAB_BACKWARD;
  else if (direction == GTK_DIR_RIGHT)
    direction = is_rtl ? GTK_DIR_TAB_BACKWARD : GTK_DIR_TAB_FORWARD;

  if (direction == GTK_DIR_TAB_BACKWARD) {
    if (last)
      success = hdy_tab_view_select_first_page (self);
    else
      success = hdy_tab_view_select_previous_page (self);
  } else if (direction == GTK_DIR_TAB_FORWARD) {
    if (last)
      success = hdy_tab_view_select_last_page (self);
    else
      success = hdy_tab_view_select_next_page (self);
  }

  gtk_widget_grab_focus (hdy_tab_page_get_content (self->selected_page));

  if (!success)
    gtk_widget_error_bell (GTK_WIDGET (self));
}

static void
reorder_page_cb (HdyTabView       *self,
                 GtkDirectionType  direction,
                 gboolean          last)
{
  gboolean is_rtl, success = last;

  if (!self->selected_page)
    return;

  is_rtl = gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL;

  if (direction == GTK_DIR_LEFT)
    direction = is_rtl ? GTK_DIR_TAB_FORWARD : GTK_DIR_TAB_BACKWARD;
  else if (direction == GTK_DIR_RIGHT)
    direction = is_rtl ? GTK_DIR_TAB_BACKWARD : GTK_DIR_TAB_FORWARD;

  if (direction == GTK_DIR_TAB_BACKWARD) {
    if (last)
      success = hdy_tab_view_reorder_first (self, self->selected_page);
    else
      success = hdy_tab_view_reorder_backward (self, self->selected_page);
  } else if (direction == GTK_DIR_TAB_FORWARD) {
    if (last)
      success = hdy_tab_view_reorder_last (self, self->selected_page);
    else
      success = hdy_tab_view_reorder_forward (self, self->selected_page);
  }

  if (!success)
    gtk_widget_error_bell (GTK_WIDGET (self));
}

static void
hdy_tab_view_dispose (GObject *object)
{
  HdyTabView *self = HDY_TAB_VIEW (object);
  GSList *l;

  self->group = g_slist_remove (self->group, self);

  for (l = self->group; l; l = l->next) {
    HdyTabView *view = l->data;

    view->group = self->group;
  }

  self->group = NULL;

  if (self->pages) {
    while (self->n_pages) {
      HdyTabPage *page = hdy_tab_view_get_nth_page (self, 0);

      close_page (self, page);

      // FIXME why is this needed
      g_object_unref (page);
    }

    g_clear_object (&self->pages);
  }

  G_OBJECT_CLASS (hdy_tab_view_parent_class)->dispose (object);
}

static void
hdy_tab_view_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  HdyTabView *self = HDY_TAB_VIEW (object);

  switch (prop_id) {
  case PROP_N_PAGES:
    g_value_set_uint (value, hdy_tab_view_get_n_pages (self));
    break;

  case PROP_N_PINNED_PAGES:
    g_value_set_uint (value, hdy_tab_view_get_n_pinned_pages (self));
    break;

  case PROP_IS_DRAGGING:
    g_value_set_boolean (value, hdy_tab_view_get_is_dragging (self));
    break;

  case PROP_SELECTED_PAGE:
    g_value_set_object (value, hdy_tab_view_get_selected_page (self));
    break;

  case PROP_DEFAULT_ICON:
    g_value_set_object (value, hdy_tab_view_get_default_icon (self));
    break;

  case PROP_MENU_MODEL:
    g_value_set_object (value, hdy_tab_view_get_menu_model (self));
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
hdy_tab_view_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  HdyTabView *self = HDY_TAB_VIEW (object);

  switch (prop_id) {
  case PROP_SELECTED_PAGE:
    hdy_tab_view_set_selected_page (self, g_value_get_object (value));
    break;

  case PROP_DEFAULT_ICON:
    hdy_tab_view_set_default_icon (self, g_value_get_object (value));
    break;

  case PROP_MENU_MODEL:
    hdy_tab_view_set_menu_model (self, g_value_get_object (value));
    break;

  case PROP_GROUP:
    set_group_from_view (self, g_value_get_object (value));
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
hdy_tab_view_class_init (HdyTabViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GtkBindingSet *binding_set;

  object_class->dispose = hdy_tab_view_dispose;
  object_class->get_property = hdy_tab_view_get_property;
  object_class->set_property = hdy_tab_view_set_property;

  /**
   * HdyTabView:n-pages:
   *
   * TBD
   *
   * Since: 1.2
   */
  props[PROP_N_PAGES] =
    g_param_spec_uint ("n-pages",
                       _("Number of Pages"),
                       _("Number of Pages"),
                       0, G_MAXUINT, 0,
                       G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * HdyTabView:n-pinned-pages:
   *
   * TBD
   *
   * Since: 1.2
   */
  props[PROP_N_PINNED_PAGES] =
    g_param_spec_uint ("n-pinned-pages",
                       _("Number of Pinned Pages"),
                       _("Number of Pinned Pages"),
                       0, G_MAXUINT, 0,
                       G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * HdyTabView:is-dragging:
   *
   * TBD
   *
   * Since: 1.2
   */
  props[PROP_IS_DRAGGING] =
    g_param_spec_boolean ("is-dragging",
                          _("Is Dragging"),
                          _("Is Dragging"),
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * HdyTabView:selected-page:
   *
   * TBD
   *
   * Since: 1.2
   */
  props[PROP_SELECTED_PAGE] =
    g_param_spec_object ("selected-page",
                         _("Selected Page"),
                         _("Selected Page"),
                         HDY_TYPE_TAB_PAGE,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * HdyTabView:default-icon:
   *
   * TBD
   *
   * Since: 1.2
   */
  props[PROP_DEFAULT_ICON] =
    g_param_spec_object ("default-icon",
                         _("Default Icon"),
                         _("Default Icon"),
                         G_TYPE_ICON,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * HdyTabView:menu-model:
   *
   * TBD
   *
   * Since: 1.2
   */
  props[PROP_MENU_MODEL] =
    g_param_spec_object ("menu-model",
                         _("Menu Model"),
                         _("Menu Model"),
                         G_TYPE_MENU_MODEL,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * HdyTabView:group:
   *
   * TBD
   *
   * Since: 1.2
   */
  props[PROP_GROUP] =
    g_param_spec_object ("group",
                         _("Group"),
                         _("Group"),
                         HDY_TYPE_TAB_VIEW,
                         G_PARAM_WRITABLE);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  /**
   * HdyTabView::page-added:
   * @self: a #HdyTabView
   * @page: TBD
   * @position: TBD
   *
   * TBD
   *
   * Since: 1.2
   */
  signals[SIGNAL_PAGE_ADDED] =
    g_signal_new ("page-added",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  2,
                  HDY_TYPE_TAB_PAGE, G_TYPE_UINT);

  /**
   * HdyTabView::page-removed:
   * @self: a #HdyTabView
   * @page: TBD
   *
   * TBD
   *
   * Since: 1.2
   */
  signals[SIGNAL_PAGE_REMOVED] =
    g_signal_new ("page-removed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  1,
                  HDY_TYPE_TAB_PAGE);

  /**
   * HdyTabView::page-reordered:
   * @self: a #HdyTabView
   * @page: TBD
   * @position: TBD
   *
   * TBD
   *
   * Since: 1.2
   */
  signals[SIGNAL_PAGE_REORDERED] =
    g_signal_new ("page-reordered",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  2,
                  HDY_TYPE_TAB_PAGE, G_TYPE_UINT);

  /**
   * HdyTabView::page-pinned:
   * @self: a #HdyTabView
   * @page: TBD
   *
   * TBD
   *
   * Since: 1.2
   */
  signals[SIGNAL_PAGE_PINNED] =
    g_signal_new ("page-pinned",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  1,
                  HDY_TYPE_TAB_PAGE);

  /**
   * HdyTabView::page-unpinned:
   * @self: a #HdyTabView
   * @page: TBD
   *
   * TBD
   *
   * Since: 1.2
   */
  signals[SIGNAL_PAGE_UNPINNED] =
    g_signal_new ("page-unpinned",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  1,
                  HDY_TYPE_TAB_PAGE);

  /**
   * HdyTabView::setup-menu:
   * @self: a #HdyTabView
   * @page: TBD
   *
   * TBD
   *
   * Since: 1.2
   */
  signals[SIGNAL_SETUP_MENU] =
    g_signal_new ("setup-menu",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  1,
                  HDY_TYPE_TAB_PAGE);

  /**
   * HdyTabView::create-window:
   * @self: a #HdyTabView
   *
   * TBD
   *
   * Returns: (transfer none) (nullable): TBD
   *
   * Since: 1.2
   */
  signals[SIGNAL_CREATE_WINDOW] =
    g_signal_new ("create-window",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  object_handled_accumulator,
                  NULL, NULL,
                  HDY_TYPE_TAB_VIEW,
                  0);

  signals[SIGNAL_SELECT_PAGE] =
    g_signal_new ("select-page",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 2,
                  GTK_TYPE_DIRECTION_TYPE, G_TYPE_BOOLEAN);

  signals[SIGNAL_REORDER_PAGE] =
    g_signal_new ("reorder-page",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 2,
                  GTK_TYPE_DIRECTION_TYPE, G_TYPE_BOOLEAN);

  binding_set = gtk_binding_set_by_class (klass);

  add_select_bindings (binding_set, GDK_KEY_Page_Up,   GTK_DIR_TAB_BACKWARD, FALSE);
  add_select_bindings (binding_set, GDK_KEY_Page_Down, GTK_DIR_TAB_FORWARD,  FALSE);
  add_select_bindings (binding_set, GDK_KEY_Home,      GTK_DIR_TAB_BACKWARD, TRUE);
  add_select_bindings (binding_set, GDK_KEY_End,       GTK_DIR_TAB_FORWARD,  TRUE);

  add_reorder_bindings (binding_set, GDK_KEY_Page_Up,   GTK_DIR_TAB_BACKWARD, FALSE);
  add_reorder_bindings (binding_set, GDK_KEY_Page_Down, GTK_DIR_TAB_FORWARD,  FALSE);
  add_reorder_bindings (binding_set, GDK_KEY_Home,      GTK_DIR_TAB_BACKWARD, TRUE);
  add_reorder_bindings (binding_set, GDK_KEY_End,       GTK_DIR_TAB_FORWARD,  TRUE);

  gtk_widget_class_set_css_name (widget_class, "tabview");
}

static void
hdy_tab_view_init (HdyTabView *self)
{
  GtkWidget *overlay, *drag_shield;

  self->pages = g_list_store_new (HDY_TYPE_TAB_PAGE);
  self->group = g_slist_prepend (NULL, self);
  self->default_icon = G_ICON (g_themed_icon_new ("hdy-tab-icon-missing-symbolic"));

  overlay = gtk_overlay_new ();
  gtk_widget_show (overlay);
  gtk_container_add (GTK_CONTAINER (self), overlay);

  self->stack = GTK_STACK (gtk_stack_new ());
  gtk_widget_show (GTK_WIDGET (self->stack));
  gtk_container_add (GTK_CONTAINER (overlay), GTK_WIDGET (self->stack));

  drag_shield = gtk_event_box_new ();
  gtk_widget_add_events (drag_shield, GDK_ALL_EVENTS_MASK);
  gtk_overlay_add_overlay (GTK_OVERLAY (overlay), drag_shield);

  g_object_bind_property (self, "is-dragging",
                          drag_shield, "visible",
                          G_BINDING_DEFAULT);

  gtk_drag_dest_set (GTK_WIDGET (self),
                     GTK_DEST_DEFAULT_MOTION,
                     dst_targets,
                     G_N_ELEMENTS (dst_targets),
                     GDK_ACTION_MOVE);

  g_signal_connect_object (self, "select-page", G_CALLBACK (select_page_cb), self, 0);
  g_signal_connect_object (self, "reorder-page", G_CALLBACK (reorder_page_cb), self, 0);
}

/**
 * hdy_tab_page_get_content:
 * @self: a #HdyTabPage
 *
 * TBD
 *
 * Returns: (transfer none): TBD
 *
 * Since: 1.2
 */
GtkWidget *
hdy_tab_page_get_content (HdyTabPage *self)
{
  g_return_val_if_fail (HDY_IS_TAB_PAGE (self), NULL);

  return self->content;
}

/**
 * hdy_tab_page_get_selected:
 * @self: a #HdyTabPage
 *
 * TBD
 *
 * Returns: TBD
 *
 * Since: 1.2
 */
gboolean
hdy_tab_page_get_selected (HdyTabPage *self)
{
  g_return_val_if_fail (HDY_IS_TAB_PAGE (self), FALSE);

  return self->selected;
}

/**
 * hdy_tab_page_get_pinned:
 * @self: a #HdyTabPage
 *
 * TBD
 *
 * Returns: TBD
 *
 * Since: 1.2
 */
gboolean
hdy_tab_page_get_pinned (HdyTabPage *self)
{
  g_return_val_if_fail (HDY_IS_TAB_PAGE (self), FALSE);

  return self->pinned;
}

/**
 * hdy_tab_page_get_title:
 * @self: a #HdyTabPage
 *
 * TBD
 *
 * Returns: (nullable): TBD
 *
 * Since: 1.2
 */
const gchar *
hdy_tab_page_get_title (HdyTabPage *self)
{
  g_return_val_if_fail (HDY_IS_TAB_PAGE (self), NULL);

  return self->title;
}

/**
 * hdy_tab_page_set_title:
 * @self: a #HdyTabPage
 * @title: (nullable): TBD
 *
 * TBD
 *
 * Since: 1.2
 */
void
hdy_tab_page_set_title (HdyTabPage  *self,
                        const gchar *title)
{
  g_return_if_fail (HDY_IS_TAB_PAGE (self));

  if (!g_strcmp0 (title, self->title))
    return;

  if (self->title)
    g_clear_pointer (&self->title, g_free);

  if (title)
    self->title = g_strdup (title);

  g_object_notify_by_pspec (G_OBJECT (self), page_props[PAGE_PROP_TITLE]);
}

/**
 * hdy_tab_page_get_tooltip:
 * @self: a #HdyTabPage
 *
 * TBD
 *
 * Returns: (nullable): TBD
 *
 * Since: 1.2
 */
const gchar *
hdy_tab_page_get_tooltip (HdyTabPage *self)
{
  g_return_val_if_fail (HDY_IS_TAB_PAGE (self), NULL);

  return self->tooltip;
}

/**
 * hdy_tab_page_set_tooltip:
 * @self: a #HdyTabPage
 * @tooltip: (nullable): TBD
 *
 * TBD
 *
 * Since: 1.2
 */
void
hdy_tab_page_set_tooltip (HdyTabPage  *self,
                          const gchar *tooltip)
{
  g_return_if_fail (HDY_IS_TAB_PAGE (self));

  if (!g_strcmp0 (tooltip, self->tooltip))
    return;

  if (self->tooltip)
    g_clear_pointer (&self->tooltip, g_free);

  if (tooltip)
    self->tooltip = g_strdup (tooltip);

  g_object_notify_by_pspec (G_OBJECT (self), page_props[PAGE_PROP_TOOLTIP]);
}

/**
 * hdy_tab_page_get_icon:
 * @self: a #HdyTabPage
 *
 * TBD
 *
 * Returns: (transfer none) (nullable): TBD
 *
 * Since: 1.2
 */
GIcon *
hdy_tab_page_get_icon (HdyTabPage *self)
{
  g_return_val_if_fail (HDY_IS_TAB_PAGE (self), NULL);

  return self->icon;
}

/**
 * hdy_tab_page_set_icon:
 * @self: a #HdyTabPage
 * @icon: (nullable): TBD
 *
 * TBD
 *
 * Since: 1.2
 */
void
hdy_tab_page_set_icon (HdyTabPage *self,
                       GIcon      *icon)
{
  g_return_if_fail (HDY_IS_TAB_PAGE (self));
  g_return_if_fail (G_IS_ICON (icon) || icon == NULL);

  if (self->icon == icon)
    return;

  g_set_object (&self->icon, icon);

  g_object_notify_by_pspec (G_OBJECT (self), page_props[PAGE_PROP_ICON]);
}

/**
 * hdy_tab_page_get_loading:
 * @self: a #HdyTabPage
 *
 * TBD
 *
 * Returns: TBD
 *
 * Since: 1.2
 */
gboolean
hdy_tab_page_get_loading (HdyTabPage *self)
{
  g_return_val_if_fail (HDY_IS_TAB_PAGE (self), FALSE);

  return self->loading;
}

/**
 * hdy_tab_page_set_loading:
 * @self: a #HdyTabPage
 * @loading: TBD
 *
 * TBD
 *
 * Since: 1.2
 */
void
hdy_tab_page_set_loading (HdyTabPage *self,
                          gboolean    loading)
{
  g_return_if_fail (HDY_IS_TAB_PAGE (self));

  loading = !!loading;

  if (self->loading == loading)
    return;

  self->loading = loading;

  g_object_notify_by_pspec (G_OBJECT (self), page_props[PAGE_PROP_LOADING]);
}

/**
 * hdy_tab_page_get_secondary_icon:
 * @self: a #HdyTabPage
 *
 * TBD
 *
 * Returns: (transfer none) (nullable): TBD
 *
 * Since: 1.2
 */
GIcon *
hdy_tab_page_get_secondary_icon (HdyTabPage *self)
{
  g_return_val_if_fail (HDY_IS_TAB_PAGE (self), NULL);

  return self->secondary_icon;
}

/**
 * hdy_tab_page_set_secondary_icon:
 * @self: a #HdyTabPage
 * @secondary_icon: (nullable): TBD
 *
 * TBD
 *
 * Since: 1.2
 */
void
hdy_tab_page_set_secondary_icon (HdyTabPage *self,
                                 GIcon      *secondary_icon)
{
  g_return_if_fail (HDY_IS_TAB_PAGE (self));
  g_return_if_fail (G_IS_ICON (secondary_icon) || secondary_icon == NULL);

  if (self->secondary_icon == secondary_icon)
    return;

  g_set_object (&self->secondary_icon, secondary_icon);

  g_object_notify_by_pspec (G_OBJECT (self), page_props[PAGE_PROP_SECONDARY_ICON]);
}

/**
 * hdy_tab_page_get_needs_attention:
 * @self: a #HdyTabPage
 *
 * TBD
 *
 * Returns: TBD
 *
 * Since: 1.2
 */
gboolean
hdy_tab_page_get_needs_attention (HdyTabPage *self)
{
  g_return_val_if_fail (HDY_IS_TAB_PAGE (self), FALSE);

  return self->needs_attention;
}

/**
 * hdy_tab_page_set_needs_attention:
 * @self: a #HdyTabPage
 * @needs_attention: TBD
 *
 * TBD
 *
 * Since: 1.2
 */
void
hdy_tab_page_set_needs_attention (HdyTabPage *self,
                                  gboolean    needs_attention)
{
  g_return_if_fail (HDY_IS_TAB_PAGE (self));

  needs_attention = !!needs_attention;

  if (self->needs_attention == needs_attention)
    return;

  self->needs_attention = needs_attention;

  g_object_notify_by_pspec (G_OBJECT (self), page_props[PAGE_PROP_NEEDS_ATTENTION]);
}

/**
 * hdy_tab_view_new:
 *
 * Creates a new #HdyTabView widget.
 *
 * Returns: a new #HdyTabView
 *
 * Since: 1.2
 */
HdyTabView *
hdy_tab_view_new (void)
{
  return g_object_new (HDY_TYPE_TAB_VIEW, NULL);
}

/**
 * hdy_tab_view_get_n_pages:
 * @self: a #HdyTabView
 *
 * TBD
 *
 * Returns: TBD
 *
 * Since: 1.2
 */
guint
hdy_tab_view_get_n_pages (HdyTabView *self)
{
  g_return_val_if_fail (HDY_IS_TAB_VIEW (self), 0);

  return self->n_pages;
}

/**
 * hdy_tab_view_get_n_pinned_pages:
 * @self: a #HdyTabView
 *
 * TBD
 *
 * Returns: TBD
 *
 * Since: 1.2
 */
guint
hdy_tab_view_get_n_pinned_pages (HdyTabView *self)
{
  g_return_val_if_fail (HDY_IS_TAB_VIEW (self), 0);

  return self->n_pinned_pages;
}

/**
 * hdy_tab_view_get_is_dragging:
 * @self: a #HdyTabView
 *
 * TBD
 *
 * Returns: TBD
 *
 * Since: 1.2
 */
gboolean
hdy_tab_view_get_is_dragging (HdyTabView *self)
{
  g_return_val_if_fail (HDY_IS_TAB_VIEW (self), FALSE);

  return self->is_dragging;
}

void
hdy_tab_view_start_drag (HdyTabView *self)
{
  GSList *l;

  g_return_if_fail (HDY_IS_TAB_VIEW (self));

  for (l = self->group; l; l = l->next) {
    HdyTabView *view = l->data;

    set_is_dragging (view, TRUE);
  }
}

void
hdy_tab_view_end_drag (HdyTabView *self)
{
  GSList *l;

  g_return_if_fail (HDY_IS_TAB_VIEW (self));

  for (l = self->group; l; l = l->next) {
    HdyTabView *view = l->data;

    set_is_dragging (view, FALSE);
  }
}

/**
 * hdy_tab_view_get_selected_page:
 * @self: a #HdyTabView
 *
 * TBD
 *
 * Returns: (transfer none): TBD
 *
 * Since: 1.2
 */
HdyTabPage *
hdy_tab_view_get_selected_page (HdyTabView *self)
{
  g_return_val_if_fail (HDY_IS_TAB_VIEW (self), NULL);

  return self->selected_page;
}

/**
 * hdy_tab_view_set_selected_page:
 * @self: a #HdyTabView
 * @selected_page: TBD
 *
 * TBD
 *
 * Since: 1.2
 */
void
hdy_tab_view_set_selected_page (HdyTabView *self,
                                HdyTabPage *selected_page)
{
  g_return_if_fail (HDY_IS_TAB_VIEW (self));
  g_return_if_fail (HDY_IS_TAB_PAGE (selected_page));

  if (self->selected_page == selected_page)
    return;

  if (self->selected_page)
    set_page_selected (self->selected_page, FALSE);

  self->selected_page = selected_page;
  gtk_stack_set_visible_child (self->stack,
                               hdy_tab_page_get_content (selected_page));

  if (self->selected_page)
    set_page_selected (self->selected_page, TRUE);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SELECTED_PAGE]);
}

/**
 * hdy_tab_view_select_previous_page:
 * @self: a #HdyTabView
 *
 * TBD
 *
 * Returns: TBD
 *
 * Since: 1.2
 */
gboolean
hdy_tab_view_select_previous_page (HdyTabView *self)
{
  HdyTabPage *page;
  guint pos;

  g_return_val_if_fail (HDY_IS_TAB_VIEW (self), FALSE);

  if (!self->selected_page)
    return FALSE;

  pos = hdy_tab_view_get_page_position (self, self->selected_page);

  if (pos <= 0)
    return FALSE;

  page = hdy_tab_view_get_nth_page (self, pos - 1);

  hdy_tab_view_set_selected_page (self, page);

  return TRUE;
}

/**
 * hdy_tab_view_select_next_page:
 * @self: a #HdyTabView
 *
 * TBD
 *
 * Returns: TBD
 *
 * Since: 1.2
 */
gboolean
hdy_tab_view_select_next_page (HdyTabView *self)
{
  HdyTabPage *page;
  guint pos;

  g_return_val_if_fail (HDY_IS_TAB_VIEW (self), FALSE);

  if (!self->selected_page)
    return FALSE;

  pos = hdy_tab_view_get_page_position (self, self->selected_page);

  if (pos + 1 >= self->n_pages)
    return FALSE;

  page = hdy_tab_view_get_nth_page (self, pos + 1);

  hdy_tab_view_set_selected_page (self, page);

  return TRUE;
}

/**
 * hdy_tab_view_select_first_page:
 * @self: a #HdyTabView
 *
 * TBD
 *
 * Returns: TBD
 *
 * Since: 1.2
 */
gboolean
hdy_tab_view_select_first_page (HdyTabView *self)
{
  HdyTabPage *page;
  guint pos;
  gboolean pinned;

  g_return_val_if_fail (HDY_IS_TAB_VIEW (self), FALSE);

  if (!self->selected_page)
    return FALSE;

  pinned = hdy_tab_page_get_pinned (self->selected_page);
  pos = pinned ? 0 : self->n_pinned_pages;

  page = hdy_tab_view_get_nth_page (self, pos);

  /* If we're on the first non-pinned tab, go to the first pinned tab */
  if (page == self->selected_page && !pinned)
    page = hdy_tab_view_get_nth_page (self, 0);

  if (page == self->selected_page)
    return FALSE;

  hdy_tab_view_set_selected_page (self, page);

  return TRUE;
}

/**
 * hdy_tab_view_select_last_page:
 * @self: a #HdyTabView
 *
 * TBD
 *
 * Returns: TBD
 *
 * Since: 1.2
 */
gboolean
hdy_tab_view_select_last_page (HdyTabView *self)
{
  HdyTabPage *page;
  guint pos;
  gboolean pinned;

  g_return_val_if_fail (HDY_IS_TAB_VIEW (self), FALSE);

  if (!self->selected_page)
    return FALSE;

  pinned = hdy_tab_page_get_pinned (self->selected_page);
  pos = (pinned ? self->n_pinned_pages : self->n_pages) - 1;

  page = hdy_tab_view_get_nth_page (self, pos);

  /* If we're on the last pinned tab, go to the last non-pinned tab */
  if (page == self->selected_page && pinned)
    page = hdy_tab_view_get_nth_page (self, self->n_pages - 1);

  if (page == self->selected_page)
    return FALSE;

  hdy_tab_view_set_selected_page (self, page);

  return TRUE;
}

/**
 * hdy_tab_view_get_default_icon:
 * @self: a #HdyTabView
 *
 * TBD
 *
 * Returns: (transfer none) (nullable): TBD
 *
 * Since: 1.2
 */
GIcon *
hdy_tab_view_get_default_icon (HdyTabView *self)
{
  g_return_val_if_fail (HDY_IS_TAB_VIEW (self), NULL);

  return self->default_icon;
}

/**
 * hdy_tab_view_set_default_icon:
 * @self: a #HdyTabView
 * @default_icon: (nullable): TBD
 *
 * TBD
 *
 * Since: 1.2
 */
void
hdy_tab_view_set_default_icon (HdyTabView *self,
                               GIcon      *default_icon)
{
  g_return_if_fail (HDY_IS_TAB_VIEW (self));
  g_return_if_fail (G_IS_ICON (default_icon));

  if (self->default_icon == default_icon)
    return;

  g_set_object (&self->default_icon, default_icon);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_DEFAULT_ICON]);
}

/**
 * hdy_tab_view_get_menu_model:
 * @self: a #HdyTabView
 *
 * TBD
 *
 * Returns: (transfer none) (nullable): TBD
 *
 * Since: 1.2
 */
GMenuModel *
hdy_tab_view_get_menu_model (HdyTabView *self)
{
  g_return_val_if_fail (HDY_IS_TAB_VIEW (self), NULL);

  return self->menu_model;
}

/**
 * hdy_tab_view_set_menu_model:
 * @self: a #HdyTabView
 * @menu_model: (nullable): TBD
 *
 * TBD
 *
 * Since: 1.2
 */
void
hdy_tab_view_set_menu_model (HdyTabView *self,
                             GMenuModel *menu_model)
{
  g_return_if_fail (HDY_IS_TAB_VIEW (self));
  g_return_if_fail (G_IS_MENU_MODEL (menu_model));

  if (self->menu_model == menu_model)
    return;

  g_set_object (&self->menu_model, menu_model);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MENU_MODEL]);
}

/**
 * hdy_tab_view_get_group:
 * @self: a #HdyTabView
 *
 * TBD
 *
 * Returns: (element-type HdyTabView) (nullable) (transfer none): TBD
 *
 * Since: 1.2
 */
GSList *
hdy_tab_view_get_group (HdyTabView *self)
{
  g_return_val_if_fail (HDY_IS_TAB_VIEW (self), NULL);

  return self->group;
}

/**
 * hdy_tab_view_set_group:
 * @self: a #HdyTabView
 * @group: (element-type HdyTabView) (nullable): TBD
 *
 * TBD
 *
 * Since: 1.2
 */
void
hdy_tab_view_set_group (HdyTabView *self,
                        GSList     *group)
{
  g_return_if_fail (HDY_IS_TAB_VIEW (self));

  if (g_slist_find (group, self))
    return;

  if (self->group) {
    GSList *l;

    self->group = g_slist_remove (self->group, self);

    for (l = self->group; l; l = l->next) {
      HdyTabView *view = l->data;

      view->group = self->group;
    }
  }

  self->group = g_slist_prepend (group, self);

  if (group) {
    GSList *l;

    for (l = group; l; l = l->next) {
      HdyTabView *view = l->data;

      view->group = self->group;
    }
  }
}

/**
 * hdy_tab_view_join_group:
 * @self: a #HdyTabView
 * @source: TBD
 *
 * TBD
 *
 * Since: 1.2
 */
void
hdy_tab_view_join_group (HdyTabView *self,
                         HdyTabView *source)
{
  g_return_if_fail (HDY_IS_TAB_VIEW (self));
  g_return_if_fail (source == NULL || HDY_IS_TAB_VIEW (source));

  if (source) {
    GSList *group = hdy_tab_view_get_group (source);

    if (!group) {
        hdy_tab_view_set_group (source, NULL);
        group = hdy_tab_view_get_group (source);
      }

    hdy_tab_view_set_group (self, group);
  } else
    hdy_tab_view_set_group (self, NULL);
}

/**
 * hdy_tab_view_set_page_pinned:
 * @self: a #HdyTabView
 * @page: TBD
 * @pinned: TBD
 *
 * TBD
 *
 * Since: 1.2
 */
void
hdy_tab_view_set_page_pinned (HdyTabView *self,
                              HdyTabPage *page,
                              gboolean    pinned)
{
  guint pos;

  g_return_if_fail (HDY_IS_TAB_VIEW (self));
  g_return_if_fail (HDY_IS_TAB_PAGE (page));

  pinned = !!pinned;

  if (hdy_tab_page_get_pinned (page) == pinned)
    return;

  pos = hdy_tab_view_get_page_position (self, page);

  g_list_store_remove (self->pages, pos);

  pos = self->n_pinned_pages;

  if (!pinned)
      pos--;

  g_list_store_insert (self->pages, pos, page);

  set_page_pinned (page, pinned);

  if (pinned)
    pos++;

  gtk_container_child_set (GTK_CONTAINER (self->stack),
                           hdy_tab_page_get_content (page),
                           "position", self->n_pinned_pages,
                           NULL);

  set_n_pinned_pages (self, pos);

  if (pinned)
    g_signal_emit (self, signals[SIGNAL_PAGE_PINNED], 0, page);
  else
    g_signal_emit (self, signals[SIGNAL_PAGE_UNPINNED], 0, page);
}

/**
 * hdy_tab_view_get_page:
 * @self: a #HdyTabView
 * @content: TBD
 *
 * TBD
 *
 * Returns: (transfer none): TBD
 *
 * Since: 1.2
 */
HdyTabPage *
hdy_tab_view_get_page (HdyTabView *self,
                       GtkWidget  *content)
{
  guint i;

  g_return_val_if_fail (HDY_IS_TAB_VIEW (self), NULL);
  g_return_val_if_fail (GTK_IS_WIDGET (content), NULL);

  for (i = 0; i < self->n_pages; i++) {
    HdyTabPage *page = g_list_model_get_item (G_LIST_MODEL (self->pages), i);

    if (hdy_tab_page_get_content (page) == content)
      return page;
  }

  return NULL;
}

/**
 * hdy_tab_view_get_nth_page:
 * @self: a #HdyTabView
 * @position: TBD
 *
 * TBD
 *
 * Returns: (transfer none): TBD
 *
 * Since: 1.2
 */
HdyTabPage *
hdy_tab_view_get_nth_page (HdyTabView *self,
                           guint       position)
{
  g_return_val_if_fail (HDY_IS_TAB_VIEW (self), NULL);
  g_return_val_if_fail (position <= self->n_pages, NULL);

  return g_list_model_get_item (G_LIST_MODEL (self->pages), position);
}

/**
 * hdy_tab_view_get_page_position:
 * @self: a #HdyTabView
 * @page: TBD
 *
 * TBD
 *
 * Returns: TBD
 *
 * Since: 1.2
 */
guint
hdy_tab_view_get_page_position (HdyTabView *self,
                                HdyTabPage *page)
{
  guint pos;

  g_return_val_if_fail (HDY_IS_TAB_VIEW (self), 0);
  g_return_val_if_fail (HDY_IS_TAB_PAGE (page), 0);

  g_return_val_if_fail (g_list_store_find (self->pages, page, &pos), 0);

  return pos;
}

/**
 * hdy_tab_view_insert:
 * @self: a #HdyTabView
 * @content: TBD
 * @position: TBD
 *
 * TBD
 *
 * Returns: (transfer none): TBD
 *
 * Since: 1.2
 */
HdyTabPage *
hdy_tab_view_insert (HdyTabView *self,
                     GtkWidget  *content,
                     guint       position)
{
  g_return_val_if_fail (HDY_IS_TAB_VIEW (self), NULL);
  g_return_val_if_fail (GTK_IS_WIDGET (content), NULL);
  g_return_val_if_fail (position <= self->n_pages, NULL);

  return insert_page (self, content, position, FALSE);
}

/**
 * hdy_tab_view_prepend:
 * @self: a #HdyTabView
 * @content: TBD
 *
 * TBD
 *
 * Returns: (transfer none): TBD
 *
 * Since: 1.2
 */
HdyTabPage *
hdy_tab_view_prepend (HdyTabView *self,
                      GtkWidget  *content)
{
  g_return_val_if_fail (HDY_IS_TAB_VIEW (self), NULL);
  g_return_val_if_fail (GTK_IS_WIDGET (content), NULL);

  return insert_page (self, content, 0, FALSE);
}

/**
 * hdy_tab_view_append:
 * @self: a #HdyTabView
 * @content: TBD
 *
 * TBD
 *
 * Returns: (transfer none): TBD
 *
 * Since: 1.2
 */
HdyTabPage *
hdy_tab_view_append (HdyTabView *self,
                     GtkWidget  *content)
{
  g_return_val_if_fail (HDY_IS_TAB_VIEW (self), NULL);
  g_return_val_if_fail (GTK_IS_WIDGET (content), NULL);

  return insert_page (self, content, self->n_pages - self->n_pinned_pages, FALSE);
}

/**
 * hdy_tab_view_insert_pinned:
 * @self: a #HdyTabView
 * @content: TBD
 * @position: TBD
 *
 * TBD
 *
 * Returns: (transfer none): TBD
 *
 * Since: 1.2
 */
HdyTabPage *
hdy_tab_view_insert_pinned (HdyTabView *self,
                            GtkWidget  *content,
                            guint       position)
{
  g_return_val_if_fail (HDY_IS_TAB_VIEW (self), NULL);
  g_return_val_if_fail (GTK_IS_WIDGET (content), NULL);
  g_return_val_if_fail (position <= self->n_pages, NULL);

  return insert_page (self, content, position, TRUE);
}

/**
 * hdy_tab_view_prepend_pinned:
 * @self: a #HdyTabView
 * @content: TBD
 *
 * TBD
 *
 * Returns: (transfer none): TBD
 *
 * Since: 1.2
 */
HdyTabPage *
hdy_tab_view_prepend_pinned (HdyTabView *self,
                             GtkWidget  *content)
{
  g_return_val_if_fail (HDY_IS_TAB_VIEW (self), NULL);
  g_return_val_if_fail (GTK_IS_WIDGET (content), NULL);

  return insert_page (self, content, 0, TRUE);
}

/**
 * hdy_tab_view_append_pinned:
 * @self: a #HdyTabView
 * @content: TBD
 *
 * TBD
 *
 * Returns: (transfer none): TBD
 *
 * Since: 1.2
 */
HdyTabPage *
hdy_tab_view_append_pinned (HdyTabView *self,
                            GtkWidget  *content)
{
  g_return_val_if_fail (HDY_IS_TAB_VIEW (self), NULL);
  g_return_val_if_fail (GTK_IS_WIDGET (content), NULL);

  return insert_page (self, content, self->n_pinned_pages, TRUE);
}

/**
 * hdy_tab_view_close_page:
 * @self: a #HdyTabView
 * @page: TBD
 *
 * TBD
 *
 * Returns: TBD
 *
 * Since: 1.2
 */
gboolean
hdy_tab_view_close_page (HdyTabView *self,
                         HdyTabPage *page)
{
  gboolean can_close;

  g_return_val_if_fail (HDY_IS_TAB_VIEW (self), FALSE);
  g_return_val_if_fail (HDY_IS_TAB_PAGE (page), FALSE);

//  g_signal_emit_by_name (page, "closing", &can_close);
  can_close = TRUE;

  if (can_close)
    close_page (self, page);

  return can_close;
}

/**
 * hdy_tab_view_close_pages:
 * @self: a #HdyTabView
 * @pages: (element-type HdyTabPage) (transfer full): TBD
 *
 * TBD
 *
 * Returns: TBD
 *
 * Since: 1.2
 */
gboolean
hdy_tab_view_close_pages (HdyTabView *self,
                          GSList     *pages)
{
  gboolean can_close = TRUE;
  GSList *l;

  g_return_val_if_fail (HDY_IS_TAB_VIEW (self), FALSE);

  if (!pages)
    return TRUE;

  for (l = pages; l; l = l->next) {
    HdyTabPage *page = l->data;
    gboolean can_close_tab;

//    g_signal_emit_by_name (page, "closing", &can_close_tab);
    can_close_tab = TRUE;

    can_close &= can_close_tab;
  }

  if (can_close)
    for (l = pages; l; l = l->next) {
      HdyTabPage *page = l->data;

      close_page (self, page);
    }

  g_free (pages);

  return can_close;
}

/**
 * hdy_tab_view_close_other_pages:
 * @self: a #HdyTabView
 * @page: TBD
 *
 * TBD
 *
 * Returns: TBD
 *
 * Since: 1.2
 */
gboolean
hdy_tab_view_close_other_pages (HdyTabView *self,
                                HdyTabPage *page)
{
  GSList *pages = NULL;
  guint i;

  g_return_val_if_fail (HDY_IS_TAB_VIEW (self), FALSE);
  g_return_val_if_fail (HDY_IS_TAB_PAGE (page), FALSE);

  for (i = self->n_pinned_pages; i < self->n_pages; i++) {
    HdyTabPage *p = hdy_tab_view_get_nth_page (self, i);

    if (p == page)
      continue;

    pages = g_slist_prepend (pages, p);
  }

  pages = g_slist_reverse (pages);

  return hdy_tab_view_close_pages (self, pages);
}

/**
 * hdy_tab_view_close_pages_before:
 * @self: a #HdyTabView
 * @page: TBD
 *
 * TBD
 *
 * Returns: TBD
 *
 * Since: 1.2
 */
gboolean
hdy_tab_view_close_pages_before (HdyTabView *self,
                                 HdyTabPage *page)
{
  GSList *pages = NULL;
  guint pos, i;

  g_return_val_if_fail (HDY_IS_TAB_VIEW (self), FALSE);
  g_return_val_if_fail (HDY_IS_TAB_PAGE (page), FALSE);

  pos = hdy_tab_view_get_page_position (self, page);

  for (i = self->n_pinned_pages; i < pos; i++) {
    HdyTabPage *p = hdy_tab_view_get_nth_page (self, i);

    pages = g_slist_prepend (pages, p);
  }

  pages = g_slist_reverse (pages);

  return hdy_tab_view_close_pages (self, pages);
}

/**
 * hdy_tab_view_close_pages_after:
 * @self: a #HdyTabView
 * @page: TBD
 *
 * TBD
 *
 * Returns: TBD
 *
 * Since: 1.2
 */
gboolean
hdy_tab_view_close_pages_after (HdyTabView *self,
                                HdyTabPage *page)
{
  GSList *pages = NULL;
  guint pos, i;

  g_return_val_if_fail (HDY_IS_TAB_VIEW (self), FALSE);
  g_return_val_if_fail (HDY_IS_TAB_PAGE (page), FALSE);

  pos = hdy_tab_view_get_page_position (self, page);

  /* Skip pinned tabs */
  pos = MAX (self->n_pinned_pages, pos);

  for (i = self->n_pages - 1; i > pos; i--) {
    HdyTabPage *p = hdy_tab_view_get_nth_page (self, i);

    pages = g_slist_prepend (pages, p);
  }

  return hdy_tab_view_close_pages (self, pages);
}

/**
 * hdy_tab_view_reorder_page:
 * @self: a #HdyTabView
 * @page: TBD
 * @position: TBD
 *
 * TBD
 *
 * Returns: TBD
 *
 * Since: 1.2
 */
gboolean
hdy_tab_view_reorder_page (HdyTabView *self,
                           HdyTabPage *page,
                           guint       position)
{
  guint original_pos, pinned;

  g_return_val_if_fail (HDY_IS_TAB_VIEW (self), FALSE);
  g_return_val_if_fail (HDY_IS_TAB_PAGE (page), FALSE);

  pinned = hdy_tab_page_get_pinned (page);

  g_return_val_if_fail (!pinned || position < self->n_pinned_pages, FALSE);
  g_return_val_if_fail (pinned || position >= self->n_pinned_pages, FALSE);
  g_return_val_if_fail (pinned || position < self->n_pages, FALSE);

  original_pos = hdy_tab_view_get_page_position (self, page);

  if (original_pos == position)
    return FALSE;

  g_list_store_remove (self->pages, original_pos);
  g_list_store_insert (self->pages, position, page);

  gtk_container_child_set (GTK_CONTAINER (self->stack),
                           hdy_tab_page_get_content (page),
                           "position", position,
                           NULL);

  g_signal_emit (self, signals[SIGNAL_PAGE_REORDERED], 0, page, position);

  return TRUE;
}

/**
 * hdy_tab_view_reorder_backward:
 * @self: a #HdyTabView
 * @page: TBD
 *
 * TBD
 *
 * Returns: TBD
 *
 * Since: 1.2
 */
gboolean
hdy_tab_view_reorder_backward (HdyTabView *self,
                               HdyTabPage *page)
{
  gboolean pinned;
  guint pos, first;

  g_return_val_if_fail (HDY_IS_TAB_VIEW (self), FALSE);
  g_return_val_if_fail (HDY_IS_TAB_PAGE (page), FALSE);

  pos = hdy_tab_view_get_page_position (self, page);

  pinned = hdy_tab_page_get_pinned (page);
  first = pinned ? 0 : self->n_pinned_pages;

  if (pos <= first)
    return FALSE;

  return hdy_tab_view_reorder_page (self, page, pos - 1);
}

/**
 * hdy_tab_view_reorder_forward:
 * @self: a #HdyTabView
 * @page: TBD
 *
 * TBD
 *
 * Returns: TBD
 *
 * Since: 1.2
 */
gboolean
hdy_tab_view_reorder_forward (HdyTabView *self,
                              HdyTabPage *page)
{
  gboolean pinned;
  guint pos, last;

  g_return_val_if_fail (HDY_IS_TAB_VIEW (self), FALSE);
  g_return_val_if_fail (HDY_IS_TAB_PAGE (page), FALSE);

  pos = hdy_tab_view_get_page_position (self, page);

  pinned = hdy_tab_page_get_pinned (page);
  last = (pinned ? self->n_pinned_pages : self->n_pages) - 1;

  if (pos >= last)
    return FALSE;

  return hdy_tab_view_reorder_page (self, page, pos + 1);
}

/**
 * hdy_tab_view_reorder_first:
 * @self: a #HdyTabView
 * @page: TBD
 *
 * TBD
 *
 * Returns: TBD
 *
 * Since: 1.2
 */
gboolean
hdy_tab_view_reorder_first (HdyTabView *self,
                            HdyTabPage *page)
{
  gboolean pinned;
  guint pos;

  g_return_val_if_fail (HDY_IS_TAB_VIEW (self), FALSE);
  g_return_val_if_fail (HDY_IS_TAB_PAGE (page), FALSE);

  pinned = hdy_tab_page_get_pinned (page);
  pos = pinned ? 0 : self->n_pinned_pages;

  return hdy_tab_view_reorder_page (self, page, pos);
}

/**
 * hdy_tab_view_reorder_last:
 * @self: a #HdyTabView
 * @page: TBD
 *
 * TBD
 *
 * Returns: TBD
 *
 * Since: 1.2
 */
gboolean
hdy_tab_view_reorder_last (HdyTabView *self,
                           HdyTabPage *page)
{
  gboolean pinned;
  guint pos;

  g_return_val_if_fail (HDY_IS_TAB_VIEW (self), FALSE);
  g_return_val_if_fail (HDY_IS_TAB_PAGE (page), FALSE);

  pinned = hdy_tab_page_get_pinned (page);
  pos = (pinned ? self->n_pinned_pages : self->n_pages) - 1;

  return hdy_tab_view_reorder_page (self, page, pos);
}

void
hdy_tab_view_detach_page (HdyTabView *self,
                          HdyTabPage *page)
{
  g_return_if_fail (HDY_IS_TAB_VIEW (self));
  g_return_if_fail (HDY_IS_TAB_PAGE (page));

  detach_page (self, page);
}

void
hdy_tab_view_attach_page (HdyTabView *self,
                          HdyTabPage *page,
                          guint       position)
{
  g_return_if_fail (HDY_IS_TAB_VIEW (self));
  g_return_if_fail (HDY_IS_TAB_PAGE (page));
  g_return_if_fail (position <= self->n_pages);

  attach_page (self, page, position);

  hdy_tab_view_set_selected_page (self, page);
}

/**
 * hdy_tab_view_transfer_page:
 * @self: a #HdyTabView
 * @page: TBD
 * @other_view: TBD
 * @position: TBD
 *
 * TBD
 *
 * Since: 1.2
 */
void
hdy_tab_view_transfer_page (HdyTabView *self,
                            HdyTabPage *page,
                            HdyTabView *other_view,
                            guint       position)
{
  g_return_if_fail (HDY_IS_TAB_VIEW (self));
  g_return_if_fail (HDY_IS_TAB_PAGE (page));
  g_return_if_fail (HDY_IS_TAB_VIEW (other_view));
  g_return_if_fail (position <= other_view->n_pages);

  hdy_tab_view_detach_page (self, page);
  hdy_tab_view_attach_page (other_view, page, position);
}

/**
 * hdy_tab_view_get_pages:
 * @self: a #HdyTabView
 *
 * TBD
 *
 * Returns: (transfer none): TBD
 *
 * Since: 1.2
 */
GListModel *
hdy_tab_view_get_pages (HdyTabView *self)
{
  g_return_val_if_fail (HDY_IS_TAB_VIEW (self), NULL);

  return G_LIST_MODEL (self->pages);
}

