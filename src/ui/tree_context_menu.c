#include "ui/tree_context_menu.h"

#include <gtk/gtk.h>

#include "app/app.h"
#include "document/tabs.h"
#include "infra/util.h"
#include "ui/file_tree_view.h"
#include "workspace/workspace.h"

static GtkWidget *tree_context_popover;

static void
tree_context_clear(LmmeApp *app)
{
    g_clear_pointer(&app->tree_context.path, g_free);
    app->tree_context.kind = LMME_FILE_KIND_OTHER;
    app->tree_context.empty_area = FALSE;
}

static void
tree_context_set(LmmeApp *app, const char *path, LmmeFileKind kind, gboolean empty_area)
{
    tree_context_clear(app);
    app->tree_context.empty_area = empty_area;
    if (empty_area || path == NULL) {
        return;
    }

    app->tree_context.path = g_strdup(path);
    app->tree_context.kind = kind;
}

static void
sync_selected_path_from_tree(LmmeApp *app, const char *path, LmmeFileKind kind)
{
    g_clear_pointer(&app->selection.path, g_free);
    app->selection.kind = LMME_FILE_KIND_OTHER;
    app->selection.empty_area = FALSE;
    if (path == NULL) {
        return;
    }

    app->selection.path = g_strdup(path);
    app->selection.kind = kind;
}

static gboolean
tree_select_path_at_position(LmmeApp *app, double x, double y)
{
    g_autofree char *file_path = NULL;
    LmmeFileKind kind = LMME_FILE_KIND_OTHER;

    if (!lmme_file_tree_select_at(app->tree_view, x, y, &file_path, &kind)) {
        sync_selected_path_from_tree(app, NULL, LMME_FILE_KIND_OTHER);
        tree_context_set(app, NULL, LMME_FILE_KIND_OTHER, TRUE);
        return FALSE;
    }

    sync_selected_path_from_tree(app, file_path, kind);
    tree_context_set(app, file_path, kind, FALSE);
    return TRUE;
}

char *
lmme_tree_context_menu_dup_relative_path(const LmmeApp *app)
{
    if (app == NULL || app->workspace == NULL || app->tree_context.path == NULL) {
        return NULL;
    }

    return lmme_path_relative_to(app->workspace->path, app->tree_context.path);
}

static GMenuModel *
create_tree_context_menu_model(LmmeApp *app)
{
    GMenu *menu = g_menu_new();
    gboolean has_workspace = app != NULL && app->workspace != NULL;
    gboolean is_empty = app != NULL && app->tree_context.empty_area;
    gboolean is_dir = app != NULL && lmme_path_context_is_directory(&app->tree_context);
    gboolean is_markdown = app != NULL && lmme_path_context_is_markdown(&app->tree_context);
    gboolean is_image = app != NULL && lmme_path_context_is_image(&app->tree_context);
    gboolean tab_open = FALSE;
    gboolean is_workspace_root = FALSE;

    if (app == NULL) {
        return G_MENU_MODEL(menu);
    }
    if (has_workspace && app->tree_context.path != NULL) {
        is_workspace_root = g_strcmp0(app->tree_context.path, app->workspace->path) == 0;
        tab_open = lmme_tabs_find_by_path(app, app->tree_context.path) != NULL;
    }
    if (is_empty) {
        if (has_workspace) {
            g_menu_append(menu, "New Markdown File", "app.tree-new-file");
            g_menu_append(menu, "New Folder", "app.tree-new-folder");
        }
        return G_MENU_MODEL(menu);
    }
    if (is_markdown) {
        GMenu *section = g_menu_new();
        g_menu_append(section, "Open", "app.tree-open");
        if (tab_open) {
            g_menu_append(section, "Close Tab", "app.tree-close-tab");
        }
        g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
        g_object_unref(section);
    }
    if (has_workspace) {
        GMenu *section = g_menu_new();
        g_menu_append(section, "New Markdown File", "app.tree-new-file");
        g_menu_append(section, "New Folder", "app.tree-new-folder");
        g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
        g_object_unref(section);
    }
    if (is_dir || is_markdown || is_image) {
        GMenu *section = g_menu_new();
        g_menu_append(section, "Rename", "app.tree-rename");
        if (!is_workspace_root) {
            g_menu_append(section, "Delete", "app.tree-delete");
        }
        g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
        g_object_unref(section);
    }
    if (app->tree_context.path != NULL) {
        GMenu *copy_section = g_menu_new();
        GMenu *reveal_section = g_menu_new();
        g_menu_append(copy_section, "Copy Relative Path", "app.tree-copy-relative-path");
        g_menu_append(copy_section, "Copy Full Path", "app.tree-copy-full-path");
        if (is_image) {
            g_menu_append(copy_section, "Copy Markdown Image Link", "app.tree-copy-markdown-image-link");
        }
        g_menu_append_section(menu, NULL, G_MENU_MODEL(copy_section));
        g_menu_append(reveal_section, "Open Containing Folder", "app.tree-open-containing-folder");
        g_menu_append_section(menu, NULL, G_MENU_MODEL(reveal_section));
        g_object_unref(copy_section);
        g_object_unref(reveal_section);
    }
    return G_MENU_MODEL(menu);
}

static void
prepare_popover_widget(GtkWidget *widget)
{
    if (widget == NULL) {
        return;
    }
    if (g_strcmp0(G_OBJECT_TYPE_NAME(widget), "GtkModelButton") == 0) {
        gtk_widget_set_hexpand(widget, TRUE);
        gtk_widget_set_halign(widget, GTK_ALIGN_FILL);
    }
    for (GtkWidget *child = gtk_widget_get_first_child(widget); child != NULL; child = gtk_widget_get_next_sibling(child)) {
        prepare_popover_widget(child);
    }
}

static void
configure_popover(GtkWidget *popover)
{
    GtkWidget *child = popover != NULL ? gtk_popover_get_child(GTK_POPOVER(popover)) : NULL;
    if (child != NULL && GTK_IS_SCROLLED_WINDOW(child)) {
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(child), GTK_POLICY_NEVER, GTK_POLICY_NEVER);
        gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(child), TRUE);
        gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(child), TRUE);
        prepare_popover_widget(child);
    }
}

static GtkWidget *
get_tree_context_popover(void)
{
    if (tree_context_popover == NULL) {
        g_autoptr(GMenu) empty_menu = g_menu_new();
        tree_context_popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(empty_menu));
        gtk_popover_set_has_arrow(GTK_POPOVER(tree_context_popover), FALSE);
        gtk_popover_set_autohide(GTK_POPOVER(tree_context_popover), TRUE);
        configure_popover(tree_context_popover);
    }
    return tree_context_popover;
}

static void
on_tree_right_click(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data)
{
    LmmeApp *app = user_data;
    GtkWidget *popover = NULL;
    GtkWidget *anchor = NULL;
    GdkRectangle rect;
    graphene_point_t source_point;
    graphene_point_t destination_point;
    g_autoptr(GMenuModel) menu_model = NULL;

    (void)gesture;
    if (app == NULL || app->tree_view == NULL || app->window == NULL || app->root_box == NULL || n_press != 1) {
        return;
    }

    tree_select_path_at_position(app, x, y);
    menu_model = create_tree_context_menu_model(app);
    if (g_menu_model_get_n_items(menu_model) == 0) {
        return;
    }

    popover = get_tree_context_popover();
    gtk_popover_menu_set_menu_model(GTK_POPOVER_MENU(popover), menu_model);
    configure_popover(popover);
    anchor = app->root_box;
    if (gtk_widget_get_parent(popover) != anchor) {
        if (gtk_widget_get_parent(popover) != NULL) {
            gtk_widget_unparent(popover);
        }
        gtk_widget_set_parent(popover, anchor);
    }
    if (gtk_widget_get_visible(popover)) {
        gtk_popover_popdown(GTK_POPOVER(popover));
    }

    graphene_point_init(&source_point, (float)x, (float)y);
    if (!gtk_widget_compute_point(app->tree_view, anchor, &source_point, &destination_point)) {
        return;
    }
    rect.x = (int)destination_point.x;
    rect.y = (int)destination_point.y;
    rect.width = 1;
    rect.height = 1;
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
    gtk_popover_popup(GTK_POPOVER(popover));
    gtk_popover_present(GTK_POPOVER(popover));
}

void
lmme_tree_context_menu_attach(LmmeApp *app)
{
    GtkGesture *gesture = gtk_gesture_click_new();

    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_SECONDARY);
    g_signal_connect(gesture, "pressed", G_CALLBACK(on_tree_right_click), app);
    gtk_widget_add_controller(app->tree_view, GTK_EVENT_CONTROLLER(gesture));
}
