#include "command/command_handlers.h"

#include <gtk/gtk.h>

#include "app/app.h"
#include "document/document.h"
#include "document/document_paths.h"
#include "document/tabs.h"
#include "infra/dialogs.h"
#include "infra/util.h"
#include "ui/file_tree_view.h"
#include "ui/tree_context_menu.h"
#include "ui/window.h"
#include "workspace/workspace.h"

static char *
selected_target_directory(LmmeApp *app)
{
    return lmme_workspace_target_directory(app->workspace,
                                           app->selection.path,
                                           lmme_path_context_is_directory(&app->selection));
}

static gboolean
directory_is_non_empty(const char *path)
{
    g_autoptr(GDir) directory = NULL;

    if (path == NULL || !g_file_test(path, G_FILE_TEST_IS_DIR)) {
        return FALSE;
    }
    directory = g_dir_open(path, 0, NULL);
    return directory != NULL && g_dir_read_name(directory) != NULL;
}

static void
action_new_file(LmmeApp *app)
{
    g_autofree char *base_dir = NULL;
    g_autofree char *name = NULL;
    g_autofree char *new_path = NULL;
    g_autoptr(GError) error = NULL;

    if (app->workspace == NULL) {
        lmme_dialog_error(GTK_WINDOW(app->window),
                          "Could not open workspace.",
                          "Open a workspace first.");
        return;
    }
    base_dir = selected_target_directory(app);
    name = lmme_dialog_prompt_text(GTK_WINDOW(app->window),
                                   "Create New File",
                                   "Filename",
                                   "note.md");
    if (name == NULL) {
        return;
    }
    if (!lmme_workspace_create_markdown_file(app->workspace,
                                             base_dir,
                                             name,
                                             &new_path,
                                             &error)) {
        lmme_dialog_error(GTK_WINDOW(app->window),
                          "Could not create file.",
                          error != NULL ? error->message : NULL);
        return;
    }
    lmme_window_refresh_tree_directory(app, base_dir);
    (void)lmme_tabs_open_file(app, new_path, NULL);
}

static void
action_new_folder(LmmeApp *app)
{
    g_autofree char *base_dir = NULL;
    g_autofree char *name = NULL;
    g_autoptr(GError) error = NULL;

    if (app->workspace == NULL) {
        lmme_dialog_error(GTK_WINDOW(app->window),
                          "Could not open workspace.",
                          "Open a workspace first.");
        return;
    }
    base_dir = selected_target_directory(app);
    name = lmme_dialog_prompt_text(GTK_WINDOW(app->window),
                                   "Create New Folder",
                                   "Folder name",
                                   "notes");
    if (name == NULL) {
        return;
    }
    if (!lmme_workspace_create_folder(app->workspace, base_dir, name, &error)) {
        lmme_dialog_error(GTK_WINDOW(app->window),
                          "Could not create folder.",
                          error != NULL ? error->message : NULL);
        return;
    }
    lmme_window_refresh_tree_directory(app, base_dir);
}

static void
action_rename(LmmeApp *app)
{
    g_autofree char *base = NULL;
    g_autofree char *name = NULL;
    g_autofree char *new_path = NULL;
    g_autofree char *old_path = NULL;
    g_autofree char *parent_dir = NULL;
    g_autofree char *rename_target = NULL;
    g_autofree char *trimmed_name = NULL;
    g_autoptr(GError) error = NULL;
    LmmeDocumentPathRemapPlan *remap_plan = NULL;

    if (app->workspace == NULL || app->selection.path == NULL) {
        return;
    }
    old_path = g_strdup(app->selection.path);
    parent_dir = g_path_get_dirname(old_path);
    base = g_path_get_basename(old_path);
    name = lmme_dialog_prompt_text(GTK_WINDOW(app->window), "Rename", "New name", base);
    if (name == NULL) {
        return;
    }
    trimmed_name = g_strdup(name);
    if (!lmme_validate_basename(g_strstrip(trimmed_name), &error)) {
        lmme_dialog_error(GTK_WINDOW(app->window), "Could not rename item.", error->message);
        return;
    }
    rename_target = g_build_filename(parent_dir, trimmed_name, NULL);
    remap_plan = lmme_document_path_remap_plan_new(app,
                                                   old_path,
                                                   rename_target,
                                                   &error);
    if (remap_plan == NULL) {
        lmme_dialog_error(GTK_WINDOW(app->window), "Could not rename item.", error->message);
        return;
    }
    if (!lmme_workspace_rename_path(app->workspace, old_path, name, &new_path, &error)) {
        lmme_document_path_remap_plan_free(remap_plan);
        lmme_dialog_error(GTK_WINDOW(app->window),
                          "Could not rename item.",
                          error != NULL ? error->message : NULL);
        return;
    }
    lmme_document_path_remap_plan_commit(remap_plan);
    lmme_document_path_remap_plan_free(remap_plan);
    lmme_window_refresh_tree_directory(app, parent_dir);
    (void)lmme_file_tree_select_path(app->tree_view, new_path);
}

static void
action_delete(LmmeApp *app)
{
    gboolean dont_show_again = FALSE;
    gboolean force_confirmation = FALSE;
    guint modified_count = 0;
    g_autofree char *path = NULL;
    g_autofree char *parent_dir = NULL;
    g_autoptr(GPtrArray) affected_documents = NULL;
    g_autoptr(GError) error = NULL;

    if (app->workspace == NULL || app->selection.path == NULL) {
        return;
    }
    force_confirmation = lmme_path_context_is_directory(&app->selection) &&
                         directory_is_non_empty(app->selection.path);
    affected_documents = lmme_tabs_find_in_subtree(app, app->selection.path);
    for (guint i = 0; i < affected_documents->len; i++) {
        LmmeDocument *doc = g_ptr_array_index(affected_documents, i);
        if (doc->modified || doc->restored_from_recovery) {
            modified_count++;
        }
    }
    if (modified_count > 0 &&
        !lmme_dialog_confirm_delete_open_documents(GTK_WINDOW(app->window),
                                                   modified_count,
                                                   affected_documents->len)) {
        return;
    }
    if ((app->config.confirm_delete || force_confirmation) &&
        !lmme_dialog_confirm_delete(GTK_WINDOW(app->window), &dont_show_again)) {
        return;
    }
    if (dont_show_again && !force_confirmation) {
        app->config.confirm_delete = FALSE;
    }
    path = g_strdup(app->selection.path);
    parent_dir = g_path_get_dirname(path);
    if (!lmme_workspace_delete_path(app->workspace, path, &error)) {
        for (guint i = 0; i < affected_documents->len; i++) {
            LmmeDocument *doc = g_ptr_array_index(affected_documents, i);
            if (!g_file_test(doc->path, G_FILE_TEST_EXISTS)) {
                doc->disk_state = LMME_DISK_STATE_EXTERNAL_DELETED;
            }
        }
        lmme_dialog_error(GTK_WINDOW(app->window),
                          "Could not delete item.",
                          error != NULL ? error->message : NULL);
        return;
    }
    lmme_tabs_forget_subtree(app, path);
    lmme_window_refresh_tree_directory(app, parent_dir);
    (void)lmme_file_tree_select_path(app->tree_view, parent_dir);
}

static void
action_tree_open(LmmeApp *app)
{
    g_autoptr(GError) error = NULL;

    if (app->tree_context.path != NULL &&
        lmme_path_context_is_markdown(&app->tree_context) &&
        !lmme_tabs_open_file(app, app->tree_context.path, &error)) {
        lmme_dialog_error(GTK_WINDOW(app->window),
                          "Could not read file.",
                          error != NULL ? error->message : NULL);
    }
}

static void
action_tree_close_tab(LmmeApp *app)
{
    LmmeDocument *doc = app->tree_context.path != NULL
                            ? lmme_tabs_find_by_path(app, app->tree_context.path)
                            : NULL;

    if (doc != NULL) {
        (void)lmme_tabs_close_document(app, doc);
    }
}

static void
action_tree_copy_relative_path(LmmeApp *app)
{
    g_autofree char *relative = lmme_tree_context_menu_dup_relative_path(app);

    if (relative != NULL) {
        gdk_clipboard_set_text(gtk_widget_get_clipboard(app->window), relative);
    }
}

static void
action_tree_copy_full_path(LmmeApp *app)
{
    if (app->tree_context.path != NULL) {
        gdk_clipboard_set_text(gtk_widget_get_clipboard(app->window),
                               app->tree_context.path);
    }
}

static void
action_tree_copy_markdown_image_link(LmmeApp *app)
{
    g_autofree char *relative = NULL;
    g_autofree char *link = NULL;

    if (!lmme_path_context_is_image(&app->tree_context) ||
        app->tree_context.path == NULL) {
        return;
    }
    relative = lmme_tree_context_menu_dup_relative_path(app);
    if (relative == NULL) {
        return;
    }
    link = g_strdup_printf("![](%s)", relative);
    gdk_clipboard_set_text(gtk_widget_get_clipboard(app->window), link);
}

#if GTK_CHECK_VERSION(4, 10, 0)
static void
on_uri_launch_finished(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    g_autoptr(GError) error = NULL;
    (void)user_data;

    if (!gtk_uri_launcher_launch_finish(GTK_URI_LAUNCHER(source_object), result, &error)) {
        g_warning("Could not open containing folder: %s",
                  error != NULL ? error->message : "unknown error");
    }
}
#endif

static void
action_tree_open_containing_folder(LmmeApp *app)
{
    g_autofree char *target_dir = NULL;
    g_autoptr(GFile) file = NULL;
    g_autofree char *uri = NULL;

    if (app->tree_context.path == NULL || app->tree_context.empty_area) {
        return;
    }
    target_dir = lmme_path_context_is_directory(&app->tree_context)
                     ? g_strdup(app->tree_context.path)
                     : g_path_get_dirname(app->tree_context.path);
    file = g_file_new_for_path(target_dir);
    uri = g_file_get_uri(file);
#if GTK_CHECK_VERSION(4, 10, 0)
    g_autoptr(GtkUriLauncher) launcher = gtk_uri_launcher_new(uri);
    gtk_uri_launcher_launch(launcher,
                            GTK_WINDOW(app->window),
                            NULL,
                            on_uri_launch_finished,
                            NULL);
#else
    gtk_show_uri(GTK_WINDOW(app->window), uri, GDK_CURRENT_TIME);
#endif
}

gboolean
lmme_command_handle_workspace(LmmeCommandHandler handler,
                              GSimpleAction *action,
                              GVariant *parameter,
                              LmmeApp *app)
{
    (void)action;
    (void)parameter;

    switch ((int)handler) {
    case LMME_COMMAND_HANDLER_NEW_FILE:
    case LMME_COMMAND_HANDLER_TREE_NEW_FILE:
        action_new_file(app);
        return TRUE;
    case LMME_COMMAND_HANDLER_NEW_FOLDER:
    case LMME_COMMAND_HANDLER_TREE_NEW_FOLDER:
        action_new_folder(app);
        return TRUE;
    case LMME_COMMAND_HANDLER_RENAME:
    case LMME_COMMAND_HANDLER_TREE_RENAME:
        action_rename(app);
        return TRUE;
    case LMME_COMMAND_HANDLER_DELETE:
    case LMME_COMMAND_HANDLER_TREE_DELETE:
        action_delete(app);
        return TRUE;
    case LMME_COMMAND_HANDLER_TREE_OPEN:
        action_tree_open(app);
        return TRUE;
    case LMME_COMMAND_HANDLER_TREE_CLOSE_TAB:
        action_tree_close_tab(app);
        return TRUE;
    case LMME_COMMAND_HANDLER_TREE_COPY_RELATIVE_PATH:
        action_tree_copy_relative_path(app);
        return TRUE;
    case LMME_COMMAND_HANDLER_TREE_COPY_FULL_PATH:
        action_tree_copy_full_path(app);
        return TRUE;
    case LMME_COMMAND_HANDLER_TREE_COPY_IMAGE_LINK:
        action_tree_copy_markdown_image_link(app);
        return TRUE;
    case LMME_COMMAND_HANDLER_TREE_OPEN_CONTAINING_FOLDER:
        action_tree_open_containing_folder(app);
        return TRUE;
    default:
        return FALSE;
    }
}
