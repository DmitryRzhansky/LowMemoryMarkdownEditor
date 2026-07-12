#include "ui/tree_context_menu.h"

#include <gtk/gtk.h>

#include "app/app.h"
#include "document/tabs.h"
#include "infra/util.h"
#include "ui/file_tree_view.h"
#include "workspace/workspace.h"

static GtkWidget *tree_context_popover;

static void
tree_context_set(LmmeApp *app, const char *path, LmmeFileKind kind, gboolean empty_area)
{
    lmme_path_context_set(&app->tree_context, path, kind, empty_area);
}

static void
sync_selected_path_from_tree(LmmeApp *app, const char *path, LmmeFileKind kind)
{
    lmme_path_context_set(&app->selection, path, kind, FALSE);
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
        g_menu_append(menu, "Open", "app.tree-open");
        if (tab_open) {
            g_menu_append(menu, "Close Tab", "app.tree-close-tab");
        }
    }
    if (has_workspace) {
        g_menu_append(menu, "New Markdown File", "app.tree-new-file");
        g_menu_append(menu, "New Folder", "app.tree-new-folder");
    }
    if (is_dir || is_markdown || is_image) {
        g_menu_append(menu, "Rename", "app.tree-rename");
        if (!is_workspace_root) {
            g_menu_append(menu, "Delete", "app.tree-delete");
        }
    }
    if (app->tree_context.path != NULL) {
        g_menu_append(menu, "Copy Relative Path", "app.tree-copy-relative-path");
        g_menu_append(menu, "Copy Full Path", "app.tree-copy-full-path");
        if (is_image) {
            g_menu_append(menu, "Copy Markdown Image Link", "app.tree-copy-markdown-image-link");
        }
        g_menu_append(menu, "Open Containing Folder", "app.tree-open-containing-folder");
    }
    return G_MENU_MODEL(menu);
}

static GtkWidget *
get_tree_context_popover(void)
{
    if (tree_context_popover == NULL) {
        g_autoptr(GMenu) empty_menu = g_menu_new();
        tree_context_popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(empty_menu));
        gtk_popover_set_has_arrow(GTK_POPOVER(tree_context_popover), FALSE);
        gtk_popover_set_autohide(GTK_POPOVER(tree_context_popover), TRUE);
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
}

void
lmme_tree_context_menu_attach(LmmeApp *app)
{
    GtkGesture *gesture = gtk_gesture_click_new();

    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_SECONDARY);
    g_signal_connect(gesture, "pressed", G_CALLBACK(on_tree_right_click), app);
    gtk_widget_add_controller(app->tree_view, GTK_EVENT_CONTROLLER(gesture));
}
