#include "window.h"

#include "autosave.h"
#include "dialogs.h"
#include "editor.h"
#include "file_tree.h"
#include "image_insert.h"
#include "preview.h"
#include "tabs.h"
#include "util.h"

static void action_open(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_save(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_quit(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_new_file(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_new_folder(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_rename(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_delete(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_toggle_sidebar(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_toggle_preview(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_focus_mode(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_close_tab(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_next_tab(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_previous_tab(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_undo(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_redo(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_cut(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_copy(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_paste(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_find(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_replace(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_bold(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_italic(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_insert_link(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_insert_image(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_about(GSimpleAction *action, GVariant *parameter, gpointer user_data);

static const GActionEntry app_actions[] = {
    {"open", action_open, NULL, NULL, NULL},
    {"save", action_save, NULL, NULL, NULL},
    {"quit", action_quit, NULL, NULL, NULL},
    {"new-file", action_new_file, NULL, NULL, NULL},
    {"new-folder", action_new_folder, NULL, NULL, NULL},
    {"rename", action_rename, NULL, NULL, NULL},
    {"delete", action_delete, NULL, NULL, NULL},
    {"toggle-sidebar", action_toggle_sidebar, NULL, NULL, NULL},
    {"toggle-preview", action_toggle_preview, NULL, NULL, NULL},
    {"focus-mode", action_focus_mode, NULL, NULL, NULL},
    {"close-tab", action_close_tab, NULL, NULL, NULL},
    {"next-tab", action_next_tab, NULL, NULL, NULL},
    {"previous-tab", action_previous_tab, NULL, NULL, NULL},
    {"undo", action_undo, NULL, NULL, NULL},
    {"redo", action_redo, NULL, NULL, NULL},
    {"cut", action_cut, NULL, NULL, NULL},
    {"copy", action_copy, NULL, NULL, NULL},
    {"paste", action_paste, NULL, NULL, NULL},
    {"find", action_find, NULL, NULL, NULL},
    {"replace", action_replace, NULL, NULL, NULL},
    {"bold", action_bold, NULL, NULL, NULL},
    {"italic", action_italic, NULL, NULL, NULL},
    {"insert-link", action_insert_link, NULL, NULL, NULL},
    {"insert-image", action_insert_image, NULL, NULL, NULL},
    {"about", action_about, NULL, NULL, NULL},
};

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

static GtkWidget *
make_button(const char *label, const char *action_name)
{
    GtkWidget *button = gtk_button_new_with_label(label);
    gtk_actionable_set_action_name(GTK_ACTIONABLE(button), action_name);
    return button;
}

static GMenuModel *
create_menu_model(void)
{
    GMenu *bar = g_menu_new();
    GMenu *file = g_menu_new();
    GMenu *edit = g_menu_new();
    GMenu *view = g_menu_new();
    GMenu *help = g_menu_new();

    g_menu_append(file, "Open Folder", "app.open");
    g_menu_append(file, "New File", "app.new-file");
    g_menu_append(file, "New Folder", "app.new-folder");
    g_menu_append(file, "Rename", "app.rename");
    g_menu_append(file, "Delete", "app.delete");
    g_menu_append(file, "Save", "app.save");
    g_menu_append(file, "Quit", "app.quit");

    g_menu_append(edit, "Undo", "app.undo");
    g_menu_append(edit, "Redo", "app.redo");
    g_menu_append(edit, "Cut", "app.cut");
    g_menu_append(edit, "Copy", "app.copy");
    g_menu_append(edit, "Paste", "app.paste");
    g_menu_append(edit, "Find", "app.find");
    g_menu_append(edit, "Replace", "app.replace");
    g_menu_append(edit, "Bold", "app.bold");
    g_menu_append(edit, "Italic", "app.italic");
    g_menu_append(edit, "Insert Link", "app.insert-link");
    g_menu_append(edit, "Insert Image", "app.insert-image");

    g_menu_append(view, "Toggle Sidebar", "app.toggle-sidebar");
    g_menu_append(view, "Toggle Preview", "app.toggle-preview");
    g_menu_append(view, "Focus Mode", "app.focus-mode");

    g_menu_append(help, "About", "app.about");

    g_menu_append_submenu(bar, "File", G_MENU_MODEL(file));
    g_menu_append_submenu(bar, "Edit", G_MENU_MODEL(edit));
    g_menu_append_submenu(bar, "View", G_MENU_MODEL(view));
    g_menu_append_submenu(bar, "Help", G_MENU_MODEL(help));

    g_object_unref(file);
    g_object_unref(edit);
    g_object_unref(view);
    g_object_unref(help);

    return G_MENU_MODEL(bar);
}

static GtkWidget *
create_toolbar(void)
{
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(toolbar, "toolbar");
    gtk_box_append(GTK_BOX(toolbar), make_button("Open", "app.open"));
    gtk_box_append(GTK_BOX(toolbar), make_button("New File", "app.new-file"));
    gtk_box_append(GTK_BOX(toolbar), make_button("New Folder", "app.new-folder"));
    gtk_box_append(GTK_BOX(toolbar), make_button("Preview", "app.toggle-preview"));
    gtk_box_append(GTK_BOX(toolbar), make_button("Insert Image", "app.insert-image"));
    gtk_box_append(GTK_BOX(toolbar), make_button("Save", "app.save"));
    return toolbar;
}

static void
set_accels(LmmeApp *app)
{
    gtk_application_set_accels_for_action(app->gtk_app, "app.open", (const char *[]){"<Ctrl>O", NULL});
    gtk_application_set_accels_for_action(app->gtk_app, "app.save", (const char *[]){"<Ctrl>S", NULL});
    gtk_application_set_accels_for_action(app->gtk_app, "app.new-file", (const char *[]){"<Ctrl>N", NULL});
    gtk_application_set_accels_for_action(app->gtk_app, "app.new-folder", (const char *[]){"<Ctrl><Shift>N", NULL});
    gtk_application_set_accels_for_action(app->gtk_app, "app.close-tab", (const char *[]){"<Ctrl>W", NULL});
    gtk_application_set_accels_for_action(app->gtk_app, "app.undo", (const char *[]){"<Ctrl>Z", NULL});
    gtk_application_set_accels_for_action(app->gtk_app, "app.redo", (const char *[]){"<Ctrl><Shift>Z", NULL});
    gtk_application_set_accels_for_action(app->gtk_app, "app.find", (const char *[]){"<Ctrl>F", NULL});
    gtk_application_set_accels_for_action(app->gtk_app, "app.replace", (const char *[]){"<Ctrl>H", NULL});
    gtk_application_set_accels_for_action(app->gtk_app, "app.bold", (const char *[]){"<Ctrl>B", NULL});
    gtk_application_set_accels_for_action(app->gtk_app, "app.italic", (const char *[]){"<Ctrl>I", NULL});
    gtk_application_set_accels_for_action(app->gtk_app, "app.insert-link", (const char *[]){"<Ctrl>K", NULL});
    gtk_application_set_accels_for_action(app->gtk_app, "app.toggle-preview", (const char *[]){"<Ctrl>P", NULL});
    gtk_application_set_accels_for_action(app->gtk_app, "app.focus-mode", (const char *[]){"F11", NULL});
    gtk_application_set_accels_for_action(app->gtk_app, "app.next-tab", (const char *[]){"<Ctrl>Tab", NULL});
    gtk_application_set_accels_for_action(app->gtk_app, "app.previous-tab", (const char *[]){"<Ctrl><Shift>Tab", NULL});
}

static void
on_tree_selection_changed(GtkTreeSelection *selection, gpointer user_data)
{
    LmmeApp *app = user_data;
    (void)selection;

    g_clear_pointer(&app->selected_path, g_free);
    app->selected_is_dir = FALSE;
    app->selected_is_markdown = FALSE;
    app->selected_is_image = FALSE;

    LmmeFileKind kind = LMME_FILE_KIND_OTHER;
    if (lmme_file_tree_get_selected(app->tree_view, &app->selected_path, &kind)) {
        app->selected_is_dir = kind == LMME_FILE_KIND_DIRECTORY;
        app->selected_is_markdown = kind == LMME_FILE_KIND_MARKDOWN;
        app->selected_is_image = kind == LMME_FILE_KIND_IMAGE;
    }
}

static void
on_tree_row_activated(GtkTreeView *tree_view,
                      GtkTreePath *path,
                      GtkTreeViewColumn *column,
                      gpointer user_data)
{
    LmmeApp *app = user_data;
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    GtkTreeIter iter;
    int kind = 0;
    g_autofree char *file_path = NULL;
    (void)column;

    if (!gtk_tree_model_get_iter(model, &iter, path)) {
        return;
    }

    gtk_tree_model_get(model,
                       &iter,
                       LMME_TREE_COL_PATH,
                       &file_path,
                       LMME_TREE_COL_KIND,
                       &kind,
                       -1);

    if ((LmmeFileKind)kind == LMME_FILE_KIND_MARKDOWN) {
        g_autoptr(GError) error = NULL;
        if (!lmme_tabs_open_file(app, file_path, &error)) {
            lmme_dialog_error(GTK_WINDOW(app->window), "Could not read file.", error != NULL ? error->message : NULL);
        }
    }
}

static void
on_notebook_switch_page(GtkNotebook *notebook, GtkWidget *page, guint page_num, gpointer user_data)
{
    LmmeApp *app = user_data;
    (void)notebook;
    (void)page;
    (void)page_num;

    lmme_window_update_status(app);
    lmme_window_schedule_preview(app);
}

static void
on_find_button_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    action_find(NULL, NULL, user_data);
}

static void
on_replace_button_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    action_replace(NULL, NULL, user_data);
}

static GtkWidget *
create_search_bar(LmmeApp *app)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *find_label = gtk_label_new("Find");
    GtkWidget *replace_label = gtk_label_new("Replace");
    GtkWidget *next = gtk_button_new_with_label("Find");
    GtkWidget *replace = gtk_button_new_with_label("Replace");

    gtk_widget_add_css_class(box, "searchbar");
    app->find_entry = gtk_entry_new();
    app->replace_entry = gtk_entry_new();

    gtk_box_append(GTK_BOX(box), find_label);
    gtk_box_append(GTK_BOX(box), app->find_entry);
    gtk_box_append(GTK_BOX(box), next);
    gtk_box_append(GTK_BOX(box), replace_label);
    gtk_box_append(GTK_BOX(box), app->replace_entry);
    gtk_box_append(GTK_BOX(box), replace);

    g_signal_connect(next, "clicked", G_CALLBACK(on_find_button_clicked), app);
    g_signal_connect(replace, "clicked", G_CALLBACK(on_replace_button_clicked), app);

    gtk_widget_set_visible(box, FALSE);
    return box;
}

static void
restore_recovery_files(LmmeApp *app)
{
    g_autoptr(GError) error = NULL;
    GPtrArray *entries = lmme_recovery_list(&error);

    if (entries == NULL) {
        return;
    }

    if (entries->len > 0) {
        GString *detail = g_string_new(NULL);
        for (guint i = 0; i < entries->len; i++) {
            LmmeRecoveryEntry *entry = g_ptr_array_index(entries, i);
            g_string_append_printf(detail, "%s\n", entry->original_path);
            lmme_tabs_open_recovery_file(app, entry->recovery_path, entry->original_path, NULL);
        }
        lmme_dialog_info(GTK_WINDOW(app->window), "Unsaved recovery files were found.", detail->str);
        g_string_free(detail, TRUE);
    }

    g_ptr_array_unref(entries);
}

static void
restore_workspace_and_tabs(LmmeApp *app)
{
    if (app->config.restore_last_workspace &&
        app->config.last_workspace != NULL &&
        app->config.last_workspace[0] != '\0' &&
        g_file_test(app->config.last_workspace, G_FILE_TEST_IS_DIR)) {
        lmme_window_open_workspace_path(app, app->config.last_workspace);
    }

    if (app->workspace != NULL && app->config.restore_tabs && app->config.open_tabs != NULL) {
        for (guint i = 0; i < app->config.open_tabs->len; i++) {
            const char *path = g_ptr_array_index(app->config.open_tabs, i);
            if (g_file_test(path, G_FILE_TEST_EXISTS) && lmme_path_is_inside(app->workspace->path, path)) {
                lmme_tabs_open_file(app, path, NULL);
            }
        }
    }
}

void
lmme_window_build(LmmeApp *app)
{
    load_css();

    g_action_map_add_action_entries(G_ACTION_MAP(app->gtk_app),
                                    app_actions,
                                    G_N_ELEMENTS(app_actions),
                                    app);
    set_accels(app);

    app->window = gtk_application_window_new(app->gtk_app);
    gtk_window_set_title(GTK_WINDOW(app->window), "LowMemoryMarkdownEditor");
    gtk_window_set_default_size(GTK_WINDOW(app->window), app->config.window_width, app->config.window_height);
    if (app->config.window_maximized) {
        gtk_window_maximize(GTK_WINDOW(app->window));
    }

    app->root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(app->window), app->root_box);

    GMenuModel *menu_model = create_menu_model();
    app->menu_bar = gtk_popover_menu_bar_new_from_model(menu_model);
    g_object_unref(menu_model);
    gtk_box_append(GTK_BOX(app->root_box), app->menu_bar);

    app->toolbar = create_toolbar();
    gtk_box_append(GTK_BOX(app->root_box), app->toolbar);

    app->main_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_vexpand(app->main_paned, TRUE);
    gtk_box_append(GTK_BOX(app->root_box), app->main_paned);

    app->sidebar = gtk_scrolled_window_new();
    gtk_widget_add_css_class(app->sidebar, "sidebar");
    gtk_widget_set_size_request(app->sidebar, app->config.sidebar_width, -1);
    app->tree_view = lmme_file_tree_create();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(app->sidebar), app->tree_view);
    gtk_paned_set_start_child(GTK_PANED(app->main_paned), app->sidebar);

    app->right_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_paned_set_end_child(GTK_PANED(app->main_paned), app->right_box);

    app->breadcrumbs_label = gtk_label_new("No workspace opened");
    gtk_label_set_xalign(GTK_LABEL(app->breadcrumbs_label), 0.0f);
    gtk_widget_set_margin_start(app->breadcrumbs_label, 8);
    gtk_widget_set_margin_top(app->breadcrumbs_label, 4);
    gtk_widget_set_margin_bottom(app->breadcrumbs_label, 4);
    gtk_box_append(GTK_BOX(app->right_box), app->breadcrumbs_label);

    app->editor_preview_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_vexpand(app->editor_preview_paned, TRUE);
    gtk_box_append(GTK_BOX(app->right_box), app->editor_preview_paned);

    app->notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(app->notebook), TRUE);
    gtk_paned_set_start_child(GTK_PANED(app->editor_preview_paned), app->notebook);

    app->preview_view = lmme_preview_create_view();
    app->preview_scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(app->preview_scroller), app->preview_view);
    gtk_paned_set_end_child(GTK_PANED(app->editor_preview_paned), app->preview_scroller);
    gtk_widget_set_visible(app->preview_scroller, app->preview_enabled);
    int preview_total_width = MAX(400, app->config.window_width - app->config.sidebar_width);
    int editor_width = (int)((double)preview_total_width * (1.0 - app->config.preview_split_ratio));
    gtk_paned_set_position(GTK_PANED(app->editor_preview_paned), editor_width);

    app->search_bar = create_search_bar(app);
    gtk_box_append(GTK_BOX(app->right_box), app->search_bar);

    app->status_label = gtk_label_new("No workspace opened");
    gtk_label_set_xalign(GTK_LABEL(app->status_label), 0.0f);
    gtk_widget_add_css_class(app->status_label, "statusbar");
    gtk_box_append(GTK_BOX(app->root_box), app->status_label);

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(app->tree_view));
    g_signal_connect(selection, "changed", G_CALLBACK(on_tree_selection_changed), app);
    g_signal_connect(app->tree_view, "row-activated", G_CALLBACK(on_tree_row_activated), app);
    g_signal_connect(app->notebook, "switch-page", G_CALLBACK(on_notebook_switch_page), app);

    gtk_widget_set_visible(app->toolbar, app->config.show_toolbar && !app->focus_mode);
    gtk_widget_set_visible(app->sidebar, !app->focus_mode);
    gtk_widget_set_visible(app->status_label, app->config.show_statusbar);
    gtk_widget_set_visible(app->breadcrumbs_label, app->config.show_breadcrumbs);
    gtk_paned_set_position(GTK_PANED(app->main_paned), app->config.sidebar_width);

    restore_workspace_and_tabs(app);
    restore_recovery_files(app);
    lmme_window_update_status(app);
}

gboolean
lmme_window_open_workspace_path(LmmeApp *app, const char *path)
{
    g_autoptr(GError) error = NULL;
    LmmeWorkspace *workspace = lmme_workspace_new(path);

    if (app->documents != NULL && app->documents->len > 0 && !lmme_tabs_close_all(app)) {
        lmme_workspace_free(workspace);
        return FALSE;
    }

    if (!lmme_workspace_rescan(workspace, app->config.show_hidden_files, app->config.show_images, &error)) {
        lmme_workspace_free(workspace);
        lmme_dialog_error(GTK_WINDOW(app->window), "Could not open workspace.", error != NULL ? error->message : NULL);
        return FALSE;
    }

    g_clear_pointer(&app->workspace, lmme_workspace_free);
    app->workspace = workspace;
    lmme_config_set_last_workspace(&app->config, app->workspace->path);
    lmme_window_refresh_tree(app);
    gtk_label_set_text(GTK_LABEL(app->breadcrumbs_label), app->workspace->path);
    lmme_window_update_status(app);
    return TRUE;
}

void
lmme_window_refresh_tree(LmmeApp *app)
{
    if (app->workspace == NULL) {
        lmme_file_tree_populate(app->tree_view, NULL);
        return;
    }

    g_autoptr(GError) error = NULL;
    if (!lmme_workspace_rescan(app->workspace, app->config.show_hidden_files, app->config.show_images, &error)) {
        lmme_dialog_error(GTK_WINDOW(app->window), "Could not open workspace.", error != NULL ? error->message : NULL);
        return;
    }

    lmme_file_tree_populate(app->tree_view, app->workspace->root);
}

void
lmme_window_update_status(LmmeApp *app)
{
    LmmeDocument *doc = lmme_tabs_get_active(app);

    if (app->workspace == NULL) {
        gtk_label_set_text(GTK_LABEL(app->status_label), "No workspace opened");
        return;
    }

    if (doc == NULL) {
        g_autofree char *status = g_strdup_printf("Workspace: %s | No file opened", app->workspace->path);
        gtk_label_set_text(GTK_LABEL(app->status_label), status);
        return;
    }

    int line = 1;
    int column = 1;
    lmme_editor_get_cursor(GTK_TEXT_BUFFER(doc->buffer), &line, &column);
    guint words = lmme_editor_word_count(GTK_TEXT_BUFFER(doc->buffer));
    g_autofree char *status = g_strdup_printf("%s | Ln %d, Col %d | %u words | %s | %s",
                                              doc->relative_path,
                                              line,
                                              column,
                                              words,
                                              lmme_document_save_state_label(doc),
                                              app->preview_enabled ? "Editor + Preview" : "Editor");
    gtk_label_set_text(GTK_LABEL(app->status_label), status);
}

void
lmme_window_set_status_error(LmmeApp *app, const char *message)
{
    gtk_label_set_text(GTK_LABEL(app->status_label), message != NULL ? message : "Error");
}

static gboolean
preview_timeout_cb(gpointer user_data)
{
    LmmeApp *app = user_data;
    LmmeDocument *doc = lmme_tabs_get_active(app);

    app->preview_timeout_id = 0;
    if (!app->preview_enabled || doc == NULL) {
        lmme_preview_set_markdown(app->preview_view, "", app->config.preview_hide_frontmatter);
        return G_SOURCE_REMOVE;
    }

    g_autofree char *text = lmme_editor_dup_text(GTK_TEXT_BUFFER(doc->buffer));
    lmme_preview_set_markdown(app->preview_view, text, app->config.preview_hide_frontmatter);
    return G_SOURCE_REMOVE;
}

void
lmme_window_schedule_preview(LmmeApp *app)
{
    if (app->preview_timeout_id != 0) {
        g_source_remove(app->preview_timeout_id);
        app->preview_timeout_id = 0;
    }

    if (!app->preview_enabled) {
        return;
    }

    app->preview_timeout_id = g_timeout_add(app->config.preview_update_delay_ms, preview_timeout_cb, app);
}

void
lmme_window_toggle_preview(LmmeApp *app)
{
    app->preview_enabled = !app->preview_enabled;
    gtk_widget_set_visible(app->preview_scroller, app->preview_enabled);
    lmme_window_schedule_preview(app);
    lmme_window_update_status(app);
}

static char *
selected_target_directory(LmmeApp *app)
{
    return lmme_workspace_target_directory(app->workspace, app->selected_path, app->selected_is_dir);
}

static gboolean
directory_is_non_empty(const char *path)
{
    g_autoptr(GDir) dir = NULL;

    if (path == NULL || !g_file_test(path, G_FILE_TEST_IS_DIR)) {
        return FALSE;
    }

    dir = g_dir_open(path, 0, NULL);
    return dir != NULL && g_dir_read_name(dir) != NULL;
}

static void
action_open(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeApp *app = user_data;
    (void)action;
    (void)parameter;

    g_autofree char *path = lmme_dialog_open_folder(GTK_WINDOW(app->window));
    if (path != NULL) {
        lmme_window_open_workspace_path(app, path);
    }
}

static void
action_save(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeApp *app = user_data;
    g_autoptr(GError) error = NULL;
    (void)action;
    (void)parameter;

    if (!lmme_tabs_save_active(app, &error)) {
        lmme_dialog_error(GTK_WINDOW(app->window), "Could not save file.", error != NULL ? error->message : NULL);
    }
    lmme_window_update_status(app);
}

static void
action_quit(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeApp *app = user_data;
    (void)action;
    (void)parameter;
    g_application_quit(G_APPLICATION(app->gtk_app));
}

static void
action_new_file(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeApp *app = user_data;
    g_autofree char *base_dir = NULL;
    g_autofree char *name = NULL;
    g_autofree char *new_path = NULL;
    g_autoptr(GError) error = NULL;
    (void)action;
    (void)parameter;

    if (app->workspace == NULL) {
        lmme_dialog_error(GTK_WINDOW(app->window), "Could not open workspace.", "Open a workspace first.");
        return;
    }

    base_dir = selected_target_directory(app);
    name = lmme_dialog_prompt_text(GTK_WINDOW(app->window), "Create New File", "Filename", "note.md");
    if (name == NULL) {
        return;
    }

    if (!lmme_workspace_create_markdown_file(app->workspace, base_dir, name, &new_path, &error)) {
        lmme_dialog_error(GTK_WINDOW(app->window), "Could not create file.", error != NULL ? error->message : NULL);
        return;
    }

    lmme_window_refresh_tree(app);
    lmme_tabs_open_file(app, new_path, NULL);
}

static void
action_new_folder(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeApp *app = user_data;
    g_autofree char *base_dir = NULL;
    g_autofree char *name = NULL;
    g_autoptr(GError) error = NULL;
    (void)action;
    (void)parameter;

    if (app->workspace == NULL) {
        lmme_dialog_error(GTK_WINDOW(app->window), "Could not open workspace.", "Open a workspace first.");
        return;
    }

    base_dir = selected_target_directory(app);
    name = lmme_dialog_prompt_text(GTK_WINDOW(app->window), "Create New Folder", "Folder name", "notes");
    if (name == NULL) {
        return;
    }

    if (!lmme_workspace_create_folder(app->workspace, base_dir, name, &error)) {
        lmme_dialog_error(GTK_WINDOW(app->window), "Could not create folder.", error != NULL ? error->message : NULL);
        return;
    }

    lmme_window_refresh_tree(app);
}

static void
action_rename(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeApp *app = user_data;
    g_autofree char *base = NULL;
    g_autofree char *name = NULL;
    g_autofree char *new_path = NULL;
    g_autofree char *old_path = NULL;
    g_autoptr(GError) error = NULL;
    (void)action;
    (void)parameter;

    if (app->workspace == NULL || app->selected_path == NULL) {
        return;
    }

    old_path = g_strdup(app->selected_path);
    base = g_path_get_basename(old_path);
    name = lmme_dialog_prompt_text(GTK_WINDOW(app->window), "Rename", "New name", base);
    if (name == NULL) {
        return;
    }

    if (!lmme_workspace_rename_path(app->workspace, old_path, name, &new_path, &error)) {
        lmme_dialog_error(GTK_WINDOW(app->window), "Could not rename item.", error != NULL ? error->message : NULL);
        return;
    }

    lmme_tabs_update_path(app, old_path, new_path);
    lmme_window_refresh_tree(app);
}

static void
action_delete(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeApp *app = user_data;
    gboolean dont_show_again = FALSE;
    g_autofree char *path = NULL;
    g_autoptr(GError) error = NULL;
    gboolean force_confirmation = FALSE;
    (void)action;
    (void)parameter;

    if (app->workspace == NULL || app->selected_path == NULL) {
        return;
    }

    force_confirmation = app->selected_is_dir && directory_is_non_empty(app->selected_path);
    if ((app->config.confirm_delete || force_confirmation) &&
        !lmme_dialog_confirm_delete(GTK_WINDOW(app->window), &dont_show_again)) {
        return;
    }

    if (dont_show_again && !force_confirmation) {
        app->config.confirm_delete = FALSE;
    }

    path = g_strdup(app->selected_path);
    if (!lmme_workspace_delete_path(app->workspace, path, &error)) {
        lmme_dialog_error(GTK_WINDOW(app->window), "Could not delete item.", error != NULL ? error->message : NULL);
        return;
    }

    lmme_tabs_close_path(app, path);
    lmme_window_refresh_tree(app);
}

static void
action_toggle_sidebar(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeApp *app = user_data;
    (void)action;
    (void)parameter;

    gtk_widget_set_visible(app->sidebar, !gtk_widget_get_visible(app->sidebar));
}

static void
action_toggle_preview(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    lmme_window_toggle_preview(user_data);
}

static void
action_focus_mode(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeApp *app = user_data;
    (void)action;
    (void)parameter;

    app->focus_mode = !app->focus_mode;
    gtk_widget_set_visible(app->sidebar, !app->focus_mode);
    gtk_widget_set_visible(app->toolbar, !app->focus_mode && app->config.show_toolbar);
}

static void
action_close_tab(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    lmme_tabs_close_active(user_data);
}

static void
action_next_tab(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeApp *app = user_data;
    (void)action;
    (void)parameter;
    gtk_notebook_next_page(GTK_NOTEBOOK(app->notebook));
}

static void
action_previous_tab(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeApp *app = user_data;
    (void)action;
    (void)parameter;
    gtk_notebook_prev_page(GTK_NOTEBOOK(app->notebook));
}

static void
action_undo(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeDocument *doc = lmme_tabs_get_active(user_data);
    (void)action;
    (void)parameter;
    if (doc != NULL && gtk_source_buffer_can_undo(doc->buffer)) {
        gtk_source_buffer_undo(doc->buffer);
    }
}

static void
action_redo(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeDocument *doc = lmme_tabs_get_active(user_data);
    (void)action;
    (void)parameter;
    if (doc != NULL && gtk_source_buffer_can_redo(doc->buffer)) {
        gtk_source_buffer_redo(doc->buffer);
    }
}

static void
action_cut(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeDocument *doc = lmme_tabs_get_active(user_data);
    (void)action;
    (void)parameter;
    if (doc != NULL) {
        GdkClipboard *clipboard = gtk_widget_get_clipboard(doc->source_view);
        gtk_text_buffer_cut_clipboard(GTK_TEXT_BUFFER(doc->buffer), clipboard, TRUE);
    }
}

static void
action_copy(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeDocument *doc = lmme_tabs_get_active(user_data);
    (void)action;
    (void)parameter;
    if (doc != NULL) {
        GdkClipboard *clipboard = gtk_widget_get_clipboard(doc->source_view);
        gtk_text_buffer_copy_clipboard(GTK_TEXT_BUFFER(doc->buffer), clipboard);
    }
}

static void
action_paste(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeDocument *doc = lmme_tabs_get_active(user_data);
    (void)action;
    (void)parameter;
    if (doc != NULL) {
        GdkClipboard *clipboard = gtk_widget_get_clipboard(doc->source_view);
        gtk_text_buffer_paste_clipboard(GTK_TEXT_BUFFER(doc->buffer), clipboard, NULL, TRUE);
    }
}

static void
show_search_bar(LmmeApp *app, gboolean with_replace)
{
    gtk_widget_set_visible(app->search_bar, TRUE);
    gtk_widget_set_visible(app->replace_entry, with_replace);
    gtk_widget_grab_focus(app->find_entry);
}

static void
action_find(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeApp *app = user_data;
    LmmeDocument *doc = lmme_tabs_get_active(app);
    (void)action;
    (void)parameter;

    show_search_bar(app, FALSE);
    if (doc != NULL) {
        const char *needle = gtk_editable_get_text(GTK_EDITABLE(app->find_entry));
        if (needle[0] != '\0' && !lmme_editor_find(GTK_TEXT_BUFFER(doc->buffer), needle, TRUE)) {
            lmme_window_set_status_error(app, "No matches");
        }
    }
}

static void
action_replace(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeApp *app = user_data;
    LmmeDocument *doc = lmme_tabs_get_active(app);
    (void)action;
    (void)parameter;

    show_search_bar(app, TRUE);
    if (doc != NULL) {
        const char *needle = gtk_editable_get_text(GTK_EDITABLE(app->find_entry));
        const char *replacement = gtk_editable_get_text(GTK_EDITABLE(app->replace_entry));
        if (needle[0] != '\0' && !lmme_editor_replace_current(GTK_TEXT_BUFFER(doc->buffer), needle, replacement)) {
            lmme_window_set_status_error(app, "No matches");
        }
    }
}

static void
action_bold(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeDocument *doc = lmme_tabs_get_active(user_data);
    (void)action;
    (void)parameter;
    if (doc != NULL) {
        lmme_editor_wrap_selection(GTK_TEXT_BUFFER(doc->buffer), "**", "**", 2);
    }
}

static void
action_italic(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeDocument *doc = lmme_tabs_get_active(user_data);
    (void)action;
    (void)parameter;
    if (doc != NULL) {
        lmme_editor_wrap_selection(GTK_TEXT_BUFFER(doc->buffer), "*", "*", 1);
    }
}

static void
action_insert_link(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeDocument *doc = lmme_tabs_get_active(user_data);
    (void)action;
    (void)parameter;
    if (doc != NULL) {
        lmme_editor_wrap_selection(GTK_TEXT_BUFFER(doc->buffer), "[", "]()", 3);
    }
}

static void
action_insert_image(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeApp *app = user_data;
    (void)action;
    (void)parameter;
    lmme_image_insert_from_dialog(app);
}

static void
action_about(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeApp *app = user_data;
    (void)action;
    (void)parameter;
    lmme_dialog_info(GTK_WINDOW(app->window),
                     "LowMemoryMarkdownEditor",
                     "Small Linux-only GTK Markdown editor for local folders.");
}
