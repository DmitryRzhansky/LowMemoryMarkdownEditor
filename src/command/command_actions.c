#include "command/command_actions.h"

#include <gtk/gtk.h>

#include "app/app.h"
#include "document/document.h"
#include "document/tabs.h"
#include "editor/editor.h"
#include "editor/editor_ops.h"
#include "editor/editor_search.h"
#include "features/image_insert.h"
#include "infra/dialogs.h"
#include "infra/util.h"
#include "ui/search_bar.h"
#include "ui/tree_context_menu.h"
#include "ui/window.h"
#include "workspace/workspace.h"

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
    g_autofree char *path = NULL;
    (void)action;
    (void)parameter;

    path = lmme_dialog_open_folder(GTK_WINDOW(app->window));
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
    (void)action;
    (void)parameter;
    g_application_quit(G_APPLICATION(((LmmeApp *)user_data)->gtk_app));
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
    gboolean force_confirmation = FALSE;
    g_autofree char *path = NULL;
    g_autoptr(GError) error = NULL;
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
    font_size = clamp_editor_font_size(font_size);
    if (app->config.font_size == font_size) {
        return;
    }
    app->config.font_size = font_size;
    lmme_editor_apply_font_css(&app->config);
    lmme_window_update_status(app);
}

static void action_zoom_in(GSimpleAction *action, GVariant *parameter, gpointer user_data) { (void)action; (void)parameter; set_editor_font_size(user_data, ((LmmeApp *)user_data)->config.font_size + 1); }
static void action_zoom_out(GSimpleAction *action, GVariant *parameter, gpointer user_data) { (void)action; (void)parameter; set_editor_font_size(user_data, ((LmmeApp *)user_data)->config.font_size - 1); }
static void action_zoom_reset(GSimpleAction *action, GVariant *parameter, gpointer user_data) { (void)action; (void)parameter; set_editor_font_size(user_data, LMME_EDITOR_FONT_SIZE_DEFAULT); }

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

static void action_close_tab(GSimpleAction *action, GVariant *parameter, gpointer user_data) { (void)action; (void)parameter; lmme_tabs_close_active(user_data); }

static void
action_close_other_tabs(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeApp *app = user_data;
    (void)action;
    (void)parameter;
    if (app->tab_context_document != NULL) { lmme_tabs_close_other_tabs(app, app->tab_context_document); }
}

static void
action_close_tabs_right(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeApp *app = user_data;
    (void)action;
    (void)parameter;
    if (app->tab_context_document != NULL) { lmme_tabs_close_tabs_to_right(app, app->tab_context_document); }
}

static void
action_close_tabs_left(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeApp *app = user_data;
    (void)action;
    (void)parameter;
    if (app->tab_context_document != NULL) { lmme_tabs_close_tabs_to_left(app, app->tab_context_document); }
}

static void action_close_all_tabs(GSimpleAction *action, GVariant *parameter, gpointer user_data) { (void)action; (void)parameter; lmme_tabs_close_all(user_data); }

static void
action_copy_relative_path(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeApp *app = user_data;
    (void)action;
    (void)parameter;
    if (app->tab_context_document != NULL && app->tab_context_document->relative_path != NULL) {
        gdk_clipboard_set_text(gtk_widget_get_clipboard(app->window), app->tab_context_document->relative_path);
    }
}

static void
action_copy_full_path(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeApp *app = user_data;
    (void)action;
    (void)parameter;
    if (app->tab_context_document != NULL && app->tab_context_document->path != NULL) {
        gdk_clipboard_set_text(gtk_widget_get_clipboard(app->window), app->tab_context_document->path);
    }
}

static void action_next_tab(GSimpleAction *action, GVariant *parameter, gpointer user_data) { (void)action; (void)parameter; gtk_notebook_next_page(GTK_NOTEBOOK(((LmmeApp *)user_data)->notebook)); }
static void action_previous_tab(GSimpleAction *action, GVariant *parameter, gpointer user_data) { (void)action; (void)parameter; gtk_notebook_prev_page(GTK_NOTEBOOK(((LmmeApp *)user_data)->notebook)); }

static void
action_undo(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeDocument *doc = lmme_tabs_get_active(user_data);
    (void)action;
    (void)parameter;
    if (doc != NULL && gtk_text_buffer_get_can_undo(GTK_TEXT_BUFFER(doc->buffer))) { gtk_text_buffer_undo(GTK_TEXT_BUFFER(doc->buffer)); }
}

static void
action_redo(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeDocument *doc = lmme_tabs_get_active(user_data);
    (void)action;
    (void)parameter;
    if (doc != NULL && gtk_text_buffer_get_can_redo(GTK_TEXT_BUFFER(doc->buffer))) { gtk_text_buffer_redo(GTK_TEXT_BUFFER(doc->buffer)); }
}

static void
action_cut(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeDocument *doc = lmme_tabs_get_active(user_data);
    (void)action;
    (void)parameter;
    if (doc != NULL) { gtk_text_buffer_cut_clipboard(GTK_TEXT_BUFFER(doc->buffer), gtk_widget_get_clipboard(doc->source_view), TRUE); }
}

static void
action_copy(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeDocument *doc = lmme_tabs_get_active(user_data);
    (void)action;
    (void)parameter;
    if (doc != NULL) { gtk_text_buffer_copy_clipboard(GTK_TEXT_BUFFER(doc->buffer), gtk_widget_get_clipboard(doc->source_view)); }
}

static void
action_paste(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeDocument *doc = lmme_tabs_get_active(user_data);
    (void)action;
    (void)parameter;
    if (doc != NULL) { gtk_text_buffer_paste_clipboard(GTK_TEXT_BUFFER(doc->buffer), gtk_widget_get_clipboard(doc->source_view), NULL, TRUE); }
}

static void
action_find(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeApp *app = user_data;
    LmmeDocument *doc = lmme_tabs_get_active(app);
    (void)action;
    (void)parameter;
    if (lmme_search_bar_is_visible(app) && !lmme_search_bar_is_replace_mode(app)) {
        lmme_search_bar_hide(app);
        return;
    }
    lmme_search_bar_show(app, FALSE);
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
    if (lmme_search_bar_is_visible(app) && lmme_search_bar_is_replace_mode(app)) {
        lmme_search_bar_hide(app);
        return;
    }
    lmme_search_bar_show(app, TRUE);
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
    if (doc != NULL) { lmme_editor_wrap_selection(GTK_TEXT_BUFFER(doc->buffer), "**", "**", 2); }
}

static void
action_italic(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeDocument *doc = lmme_tabs_get_active(user_data);
    (void)action;
    (void)parameter;
    if (doc != NULL) { lmme_editor_wrap_selection(GTK_TEXT_BUFFER(doc->buffer), "*", "*", 1); }
}

static void
action_insert_link(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeDocument *doc = lmme_tabs_get_active(user_data);
    (void)action;
    (void)parameter;
    if (doc != NULL) { lmme_editor_wrap_selection(GTK_TEXT_BUFFER(doc->buffer), "[", "]()", 3); }
}

static void action_insert_image(GSimpleAction *action, GVariant *parameter, gpointer user_data) { (void)action; (void)parameter; lmme_image_insert_from_dialog(user_data); }

static void
action_about(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeApp *app = user_data;
    (void)action;
    (void)parameter;
    lmme_dialog_info(GTK_WINDOW(app->window), "LowMemoryMarkdownEditor", "Small Linux-only GTK Markdown editor for local folders.");
}

static void action_tree_new_file(GSimpleAction *action, GVariant *parameter, gpointer user_data) { action_new_file(action, parameter, user_data); }
static void action_tree_new_folder(GSimpleAction *action, GVariant *parameter, gpointer user_data) { action_new_folder(action, parameter, user_data); }
static void action_tree_rename(GSimpleAction *action, GVariant *parameter, gpointer user_data) { action_rename(action, parameter, user_data); }
static void action_tree_delete(GSimpleAction *action, GVariant *parameter, gpointer user_data) { action_delete(action, parameter, user_data); }

static void
action_tree_open(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeApp *app = user_data;
    g_autoptr(GError) error = NULL;
    (void)action;
    (void)parameter;
    if (app->tree_context_path != NULL && app->tree_context_is_markdown &&
        !lmme_tabs_open_file(app, app->tree_context_path, &error)) {
        lmme_dialog_error(GTK_WINDOW(app->window), "Could not read file.", error != NULL ? error->message : NULL);
    }
}

static void
action_tree_close_tab(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeApp *app = user_data;
    LmmeDocument *doc = NULL;
    (void)action;
    (void)parameter;
    if (app->tree_context_path != NULL) {
        doc = lmme_tabs_find_by_path(app, app->tree_context_path);
        if (doc != NULL) { lmme_tabs_close_document(app, doc); }
    }
}

static void
action_tree_copy_relative_path(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeApp *app = user_data;
    g_autofree char *relative = NULL;
    (void)action;
    (void)parameter;
    relative = lmme_tree_context_menu_dup_relative_path(app);
    if (relative != NULL) { gdk_clipboard_set_text(gtk_widget_get_clipboard(app->window), relative); }
}

static void
action_tree_copy_full_path(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeApp *app = user_data;
    (void)action;
    (void)parameter;
    if (app->tree_context_path != NULL) { gdk_clipboard_set_text(gtk_widget_get_clipboard(app->window), app->tree_context_path); }
}

static void
action_tree_copy_markdown_image_link(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeApp *app = user_data;
    g_autofree char *relative = NULL;
    g_autofree char *link = NULL;
    (void)action;
    (void)parameter;
    if (!app->tree_context_is_image || app->tree_context_path == NULL) { return; }
    relative = lmme_tree_context_menu_dup_relative_path(app);
    if (relative == NULL) { return; }
    link = g_strdup_printf("![](%s)", relative);
    gdk_clipboard_set_text(gtk_widget_get_clipboard(app->window), link);
}

static void
action_tree_open_containing_folder(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    LmmeApp *app = user_data;
    g_autofree char *target_dir = NULL;
    g_autoptr(GFile) file = NULL;
    g_autofree char *uri = NULL;
    (void)action;
    (void)parameter;
    if (app->tree_context_path == NULL || app->tree_context_is_empty_area) { return; }
    target_dir = app->tree_context_is_dir ? g_strdup(app->tree_context_path) : g_path_get_dirname(app->tree_context_path);
    file = g_file_new_for_path(target_dir);
    uri = g_file_get_uri(file);
    gtk_show_uri(GTK_WINDOW(app->window), uri, GDK_CURRENT_TIME);
}

static const GActionEntry app_actions[] = {
    {.name = "open", .activate = action_open}, {.name = "save", .activate = action_save}, {.name = "quit", .activate = action_quit},
    {.name = "new-file", .activate = action_new_file}, {.name = "new-folder", .activate = action_new_folder}, {.name = "rename", .activate = action_rename}, {.name = "delete", .activate = action_delete},
    {.name = "toggle-sidebar", .activate = action_toggle_sidebar}, {.name = "toggle-preview", .activate = action_toggle_preview}, {.name = "zoom-in", .activate = action_zoom_in}, {.name = "zoom-out", .activate = action_zoom_out}, {.name = "zoom-reset", .activate = action_zoom_reset}, {.name = "focus-mode", .activate = action_focus_mode},
    {.name = "close-tab", .activate = action_close_tab}, {.name = "close-other-tabs", .activate = action_close_other_tabs}, {.name = "close-tabs-right", .activate = action_close_tabs_right}, {.name = "close-tabs-left", .activate = action_close_tabs_left}, {.name = "close-all-tabs", .activate = action_close_all_tabs}, {.name = "copy-relative-path", .activate = action_copy_relative_path}, {.name = "copy-full-path", .activate = action_copy_full_path}, {.name = "next-tab", .activate = action_next_tab}, {.name = "previous-tab", .activate = action_previous_tab},
    {.name = "undo", .activate = action_undo}, {.name = "redo", .activate = action_redo}, {.name = "cut", .activate = action_cut}, {.name = "copy", .activate = action_copy}, {.name = "paste", .activate = action_paste}, {.name = "find", .activate = action_find}, {.name = "replace", .activate = action_replace}, {.name = "bold", .activate = action_bold}, {.name = "italic", .activate = action_italic}, {.name = "insert-link", .activate = action_insert_link}, {.name = "insert-image", .activate = action_insert_image}, {.name = "about", .activate = action_about},
    {.name = "tree-new-file", .activate = action_tree_new_file}, {.name = "tree-new-folder", .activate = action_tree_new_folder}, {.name = "tree-rename", .activate = action_tree_rename}, {.name = "tree-delete", .activate = action_tree_delete}, {.name = "tree-open", .activate = action_tree_open}, {.name = "tree-close-tab", .activate = action_tree_close_tab}, {.name = "tree-copy-relative-path", .activate = action_tree_copy_relative_path}, {.name = "tree-copy-full-path", .activate = action_tree_copy_full_path}, {.name = "tree-copy-markdown-image-link", .activate = action_tree_copy_markdown_image_link}, {.name = "tree-open-containing-folder", .activate = action_tree_open_containing_folder},
};

void
lmme_command_actions_register(LmmeApp *app)
{
    g_action_map_add_action_entries(G_ACTION_MAP(app->gtk_app), app_actions, G_N_ELEMENTS(app_actions), app);
}
