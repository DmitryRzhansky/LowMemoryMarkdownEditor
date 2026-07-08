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
static void action_zoom_in(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_zoom_out(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_zoom_reset(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_focus_mode(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_close_tab(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_close_other_tabs(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_close_tabs_right(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_close_tabs_left(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_close_all_tabs(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_copy_relative_path(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void action_copy_full_path(GSimpleAction *action, GVariant *parameter, gpointer user_data);
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
    {.name = "open", .activate = action_open},
    {.name = "save", .activate = action_save},
    {.name = "quit", .activate = action_quit},
    {.name = "new-file", .activate = action_new_file},
    {.name = "new-folder", .activate = action_new_folder},
    {.name = "rename", .activate = action_rename},
    {.name = "delete", .activate = action_delete},
    {.name = "toggle-sidebar", .activate = action_toggle_sidebar},
    {.name = "toggle-preview", .activate = action_toggle_preview},
    {.name = "zoom-in", .activate = action_zoom_in},
    {.name = "zoom-out", .activate = action_zoom_out},
    {.name = "zoom-reset", .activate = action_zoom_reset},
    {.name = "focus-mode", .activate = action_focus_mode},
    {.name = "close-tab", .activate = action_close_tab},
    {.name = "close-other-tabs", .activate = action_close_other_tabs},
    {.name = "close-tabs-right", .activate = action_close_tabs_right},
    {.name = "close-tabs-left", .activate = action_close_tabs_left},
    {.name = "close-all-tabs", .activate = action_close_all_tabs},
    {.name = "copy-relative-path", .activate = action_copy_relative_path},
    {.name = "copy-full-path", .activate = action_copy_full_path},
    {.name = "next-tab", .activate = action_next_tab},
    {.name = "previous-tab", .activate = action_previous_tab},
    {.name = "undo", .activate = action_undo},
    {.name = "redo", .activate = action_redo},
    {.name = "cut", .activate = action_cut},
    {.name = "copy", .activate = action_copy},
    {.name = "paste", .activate = action_paste},
    {.name = "find", .activate = action_find},
    {.name = "replace", .activate = action_replace},
    {.name = "bold", .activate = action_bold},
    {.name = "italic", .activate = action_italic},
    {.name = "insert-link", .activate = action_insert_link},
    {.name = "insert-image", .activate = action_insert_image},
    {.name = "about", .activate = action_about},
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
make_icon_button(const char *icon_name, const char *tooltip, const char *action_name)
{
    GtkWidget *button = gtk_button_new();
    GtkWidget *image = gtk_image_new_from_icon_name(icon_name);

    gtk_image_set_pixel_size(GTK_IMAGE(image), 18);
    gtk_button_set_child(GTK_BUTTON(button), image);
    gtk_actionable_set_action_name(GTK_ACTIONABLE(button), action_name);
    gtk_widget_set_tooltip_text(button, tooltip);
    gtk_widget_add_css_class(button, "toolbar-icon-button");

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
    g_menu_append(view, "Zoom In", "app.zoom-in");
    g_menu_append(view, "Zoom Out", "app.zoom-out");
    g_menu_append(view, "Reset Zoom", "app.zoom-reset");
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
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

    gtk_widget_add_css_class(toolbar, "toolbar");
    gtk_box_append(GTK_BOX(toolbar),
                   make_icon_button("document-open-symbolic", "Open Workspace", "app.open"));
    gtk_box_append(GTK_BOX(toolbar),
                   make_icon_button("document-new-symbolic", "New Markdown File", "app.new-file"));
    gtk_box_append(GTK_BOX(toolbar),
                   make_icon_button("folder-new-symbolic", "New Folder", "app.new-folder"));
    gtk_box_append(GTK_BOX(toolbar),
                   make_icon_button("document-save-symbolic", "Save", "app.save"));
    gtk_box_append(GTK_BOX(toolbar),
                   make_icon_button("view-split-left-right-symbolic",
                                    "Editor + Preview",
                                    "app.toggle-preview"));
    gtk_box_append(GTK_BOX(toolbar),
                   make_icon_button("image-x-generic-symbolic", "Insert Image", "app.insert-image"));
    gtk_box_append(GTK_BOX(toolbar),
                   make_icon_button("edit-find-symbolic", "Find", "app.find"));
    gtk_box_append(GTK_BOX(toolbar),
                   make_icon_button("edit-find-replace-symbolic", "Replace", "app.replace"));

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
    gtk_application_set_accels_for_action(app->gtk_app,
                                          "app.zoom-in",
                                          (const char *[]){"<Ctrl>plus",
                                                           "<Ctrl>equal",
                                                           "<Ctrl><Shift>plus",
                                                           "<Ctrl><Shift>equal",
                                                           "<Ctrl>KP_Add",
                                                           NULL});
    gtk_application_set_accels_for_action(app->gtk_app,
                                          "app.zoom-out",
                                          (const char *[]){"<Ctrl>minus", "<Ctrl>KP_Subtract", NULL});
    gtk_application_set_accels_for_action(app->gtk_app,
                                          "app.zoom-reset",
                                          (const char *[]){"<Ctrl>0", "<Ctrl>KP_0", NULL});
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

    if ((LmmeFileKind)kind == LMME_FILE_KIND_DIRECTORY) {
        if (gtk_tree_view_row_expanded(tree_view, path)) {
            gtk_tree_view_collapse_row(tree_view, path);
        } else {
            gtk_tree_view_expand_row(tree_view, path, FALSE);
        }
        return;
    }

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

    if (app->preview_enabled) {
        lmme_window_refresh_preview_now(app);
    } else {
        lmme_window_update_status(app);
    }
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

static gboolean
search_bar_is_visible(LmmeApp *app)
{
    return gtk_widget_get_visible(app->search_bar);
}

static gboolean
search_bar_is_replace_mode(LmmeApp *app)
{
    return gtk_widget_get_visible(app->replace_entry);
}

static void
hide_search_bar(LmmeApp *app)
{
    LmmeDocument *doc;

    if (!search_bar_is_visible(app)) {
        return;
    }

    gtk_widget_set_visible(app->search_bar, FALSE);
    gtk_widget_set_visible(app->replace_entry, FALSE);

    doc = lmme_tabs_get_active(app);
    if (doc != NULL) {
        gtk_widget_grab_focus(doc->source_view);
    }
}

static gboolean
on_window_search_key_pressed(GtkEventControllerKey *controller,
                             guint keyval,
                             guint keycode,
                             GdkModifierType state,
                             gpointer user_data)
{
    LmmeApp *app = user_data;
    (void)controller;
    (void)keycode;
    (void)state;

    if (keyval != GDK_KEY_Escape || !search_bar_is_visible(app)) {
        return FALSE;
    }

    hide_search_bar(app);
    return TRUE;
}

static void
search_bar_set_valign_center(GtkWidget *widget)
{
    gtk_widget_set_valign(widget, GTK_ALIGN_CENTER);
}

static GtkWidget *
create_search_bar(LmmeApp *app)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *find_button = gtk_button_new_with_label("Find");

    gtk_widget_add_css_class(box, "searchbar");
    app->find_entry = gtk_entry_new();
    app->replace_entry = gtk_entry_new();
    app->replace_button = gtk_button_new_with_label("Replace");

    gtk_widget_set_size_request(app->find_entry, 180, -1);
    gtk_widget_set_size_request(app->replace_entry, 180, -1);
    search_bar_set_valign_center(app->find_entry);
    search_bar_set_valign_center(find_button);
    search_bar_set_valign_center(app->replace_entry);
    search_bar_set_valign_center(app->replace_button);

    gtk_box_append(GTK_BOX(box), app->find_entry);
    gtk_box_append(GTK_BOX(box), find_button);
    gtk_box_append(GTK_BOX(box), app->replace_entry);
    gtk_box_append(GTK_BOX(box), app->replace_button);

    g_signal_connect(find_button, "clicked", G_CALLBACK(on_find_button_clicked), app);
    g_signal_connect(app->replace_button, "clicked", G_CALLBACK(on_replace_button_clicked), app);

    gtk_widget_set_visible(box, FALSE);
    gtk_widget_set_visible(app->replace_entry, FALSE);
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
    lmme_editor_apply_font_css(&app->config);

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
    gtk_paned_set_shrink_start_child(GTK_PANED(app->main_paned), FALSE);
    gtk_box_append(GTK_BOX(app->root_box), app->main_paned);

    app->config.sidebar_width = MAX(app->config.sidebar_width, 220);
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

    app->notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(app->notebook), TRUE);
    gtk_widget_set_hexpand(app->notebook, TRUE);
    gtk_widget_set_vexpand(app->notebook, TRUE);
    gtk_box_append(GTK_BOX(app->right_box), app->notebook);

    app->search_bar = create_search_bar(app);
    gtk_box_append(GTK_BOX(app->right_box), app->search_bar);

    GtkEventController *search_key = gtk_event_controller_key_new();
    g_signal_connect(search_key, "key-pressed", G_CALLBACK(on_window_search_key_pressed), app);
    gtk_widget_add_controller(app->window, search_key);

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
                                              app->preview_enabled ? "Editable Preview" : "Source");
    gtk_label_set_text(GTK_LABEL(app->status_label), status);
}

void
lmme_window_set_status_error(LmmeApp *app, const char *message)
{
    gtk_label_set_text(GTK_LABEL(app->status_label), message != NULL ? message : "Error");
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

    if (!app->preview_enabled) {
        return;
    }

    app->preview_timeout_id = g_timeout_add(app->config.preview_update_delay_ms, preview_timeout_cb, app);
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

static int
clamp_editor_font_size(int font_size)
{
    if (font_size < LMME_EDITOR_FONT_SIZE_MIN) {
        return LMME_EDITOR_FONT_SIZE_MIN;
    }

    if (font_size > LMME_EDITOR_FONT_SIZE_MAX) {
        return LMME_EDITOR_FONT_SIZE_MAX;
    }

    return font_size;
}

static void
set_editor_font_size(LmmeApp *app, int font_size)
{
    if (app == NULL) {
        return;
    }

    font_size = clamp_editor_font_size(font_size);

    if (app->config.font_size == font_size) {
        return;
    }

    app->config.font_size = font_size;
    lmme_editor_apply_font_css(&app->config);
    lmme_window_update_status(app);
}

static void
action_zoom_in(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeApp *app = user_data;
    (void)action;
    (void)parameter;

    set_editor_font_size(app, app->config.font_size + 1);
}

static void
action_zoom_out(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeApp *app = user_data;
    (void)action;
    (void)parameter;

    set_editor_font_size(app, app->config.font_size - 1);
}

static void
action_zoom_reset(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;

    set_editor_font_size(user_data, LMME_EDITOR_FONT_SIZE_DEFAULT);
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
action_close_other_tabs(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeApp *app = user_data;
    (void)action;
    (void)parameter;

    if (app == NULL || app->tab_context_document == NULL) {
        return;
    }

    lmme_tabs_close_other_tabs(app, app->tab_context_document);
}

static void
action_close_tabs_right(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeApp *app = user_data;
    (void)action;
    (void)parameter;

    if (app == NULL || app->tab_context_document == NULL) {
        return;
    }

    lmme_tabs_close_tabs_to_right(app, app->tab_context_document);
}

static void
action_close_tabs_left(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeApp *app = user_data;
    (void)action;
    (void)parameter;

    if (app == NULL || app->tab_context_document == NULL) {
        return;
    }

    lmme_tabs_close_tabs_to_left(app, app->tab_context_document);
}

static void
action_close_all_tabs(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeApp *app = user_data;
    (void)action;
    (void)parameter;

    if (app == NULL) {
        return;
    }

    lmme_tabs_close_all(app);
}

static void
action_copy_relative_path(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeApp *app = user_data;
    GdkClipboard *clipboard = NULL;
    (void)action;
    (void)parameter;

    if (app == NULL || app->tab_context_document == NULL || app->tab_context_document->relative_path == NULL) {
        return;
    }

    clipboard = gtk_widget_get_clipboard(app->window);
    gdk_clipboard_set_text(clipboard, app->tab_context_document->relative_path);
}

static void
action_copy_full_path(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeApp *app = user_data;
    GdkClipboard *clipboard = NULL;
    (void)action;
    (void)parameter;

    if (app == NULL || app->tab_context_document == NULL || app->tab_context_document->path == NULL) {
        return;
    }

    clipboard = gtk_widget_get_clipboard(app->window);
    gdk_clipboard_set_text(clipboard, app->tab_context_document->path);
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
    if (doc != NULL && gtk_text_buffer_get_can_undo(GTK_TEXT_BUFFER(doc->buffer))) {
        gtk_text_buffer_undo(GTK_TEXT_BUFFER(doc->buffer));
    }
}

static void
action_redo(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeDocument *doc = lmme_tabs_get_active(user_data);
    (void)action;
    (void)parameter;
    if (doc != NULL && gtk_text_buffer_get_can_redo(GTK_TEXT_BUFFER(doc->buffer))) {
        gtk_text_buffer_redo(GTK_TEXT_BUFFER(doc->buffer));
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
    gtk_widget_grab_focus(with_replace ? app->replace_entry : app->find_entry);
}

static void
action_find(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeApp *app = user_data;
    LmmeDocument *doc = lmme_tabs_get_active(app);
    (void)action;
    (void)parameter;

    if (search_bar_is_visible(app) && !search_bar_is_replace_mode(app)) {
        hide_search_bar(app);
        return;
    }

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

    if (search_bar_is_visible(app) && search_bar_is_replace_mode(app)) {
        hide_search_bar(app);
        return;
    }

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
