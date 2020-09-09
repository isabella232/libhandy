#include "hdy-tab-view-demo-window.h"

#include <glib/gi18n.h>

struct _HdyTabViewDemoWindow
{
  HdyApplicationWindow parent_instance;
  HdyTabView *view;

  GActionMap *tab_action_group;

  HdyTabPage *menu_page;
};

G_DEFINE_TYPE (HdyTabViewDemoWindow, hdy_tab_view_demo_window, HDY_TYPE_APPLICATION_WINDOW)

static void
window_new (GSimpleAction *action,
            GVariant      *parameter,
            gpointer       user_data)
{
  HdyTabViewDemoWindow *self = HDY_TAB_VIEW_DEMO_WINDOW (user_data);
  GtkApplication *app = gtk_window_get_application (GTK_WINDOW (self));
  HdyTabViewDemoWindow *window = hdy_tab_view_demo_window_new (app);

  hdy_tab_view_demo_window_prepopulate (window);

  gtk_window_present (GTK_WINDOW (window));
}

static GIcon *
get_random_icon (void)
{
  GtkIconTheme *theme = gtk_icon_theme_get_default ();
  GList *list;
  gint index;
  GIcon *icon;

  list = gtk_icon_theme_list_icons (theme, "MimeTypes");

  index = g_random_int_range (0, g_list_length (list));
  icon = g_themed_icon_new (g_list_nth_data (list, index));

  g_list_free_full (list, g_free);

  return icon;
}

static gboolean
text_to_tooltip (GBinding     *binding,
                 const GValue *input,
                 GValue       *output,
                 gpointer      user_data)
{
  const gchar *title = g_value_get_string (input);
  gchar *tooltip = g_markup_printf_escaped ("An elaborate tooltip for <b>%s</b>", title);

  g_value_take_string (output, tooltip);

  return TRUE;
}

static void
tab_new (GSimpleAction *action,
         GVariant      *parameter,
         gpointer       user_data)
{
  HdyTabViewDemoWindow *self = HDY_TAB_VIEW_DEMO_WINDOW (user_data);
  g_autofree gchar *title = NULL;
  GtkWidget *content;
  HdyTabPage *page;
  GIcon *icon;
  static gint next_page = 1;

  title = g_strdup_printf ("Page %d", next_page);
  icon = get_random_icon ();

  content = g_object_new (GTK_TYPE_ENTRY,
                          "visible", TRUE,
                          "text", title,
                          "halign", GTK_ALIGN_CENTER,
                          "valign", GTK_ALIGN_CENTER,
                          NULL);

  page = hdy_tab_view_append (self->view, GTK_WIDGET (content));

  g_object_bind_property (content, "text",
                          page, "title",
                          G_BINDING_SYNC_CREATE);
  g_object_bind_property_full (content, "text",
                               page, "tooltip",
                               G_BINDING_SYNC_CREATE,
                               text_to_tooltip, NULL,
                               NULL, NULL);

  hdy_tab_page_set_icon (page, icon);
  hdy_tab_page_set_secondary_icon_activatable (page, TRUE);

  hdy_tab_view_set_selected_page (self->view, page);

  next_page++;
}

static HdyTabPage *
get_current_page (HdyTabViewDemoWindow *self)
{
  if (self->menu_page)
    return self->menu_page;

  return hdy_tab_view_get_selected_page (self->view);
}

static void
tab_pin (GSimpleAction *action,
         GVariant      *parameter,
         gpointer       user_data)
{
  HdyTabViewDemoWindow *self = HDY_TAB_VIEW_DEMO_WINDOW (user_data);

  hdy_tab_view_set_page_pinned (self->view, get_current_page (self), TRUE);
}

static void
tab_unpin (GSimpleAction *action,
           GVariant      *parameter,
           gpointer       user_data)
{
  HdyTabViewDemoWindow *self = HDY_TAB_VIEW_DEMO_WINDOW (user_data);

  hdy_tab_view_set_page_pinned (self->view, get_current_page (self), FALSE);
}

static void
tab_close (GSimpleAction *action,
           GVariant      *parameter,
           gpointer       user_data)
{
  HdyTabViewDemoWindow *self = HDY_TAB_VIEW_DEMO_WINDOW (user_data);

  hdy_tab_view_close_page (self->view, get_current_page (self));
}

static void
tab_close_other (GSimpleAction *action,
                 GVariant      *parameter,
                 gpointer       user_data)
{
  HdyTabViewDemoWindow *self = HDY_TAB_VIEW_DEMO_WINDOW (user_data);

  hdy_tab_view_close_other_pages (self->view, get_current_page (self));
}

static void
tab_close_left (GSimpleAction *action,
                GVariant      *parameter,
                gpointer       user_data)
{
  HdyTabViewDemoWindow *self = HDY_TAB_VIEW_DEMO_WINDOW (user_data);

  hdy_tab_view_close_pages_before (self->view, get_current_page (self));
}

static void
tab_close_right (GSimpleAction *action,
                 GVariant      *parameter,
                 gpointer       user_data)
{
  HdyTabViewDemoWindow *self = HDY_TAB_VIEW_DEMO_WINDOW (user_data);

  hdy_tab_view_close_pages_after (self->view, get_current_page (self));
}

static void
tab_move_to_new_window (GSimpleAction *action,
                        GVariant      *parameter,
                        gpointer       user_data)
{
  HdyTabViewDemoWindow *self = HDY_TAB_VIEW_DEMO_WINDOW (user_data);
  GtkApplication *app = gtk_window_get_application (GTK_WINDOW (self));
  HdyTabViewDemoWindow *window = hdy_tab_view_demo_window_new (app);

  hdy_tab_view_transfer_page (self->view,
                              self->menu_page,
                              window->view,
                              0);

  gtk_window_present (GTK_WINDOW (window));
}

static void
tab_change_needs_attention (GSimpleAction *action,
                            GVariant      *parameter,
                            gpointer       user_data)
{
  HdyTabViewDemoWindow *self = HDY_TAB_VIEW_DEMO_WINDOW (user_data);
  gboolean need_attention = g_variant_get_boolean (parameter);

  hdy_tab_page_set_needs_attention (get_current_page (self), need_attention);
  g_simple_action_set_state (action, g_variant_new_boolean (need_attention));
}

static void
tab_change_loading (GSimpleAction *action,
                    GVariant      *parameter,
                    gpointer       user_data)
{
  HdyTabViewDemoWindow *self = HDY_TAB_VIEW_DEMO_WINDOW (user_data);
  gboolean loading = g_variant_get_boolean (parameter);

  hdy_tab_page_set_loading (get_current_page (self), loading);
  g_simple_action_set_state (action, g_variant_new_boolean (loading));
}

static GIcon *
get_secondary_icon (HdyTabPage *page)
{
  gboolean muted;

  muted = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (page),
                                              "hdy-tab-view-demo-muted"));

  if (muted)
    return g_themed_icon_new ("ephy-audio-muted-symbolic");
  else
    return g_themed_icon_new ("ephy-audio-playing-symbolic");
}

static void
tab_change_secondary_icon (GSimpleAction *action,
                           GVariant      *parameter,
                           gpointer       user_data)
{
  HdyTabViewDemoWindow *self = HDY_TAB_VIEW_DEMO_WINDOW (user_data);
  gboolean secondary_icon = g_variant_get_boolean (parameter);
  g_autoptr (GIcon) icon = NULL;

  if (secondary_icon)
    icon = get_secondary_icon (get_current_page (self));

  hdy_tab_page_set_secondary_icon (get_current_page (self), icon);
  g_simple_action_set_state (action, g_variant_new_boolean (secondary_icon));
}

static GActionEntry action_entries[] = {
  { "window-new", window_new },
  { "tab-new", tab_new },
};

static GActionEntry tab_action_entries[] = {
  { "pin", tab_pin },
  { "unpin", tab_unpin },
  { "close", tab_close },
  { "close-other", tab_close_other },
  { "close-left", tab_close_left },
  { "close-right", tab_close_right },
  { "move-to-new-window", tab_move_to_new_window },
  { "needs-attention", NULL, NULL, "false", tab_change_needs_attention },
  { "loading", NULL, NULL, "false", tab_change_loading },
  { "secondary-icon", NULL, NULL, "false", tab_change_secondary_icon },
};

static inline void
set_tab_action_enabled (HdyTabViewDemoWindow *self,
                        const gchar          *name,
                        gboolean              enabled)
{
  GAction *action = g_action_map_lookup_action (self->tab_action_group, name);

  g_assert (G_IS_SIMPLE_ACTION (action));

  g_simple_action_set_enabled (G_SIMPLE_ACTION (action),
                               enabled);
}

static inline void
set_tab_action_state (HdyTabViewDemoWindow *self,
                      const gchar          *name,
                      gboolean              state)
{
  GAction *action = g_action_map_lookup_action (self->tab_action_group, name);

  g_assert (G_IS_SIMPLE_ACTION (action));

  g_simple_action_set_state (G_SIMPLE_ACTION (action),
                             g_variant_new_boolean (state));
}

static void
setup_menu_cb (HdyTabViewDemoWindow *self,
               HdyTabPage           *page,
               HdyTabView           *view)
{
  HdyTabPage *prev = NULL;
  gboolean can_close_left = TRUE, can_close_right = TRUE;
  gboolean pinned = FALSE, prev_pinned;
  guint n_pages, pos;

  self->menu_page = page;

  n_pages = hdy_tab_view_get_n_pages (self->view);

  if (page) {
    pos = hdy_tab_view_get_page_position (self->view, page);

    if (pos > 0)
      prev = hdy_tab_view_get_nth_page (self->view, pos - 1);

    pinned = hdy_tab_page_get_pinned (page);
    prev_pinned = prev && hdy_tab_page_get_pinned (prev);

    can_close_left = !pinned && prev && !prev_pinned;
    can_close_right = pos < n_pages - 1;
  }

  set_tab_action_enabled (self, "pin", !page || !pinned);
  set_tab_action_enabled (self, "unpin", !page || pinned);
  set_tab_action_enabled (self, "close", !page || !pinned);
  set_tab_action_enabled (self, "close-left", can_close_left);
  set_tab_action_enabled (self, "close-right", can_close_right);
  set_tab_action_enabled (self, "close-other", can_close_left || can_close_right);
  set_tab_action_enabled (self, "move-to-new-window", !page || (!pinned && n_pages > 1));

  if (page) {
    set_tab_action_state (self, "loading", hdy_tab_page_get_loading (page));
    set_tab_action_state (self, "needs-attention", hdy_tab_page_get_needs_attention (page));
    set_tab_action_state (self, "secondary-icon", hdy_tab_page_get_secondary_icon (page) != NULL);
  }
}

static HdyTabView *
create_window_cb (HdyTabViewDemoWindow *self)
{
  GtkApplication *app = gtk_window_get_application (GTK_WINDOW (self));
  HdyTabViewDemoWindow *window = hdy_tab_view_demo_window_new (app);

  gtk_window_set_position (GTK_WINDOW (window), GTK_WIN_POS_MOUSE);
  gtk_window_present (GTK_WINDOW (window));

  return window->view;
}

static void
secondary_icon_activated_cb (HdyTabViewDemoWindow *self,
                             HdyTabPage           *page)
{
  g_autoptr (GIcon) icon = NULL;
  gboolean muted;

  muted = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (page),
                                              "hdy-tab-view-demo-muted"));

  g_object_set_data (G_OBJECT (page),
                     "hdy-tab-view-demo-muted",
                     GINT_TO_POINTER (!muted));

  icon = get_secondary_icon (page);

  hdy_tab_page_set_secondary_icon (page, icon);
}

static void
hdy_tab_view_demo_window_class_init (HdyTabViewDemoWindowClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class, "/sm/puri/Handy/Demo/ui/hdy-tab-view-demo-window.ui");
  gtk_widget_class_bind_template_child (widget_class, HdyTabViewDemoWindow, view);
  gtk_widget_class_bind_template_callback (widget_class, setup_menu_cb);
  gtk_widget_class_bind_template_callback (widget_class, create_window_cb);
  gtk_widget_class_bind_template_callback (widget_class, secondary_icon_activated_cb);
}

static void
hdy_tab_view_demo_window_init (HdyTabViewDemoWindow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  g_action_map_add_action_entries (G_ACTION_MAP (self),
                                   action_entries,
                                   G_N_ELEMENTS (action_entries),
                                   self);

  self->tab_action_group = G_ACTION_MAP (g_simple_action_group_new ());
  g_action_map_add_action_entries (self->tab_action_group,
                                   tab_action_entries,
                                   G_N_ELEMENTS (tab_action_entries),
                                   self);

  gtk_widget_insert_action_group (GTK_WIDGET (self),
                                  "tab",
                                  G_ACTION_GROUP (self->tab_action_group));
}

HdyTabViewDemoWindow *
hdy_tab_view_demo_window_new (GtkApplication *app)
{
  g_return_val_if_fail (GTK_IS_APPLICATION (app), NULL);

  return g_object_new (HDY_TYPE_TAB_VIEW_DEMO_WINDOW,
                       "application", app,
                       NULL);
}

void
hdy_tab_view_demo_window_prepopulate (HdyTabViewDemoWindow *self)
{
  tab_new (NULL, NULL, self);
  tab_new (NULL, NULL, self);
  tab_new (NULL, NULL, self);
}
