#include "ui/window.h"

#include "app/app.h"
#include "command/command_actions.h"
#include "document/document.h"
#include "document/recovery.h"
#include "document/tabs.h"
#include "editor/editor.h"
#include "infra/dialogs.h"
#include "infra/util.h"
#include "ui/file_tree_view.h"
#include "ui/menu.h"
#include "ui/search_bar.h"
#include "ui/statusbar.h"
#include "ui/toolbar.h"
#include "ui/tree_context_menu.h"
#include "workspace/workspace.h"

static void
load_css(void)
{
    GtkCssProvider *provider = gtk_css_provider_new();
    GdkDisplay *display = gdk_display_get_default();

    gtk_css_provider_load_from_resource(provider, "/org/lmme/lmme.css");
    gtk_style_context_add_provider_for_display(display,
                                               GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

static void
on_notebook_switch_page(GtkNotebook *notebook, GtkWidget *page, guint page_num, gpointer user_data)
{
    LmmeApp *app = user_data;
    (void)notebook;
    (void)page;
    (void)page_num;

    if (app->preview_enabled) {
        lmme_window_refresh_preview_now(app);
    } else {
        lmme_window_update_status(app);
    }
    {
        LmmeDocument *active = lmme_tabs_get_active(app);
        if (active != NULL) {
            lmme_document_request_stats_update(active);
        }
    }
    lmme_command_actions_refresh(app);
}

static gboolean
on_window_search_key_pressed(GtkEventControllerKey *controller,
                             guint keyval,
                             guint keycode,
                             GdkModifierType state,
                             gpointer user_data)
{
    (void)controller;
    (void)keycode;
    (void)state;
    return lmme_search_bar_handle_key_press(user_data, keyval);
}

static gboolean
on_window_close_request(GtkWindow *window, gpointer user_data)
{
    LmmeApp *app = user_data;
    (void)window;

    if (app->shutdown_in_progress) {
        return FALSE;
    }
    (void)lmme_app_request_shutdown(app);
    return TRUE;
}

static void
on_window_destroy(GtkWidget *widget, gpointer user_data)
{
    LmmeApp *app = user_data;

    if (app != NULL && app->window == widget) {
        lmme_app_clear_widget_refs(app);
    }
}

static void
restore_recovery_files(LmmeApp *app)
{
    g_autoptr(GError) error = NULL;
    GPtrArray *entries = lmme_recovery_list(app->recovery_store, &error);

    if (entries == NULL) {
        if (error != NULL) {
            lmme_dialog_error(GTK_WINDOW(app->window), "Could not inspect recovery data.", error->message);
        }
        return;
    }
    for (guint i = 0; i < entries->len; i++) {
        LmmeRecoveryEntry *entry = g_ptr_array_index(entries, i);
        g_autoptr(GError) changed_error = NULL;
        gboolean belongs_to_workspace = app->workspace != NULL &&
                                        lmme_recovery_entry_belongs_to_workspace(entry,
                                                                                 app->workspace->path);
        gboolean original_changed = FALSE;
        LmmeRecoveryChoice choice = LMME_RECOVERY_CHOICE_LATER;

        if (!belongs_to_workspace) {
            continue;
        }
        original_changed = lmme_recovery_entry_original_changed(entry, &changed_error);
        choice = lmme_dialog_choose_recovery(GTK_WINDOW(app->window),
                                             entry->original_path,
                                             original_changed);
        if (choice == LMME_RECOVERY_CHOICE_RESTORE) {
            g_autoptr(GError) open_error = NULL;
            if (!lmme_tabs_open_recovery_entry(app, entry, &open_error)) {
                lmme_dialog_error(GTK_WINDOW(app->window),
                                  "Could not restore recovery data.",
                                  open_error != NULL ? open_error->message : NULL);
            }
        } else if (choice == LMME_RECOVERY_CHOICE_DISCARD) {
            g_autoptr(GError) remove_error = NULL;
            if (!lmme_recovery_remove(app->recovery_store, entry->original_path, &remove_error)) {
                lmme_dialog_error(GTK_WINDOW(app->window),
                                  "Could not discard recovery data.",
                                  remove_error != NULL ? remove_error->message : NULL);
            }
        }
    }
    g_ptr_array_unref(entries);
}

static void
restore_workspace(LmmeApp *app)
{
    if (app->config.restore_last_workspace && app->config.last_workspace != NULL &&
        app->config.last_workspace[0] != '\0' && g_file_test(app->config.last_workspace, G_FILE_TEST_IS_DIR)) {
        lmme_window_open_workspace_path(app, app->config.last_workspace);
    }

}

static void
restore_tabs(LmmeApp *app)
{
    if (app->workspace != NULL && app->config.restore_tabs && app->config.open_tabs != NULL) {
        for (guint i = 0; i < app->config.open_tabs->len; i++) {
            const char *path = g_ptr_array_index(app->config.open_tabs, i);
            g_autoptr(GError) error = NULL;

            if (g_file_test(path, G_FILE_TEST_EXISTS) && lmme_path_is_inside(app->workspace->path, path) &&
                !lmme_tabs_open_file(app, path, &error)) {
                g_warning("Could not restore tab %s: %s", path, error != NULL ? error->message : "unknown error");
            }
        }
    }
}

void
lmme_window_build(LmmeApp *app)
{
    GMenuModel *menu_model = NULL;
    GtkEventController *search_key = NULL;

    load_css();
    lmme_editor_apply_font_css(&app->config);
    lmme_command_actions_register(app);

    app->window = gtk_application_window_new(app->gtk_app);
    gtk_widget_add_css_class(app->window, "lmme-window");
    g_signal_connect(app->window, "close-request", G_CALLBACK(on_window_close_request), app);
    g_signal_connect(app->window, "destroy", G_CALLBACK(on_window_destroy), app);
    gtk_window_set_title(GTK_WINDOW(app->window), "LowMemoryMarkdownEditor");
    gtk_window_set_default_size(GTK_WINDOW(app->window), app->config.window_width, app->config.window_height);
    if (app->config.window_maximized) {
        gtk_window_maximize(GTK_WINDOW(app->window));
    }

    app->root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(app->root_box, "app-root");
    gtk_window_set_child(GTK_WINDOW(app->window), app->root_box);
    menu_model = lmme_menu_create_model();
    app->menu_bar = gtk_popover_menu_bar_new_from_model(menu_model);
    gtk_widget_add_css_class(app->menu_bar, "app-menu");
    g_object_unref(menu_model);
    gtk_box_append(GTK_BOX(app->root_box), app->menu_bar);
    app->toolbar = lmme_toolbar_create();
    gtk_box_append(GTK_BOX(app->root_box), app->toolbar);

    app->main_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_add_css_class(app->main_paned, "main-paned");
    gtk_widget_set_vexpand(app->main_paned, TRUE);
    gtk_paned_set_shrink_start_child(GTK_PANED(app->main_paned), FALSE);
    gtk_box_append(GTK_BOX(app->root_box), app->main_paned);
    app->config.sidebar_width = MAX(app->config.sidebar_width, 220);
    app->sidebar = gtk_scrolled_window_new();
    gtk_widget_add_css_class(app->sidebar, "sidebar");
    gtk_widget_set_size_request(app->sidebar, app->config.sidebar_width, -1);
    app->tree_view = lmme_file_tree_create(app);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(app->sidebar), app->tree_view);
    gtk_paned_set_start_child(GTK_PANED(app->main_paned), app->sidebar);

    app->right_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(app->right_box, "content-area");
    gtk_paned_set_end_child(GTK_PANED(app->main_paned), app->right_box);
    app->breadcrumbs_label = gtk_label_new("No workspace opened");
    gtk_widget_add_css_class(app->breadcrumbs_label, "breadcrumbs");
    gtk_label_set_xalign(GTK_LABEL(app->breadcrumbs_label), 0.0f);
    gtk_label_set_single_line_mode(GTK_LABEL(app->breadcrumbs_label), TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(app->breadcrumbs_label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_margin_start(app->breadcrumbs_label, 8);
    gtk_widget_set_margin_top(app->breadcrumbs_label, 4);
    gtk_widget_set_margin_bottom(app->breadcrumbs_label, 4);
    gtk_box_append(GTK_BOX(app->right_box), app->breadcrumbs_label);
    app->notebook = gtk_notebook_new();
    gtk_widget_add_css_class(app->notebook, "editor-tabs");
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(app->notebook), TRUE);
    gtk_widget_set_hexpand(app->notebook, TRUE);
    gtk_widget_set_vexpand(app->notebook, TRUE);
    gtk_box_append(GTK_BOX(app->right_box), app->notebook);
    app->search_bar = lmme_search_bar_create(app);
    gtk_box_append(GTK_BOX(app->right_box), app->search_bar);

    search_key = gtk_event_controller_key_new();
    g_signal_connect(search_key, "key-pressed", G_CALLBACK(on_window_search_key_pressed), app);
    gtk_widget_add_controller(app->window, search_key);
    app->status_label = gtk_label_new("No workspace opened");
    gtk_label_set_xalign(GTK_LABEL(app->status_label), 0.0f);
    gtk_widget_add_css_class(app->status_label, "statusbar");
    gtk_box_append(GTK_BOX(app->root_box), app->status_label);

    lmme_tree_context_menu_attach(app);
    g_signal_connect(app->notebook, "switch-page", G_CALLBACK(on_notebook_switch_page), app);

    gtk_widget_set_visible(app->toolbar, app->config.show_toolbar && !app->focus_mode);
    gtk_widget_set_visible(app->sidebar, !app->focus_mode);
    gtk_widget_set_visible(app->status_label, app->config.show_statusbar);
    gtk_widget_set_visible(app->breadcrumbs_label, app->config.show_breadcrumbs);
    gtk_paned_set_position(GTK_PANED(app->main_paned), app->config.sidebar_width);
    restore_workspace(app);
    restore_recovery_files(app);
    restore_tabs(app);
    lmme_window_update_status(app);
}

gboolean
lmme_window_open_workspace_path(LmmeApp *app, const char *path)
{
    g_autoptr(GError) error = NULL;
    LmmeWorkspace *workspace = lmme_workspace_new_scanned(path,
                                                          app->config.show_hidden_files,
                                                          app->config.show_images,
                                                          &error);

    if (workspace == NULL) {
        lmme_dialog_error(GTK_WINDOW(app->window), "Could not open workspace.", error != NULL ? error->message : NULL);
        return FALSE;
    }
    if (app->documents != NULL && app->documents->len > 0 &&
        !lmme_tabs_prepare_close_all(app)) {
        lmme_workspace_free(workspace);
        return FALSE;
    }
    if (app->documents != NULL && app->documents->len > 0) {
        g_autoptr(GError) commit_error = NULL;
        if (!lmme_tabs_commit_close_all(app, &commit_error)) {
            lmme_workspace_free(workspace);
            lmme_dialog_error(GTK_WINDOW(app->window),
                              "Could not close open documents.",
                              commit_error != NULL ? commit_error->message : NULL);
            return FALSE;
        }
    }

    lmme_file_tree_populate(app->tree_view,
                            NULL,
                            app->config.show_hidden_files,
                            app->config.show_images);
    g_clear_pointer(&app->workspace, lmme_workspace_free);
    app->workspace = workspace;
    lmme_config_set_last_workspace(&app->config, app->workspace->path);
    lmme_window_refresh_tree(app);
    gtk_label_set_text(GTK_LABEL(app->breadcrumbs_label), app->workspace->path);
    lmme_window_update_status(app);
    lmme_command_actions_refresh(app);
    return TRUE;
}

void
lmme_window_refresh_tree(LmmeApp *app)
{
    if (app->workspace == NULL) {
        lmme_file_tree_populate(app->tree_view,
                                NULL,
                                app->config.show_hidden_files,
                                app->config.show_images);
        return;
    }
    lmme_file_tree_populate(app->tree_view,
                            app->workspace,
                            app->config.show_hidden_files,
                            app->config.show_images);
}

void
lmme_window_refresh_tree_directory(LmmeApp *app, const char *directory_path)
{
    g_autoptr(GError) error = NULL;

    if (app == NULL || app->workspace == NULL || directory_path == NULL) {
        return;
    }
    if (!lmme_file_tree_refresh_directory(app->tree_view, directory_path, &error)) {
        if (error != NULL && !g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
            lmme_dialog_error(GTK_WINDOW(app->window), "Could not refresh workspace.", error->message);
        }
        return;
    }
}

void
lmme_window_update_status(LmmeApp *app)
{
    lmme_statusbar_update(app);
}

void
lmme_window_set_status_error(LmmeApp *app, const char *message)
{
    lmme_statusbar_set_error(app, message);
}

static void
report_preview_result(LmmeApp *app, LmmePreviewApplyResult result)
{
    switch (result) {
    case LMME_PREVIEW_APPLY_SKIPPED_LARGE_FILE:
        lmme_window_set_status_error(app, "Preview styling skipped for large file");
        break;
    case LMME_PREVIEW_APPLY_FAILED:
        lmme_window_set_status_error(app, "Preview styling failed");
        break;
    case LMME_PREVIEW_APPLY_OK:
    default:
        lmme_window_update_status(app);
        break;
    }
}

static gboolean
preview_timeout_cb(gpointer user_data)
{
    LmmeApp *app = user_data;
    app->preview_timeout_id = 0;
    lmme_window_refresh_preview_now(app);
    return G_SOURCE_REMOVE;
}

void
lmme_window_schedule_preview(LmmeApp *app)
{
    if (app->preview_timeout_id != 0) {
        g_source_remove(app->preview_timeout_id);
        app->preview_timeout_id = 0;
    }
    if (app->preview_enabled) {
        app->preview_timeout_id = g_timeout_add(app->config.preview_update_delay_ms, preview_timeout_cb, app);
    }
}

void
lmme_window_refresh_preview_now(LmmeApp *app)
{
    LmmeDocument *doc = NULL;
    LmmePreviewApplyResult result = LMME_PREVIEW_APPLY_OK;

    if (app == NULL) {
        return;
    }
    if (app->preview_timeout_id != 0) {
        g_source_remove(app->preview_timeout_id);
        app->preview_timeout_id = 0;
    }
    if (!app->preview_enabled) {
        lmme_window_update_status(app);
        return;
    }

    doc = lmme_tabs_get_active(app);
    if (doc == NULL) {
        lmme_window_update_status(app);
        return;
    }
    if (!doc->preview_dirty) {
        lmme_document_update_preview_active_line(doc);
        lmme_window_update_status(app);
        return;
    }
    result = lmme_document_set_preview_visible(doc, TRUE);
    report_preview_result(app, result);
}

void
lmme_window_toggle_preview(LmmeApp *app)
{
    LmmePreviewApplyResult result = LMME_PREVIEW_APPLY_OK;

    app->preview_enabled = !app->preview_enabled;
    if (app->preview_timeout_id != 0) {
        g_source_remove(app->preview_timeout_id);
        app->preview_timeout_id = 0;
    }
    result = lmme_tabs_set_preview_visible(app, app->preview_enabled);
    report_preview_result(app, result);
}
