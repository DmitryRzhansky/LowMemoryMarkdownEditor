#include "document/tabs.h"
#include "document/tabs_test.h"

#include "app/app.h"
#include "command/command_actions.h"
#include "document/document_autosave.h"
#include "document/file_io.h"
#include "document/file_monitor.h"
#include "document/recovery.h"
#include "infra/dialogs.h"
#include "infra/util.h"
#include "ui/tab_context_menu.h"
#include "ui/window.h"
#include "workspace/workspace.h"

static void
on_close_tab_clicked(GtkButton *button, gpointer user_data)
{
    LmmeDocument *doc = user_data;
    GtkNotebook *notebook = GTK_NOTEBOOK(doc->app->notebook);
    int page = gtk_notebook_page_num(notebook, doc->scroller);

    (void)button;
    if (page >= 0) {
        gtk_notebook_set_current_page(notebook, page);
        lmme_tabs_close_active(doc->app);
    }
}

static GtkWidget *
create_tab_label(LmmeDocument *doc)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    GtkWidget *label = gtk_label_new("");
    GtkWidget *close = gtk_button_new_from_icon_name("window-close-symbolic");

    gtk_widget_add_css_class(box, "tab-label");
    gtk_widget_add_css_class(close, "tab-close");
    gtk_button_set_has_frame(GTK_BUTTON(close), FALSE);
    gtk_widget_set_size_request(close, 18, 18);
    gtk_widget_set_tooltip_text(close, "Close");
    gtk_box_append(GTK_BOX(box), label);
    gtk_box_append(GTK_BOX(box), close);
    g_signal_connect(close, "clicked", G_CALLBACK(on_close_tab_clicked), doc);

    doc->title_label = label;
    doc->tab_box = box;
    lmme_document_refresh_title(doc);
    lmme_tab_context_menu_attach(doc, box);
    return box;
}

LmmePreviewApplyResult
lmme_tabs_set_preview_visible(LmmeApp *app, gboolean visible)
{
    LmmeDocument *active = app != NULL ? lmme_tabs_get_active(app) : NULL;
    LmmePreviewApplyResult active_result = LMME_PREVIEW_APPLY_OK;

    if (app == NULL || app->documents == NULL) {
        return LMME_PREVIEW_APPLY_FAILED;
    }

    for (guint i = 0; i < app->documents->len; i++) {
        LmmeDocument *doc = g_ptr_array_index(app->documents, i);
        if (visible) {
            doc->preview_dirty = TRUE;
            gtk_widget_add_css_class(doc->source_view, "preview-edit-mode");
            if (doc == active) {
                active_result = lmme_document_set_preview_visible(doc, TRUE);
            }
        } else {
            LmmePreviewApplyResult result = lmme_document_set_preview_visible(doc, FALSE);
            if (doc == active) {
                active_result = result;
            }
        }
    }

    return active_result;
}

LmmeDocument *
lmme_tabs_find_by_path(LmmeApp *app, const char *path)
{
    g_autofree char *canon = NULL;

    if (app == NULL || app->documents == NULL || path == NULL) {
        return NULL;
    }

    canon = g_canonicalize_filename(path, NULL);
    for (guint i = 0; i < app->documents->len; i++) {
        LmmeDocument *doc = g_ptr_array_index(app->documents, i);
        if (g_strcmp0(doc->path, canon) == 0) {
            return doc;
        }
    }

    return NULL;
}

LmmeDocument *
lmme_tabs_find_by_id(LmmeApp *app, guint64 document_id)
{
    if (app == NULL || app->documents == NULL || document_id == 0) {
        return NULL;
    }
    for (guint i = 0; i < app->documents->len; i++) {
        LmmeDocument *doc = g_ptr_array_index(app->documents, i);
        if (doc->id == document_id) {
            return doc;
        }
    }
    return NULL;
}

LmmeDocument *
lmme_tabs_get_active(LmmeApp *app)
{
    int page = -1;
    GtkWidget *child = NULL;

    if (app == NULL || app->notebook == NULL || app->documents == NULL ||
        !GTK_IS_NOTEBOOK(app->notebook)) {
        return NULL;
    }

    page = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook));
    child = page >= 0 ? gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), page) : NULL;
    if (child == NULL) {
        return NULL;
    }

    for (guint i = 0; i < app->documents->len; i++) {
        LmmeDocument *doc = g_ptr_array_index(app->documents, i);
        if (doc->scroller == child) {
            return doc;
        }
    }

    return NULL;
}

static gboolean
open_document_contents(const char *path, char **contents, GError **error)
{
    return lmme_file_read_utf8(path, LMME_DOCUMENT_MAX_OPEN_BYTES, contents, NULL, error);
}

static gboolean
open_recovery_contents(const char *path, char **contents, GError **error)
{
    return lmme_file_read_utf8(path, G_MAXINT, contents, NULL, error);
}

static void
add_document_to_notebook(LmmeApp *app, LmmeDocument *doc)
{
    GtkWidget *label = create_tab_label(doc);
    int page = gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), doc->scroller, label);

    g_ptr_array_add(app->documents, doc);
    gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(app->notebook), doc->scroller, TRUE);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), page);
    gtk_widget_grab_focus(doc->source_view);
    lmme_window_update_status(app);
    lmme_window_schedule_preview(app);
    lmme_command_actions_refresh(app);
}

gboolean
lmme_tabs_open_file(LmmeApp *app, const char *path, GError **error)
{
    LmmeDocument *existing = lmme_tabs_find_by_path(app, path);
    g_autofree char *contents = NULL;

    if (existing != NULL) {
        int page = gtk_notebook_page_num(GTK_NOTEBOOK(app->notebook), existing->scroller);
        gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), page);
        lmme_command_actions_refresh(app);
        return TRUE;
    }
    if (!open_document_contents(path, &contents, error)) {
        return FALSE;
    }

    add_document_to_notebook(app, lmme_document_new(app, path, contents, NULL));
    return TRUE;
}

gboolean
lmme_tabs_open_recovery_entry(LmmeApp *app,
                              const LmmeRecoveryEntry *entry,
                              GError **error)
{
    g_autofree char *contents = NULL;
    g_autoptr(GError) fingerprint_error = NULL;
    LmmeDocument *doc = NULL;
    gboolean original_changed = TRUE;

    if (app == NULL || entry == NULL || entry->original_path == NULL || entry->recovery_path == NULL) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Invalid recovery entry.");
        return FALSE;
    }
    if (lmme_tabs_find_by_path(app, entry->original_path) != NULL) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_EXIST, "The original file is already open.");
        return FALSE;
    }
    if (!open_recovery_contents(entry->recovery_path, &contents, error)) {
        return FALSE;
    }

    original_changed = lmme_recovery_entry_original_changed(entry, &fingerprint_error);
    doc = lmme_document_new(app, entry->original_path, contents, entry->original_path);
    lmme_document_mark_recovered(doc, entry->recovery_path, original_changed);
    if (!entry->legacy) {
        doc->base_fingerprint = entry->original_fingerprint;
    }
    add_document_to_notebook(app, doc);
    return TRUE;
}

LmmeDocumentSaveResult
lmme_tabs_save_document(LmmeDocument *doc, GError **error)
{
    return lmme_document_save(doc, error);
}

LmmeDocumentSaveResult
lmme_tabs_save_active(LmmeApp *app, GError **error)
{
    return lmme_tabs_save_document(lmme_tabs_get_active(app), error);
}

static void
remove_document(LmmeApp *app, LmmeDocument *doc)
{
    if (app->tab_context_document == doc) {
        app->tab_context_document = NULL;
    }

    for (guint i = 0; i < app->documents->len; i++) {
        if (g_ptr_array_index(app->documents, i) == doc) {
            g_ptr_array_remove_index(app->documents, i);
            break;
        }
    }
    lmme_document_free(doc);
}

static gboolean
prepare_document_close(LmmeDocument *doc)
{
    if (doc == NULL) {
        return TRUE;
    }
    doc->pending_close = LMME_PENDING_CLOSE_NONE;
    if (!doc->modified && !doc->restored_from_recovery &&
        doc->disk_state == LMME_DISK_STATE_NORMAL) {
        return TRUE;
    }

    for (;;) {
        g_autoptr(GError) recovery_error = NULL;
        g_autoptr(GError) save_error = NULL;
        gboolean recovery_ok = lmme_document_flush_recovery(doc, &recovery_error);
        gboolean can_save_directly = doc->disk_state == LMME_DISK_STATE_NORMAL;
        LmmeDocumentSaveResult save_result = LMME_DOCUMENT_SAVE_NOT_COMMITTED;
        const char *detail = NULL;

        if (can_save_directly) {
            save_result = lmme_document_save(doc, &save_error);
        }
        if (save_result == LMME_DOCUMENT_SAVE_COMMITTED_DURABLE &&
            !doc->modified && !doc->recovery_failed) {
            return TRUE;
        }
        recovery_ok = !doc->recovery_failed;
        if (!recovery_ok) {
            detail = recovery_error != NULL
                         ? recovery_error->message
                         : "Recovery data could not be written.";
        } else if (!can_save_directly) {
            detail = "The file has an unresolved external change. Recovery was kept; resolve the conflict before overwriting the file.";
        } else if (save_error != NULL) {
            detail = save_error->message;
        }

        LmmeSaveFailureChoice choice = lmme_dialog_resolve_save_failure(
            GTK_WINDOW(doc->app->window),
            doc->relative_path,
            detail,
            TRUE,
            recovery_ok,
            TRUE);
        if (choice == LMME_SAVE_FAILURE_RETRY) {
            continue;
        }
        if (choice == LMME_SAVE_FAILURE_KEEP_RECOVERY) {
            doc->pending_close = LMME_PENDING_CLOSE_KEEP_RECOVERY;
            return TRUE;
        }
        if (choice == LMME_SAVE_FAILURE_DISCARD_LOCAL) {
            doc->pending_close = LMME_PENDING_CLOSE_DISCARD_LOCAL;
            return TRUE;
        }
        doc->pending_close = LMME_PENDING_CLOSE_NONE;
        return FALSE;
    }
}

gboolean
lmme_tabs_test_prepare_documents(GPtrArray *documents,
                                 LmmeTabsTestPrepareFunc prepare,
                                 gpointer user_data)
{
    if (documents == NULL || prepare == NULL) {
        return FALSE;
    }
    for (guint i = 0; i < documents->len; i++) {
        LmmeDocument *doc = g_ptr_array_index(documents, i);
        if (doc != NULL) {
            doc->pending_close = LMME_PENDING_CLOSE_NONE;
        }
    }
    for (guint i = 0; i < documents->len; i++) {
        if (!prepare(g_ptr_array_index(documents, i), user_data)) {
            for (guint j = 0; j < documents->len; j++) {
                LmmeDocument *doc = g_ptr_array_index(documents, j);
                if (doc != NULL) {
                    doc->pending_close = LMME_PENDING_CLOSE_NONE;
                }
            }
            return FALSE;
        }
    }
    return TRUE;
}

static gboolean
prepare_document_close_adapter(LmmeDocument *doc, gpointer user_data)
{
    (void)user_data;
    return prepare_document_close(doc);
}

static LmmeDocument *
document_for_notebook_page(LmmeApp *app, int page)
{
    GtkWidget *child = gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), page);

    if (child == NULL) {
        return NULL;
    }
    for (guint i = 0; i < app->documents->len; i++) {
        LmmeDocument *doc = g_ptr_array_index(app->documents, i);
        if (doc->scroller == child) {
            return doc;
        }
    }
    return NULL;
}

static GPtrArray *
snapshot_notebook_range(LmmeApp *app,
                        int first_page,
                        int last_page,
                        LmmeDocument *excluded)
{
    GPtrArray *documents = g_ptr_array_new();

    for (int page = first_page; page <= last_page; page++) {
        LmmeDocument *doc = document_for_notebook_page(app, page);
        if (doc != NULL && doc != excluded) {
            g_ptr_array_add(documents, doc);
        }
    }
    return documents;
}

static gboolean
prepare_document_snapshot(GPtrArray *documents)
{
    return lmme_tabs_test_prepare_documents(documents,
                                            prepare_document_close_adapter,
                                            NULL);
}

static gboolean
commit_close_disposition(LmmeDocument *doc, GError **error)
{
    if (doc == NULL) {
        return TRUE;
    }
    if (doc->pending_close == LMME_PENDING_CLOSE_KEEP_RECOVERY) {
        doc->pending_close = LMME_PENDING_CLOSE_NONE;
        return TRUE;
    }
    if (doc->pending_close == LMME_PENDING_CLOSE_DISCARD_LOCAL) {
        g_autoptr(GError) local_error = NULL;

        if (!lmme_recovery_remove(doc->app->recovery_store, doc->path, &local_error)) {
            g_propagate_prefixed_error(error,
                                       g_steal_pointer(&local_error),
                                       "Could not discard recovery data for \"%s\"",
                                       doc->relative_path != NULL ? doc->relative_path : doc->path);
            return FALSE;
        }
        doc->pending_close = LMME_PENDING_CLOSE_NONE;
    }
    return TRUE;
}

gboolean
lmme_tabs_test_commit_close_disposition(LmmeDocument *doc, GError **error)
{
    return commit_close_disposition(doc, error);
}

static gboolean
commit_dispositions_for_documents(GPtrArray *documents, GError **error)
{
    if (documents == NULL) {
        return TRUE;
    }
    for (guint i = 0; i < documents->len; i++) {
        LmmeDocument *doc = g_ptr_array_index(documents, i);
        if (doc->pending_close == LMME_PENDING_CLOSE_NONE) {
            continue;
        }
        if (!commit_close_disposition(doc, error)) {
            return FALSE;
        }
    }
    return TRUE;
}

gboolean
lmme_tabs_commit_pending_dispositions(LmmeApp *app, GError **error)
{
    if (app == NULL || app->documents == NULL) {
        return TRUE;
    }
    for (guint i = 0; i < app->documents->len; i++) {
        LmmeDocument *doc = g_ptr_array_index(app->documents, i);
        if (doc->pending_close == LMME_PENDING_CLOSE_NONE) {
            continue;
        }
        if (!commit_close_disposition(doc, error)) {
            return FALSE;
        }
    }
    return TRUE;
}

static void
remove_documents_from_snapshot(LmmeApp *app,
                             GPtrArray *documents,
                             LmmeDocument *anchor)
{
    for (guint i = 0; i < documents->len; i++) {
        LmmeDocument *doc = g_ptr_array_index(documents, i);
        int page = gtk_notebook_page_num(GTK_NOTEBOOK(app->notebook), doc->scroller);

        if (page >= 0) {
            gtk_notebook_remove_page(GTK_NOTEBOOK(app->notebook), page);
        }
        remove_document(app, doc);
    }

    if (anchor != NULL) {
        int anchor_page = gtk_notebook_page_num(GTK_NOTEBOOK(app->notebook), anchor->scroller);
        if (anchor_page >= 0) {
            gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), anchor_page);
        }
    }
    lmme_window_update_status(app);
    lmme_window_schedule_preview(app);
}

static gboolean
commit_document_snapshot(LmmeApp *app,
                         GPtrArray *documents,
                         LmmeDocument *anchor,
                         GError **error)
{
    if (!commit_dispositions_for_documents(documents, error)) {
        return FALSE;
    }
    remove_documents_from_snapshot(app, documents, anchor);
    return TRUE;
}

static gboolean
close_document_snapshot(LmmeApp *app,
                        GPtrArray *documents,
                        LmmeDocument *anchor)
{
    g_autoptr(GError) error = NULL;

    if (!prepare_document_snapshot(documents)) {
        return FALSE;
    }
    if (!commit_document_snapshot(app, documents, anchor, &error)) {
        if (app->window != NULL) {
            lmme_dialog_error(GTK_WINDOW(app->window),
                              "Could not close documents.",
                              error != NULL ? error->message : NULL);
        }
        return FALSE;
    }
    return TRUE;
}

void
lmme_tabs_close_active(LmmeApp *app)
{
    LmmeDocument *doc = lmme_tabs_get_active(app);
    int page = doc != NULL ? gtk_notebook_page_num(GTK_NOTEBOOK(app->notebook), doc->scroller) : -1;
    g_autoptr(GError) error = NULL;

    if (doc == NULL || page < 0) {
        return;
    }

    if (!prepare_document_close(doc)) {
        return;
    }
    if (!commit_close_disposition(doc, &error)) {
        if (app->window != NULL) {
            lmme_dialog_error(GTK_WINDOW(app->window),
                              "Could not close the document.",
                              error != NULL ? error->message : NULL);
        }
        return;
    }
    gtk_notebook_remove_page(GTK_NOTEBOOK(app->notebook), page);
    remove_document(app, doc);
    lmme_window_update_status(app);
    lmme_window_schedule_preview(app);
    lmme_command_actions_refresh(app);
}

gboolean
lmme_tabs_close_document(LmmeApp *app, LmmeDocument *doc)
{
    GtkNotebook *notebook = NULL;
    int page = -1;
    guint before = 0;

    if (app == NULL || doc == NULL || app->notebook == NULL || app->documents == NULL) {
        return FALSE;
    }

    notebook = GTK_NOTEBOOK(app->notebook);
    page = gtk_notebook_page_num(notebook, doc->scroller);
    if (page < 0) {
        return FALSE;
    }

    before = app->documents->len;
    gtk_notebook_set_current_page(notebook, page);
    lmme_tabs_close_active(app);
    return app->documents->len < before;
}

gboolean
lmme_tabs_close_tabs_to_right(LmmeApp *app, LmmeDocument *anchor)
{
    GtkNotebook *notebook = NULL;
    int anchor_page = -1;
    g_autoptr(GPtrArray) documents = NULL;

    if (app == NULL || anchor == NULL || app->notebook == NULL || app->documents == NULL) {
        return FALSE;
    }

    notebook = GTK_NOTEBOOK(app->notebook);
    anchor_page = gtk_notebook_page_num(notebook, anchor->scroller);
    if (anchor_page < 0) {
        return FALSE;
    }

    documents = snapshot_notebook_range(app,
                                        anchor_page + 1,
                                        gtk_notebook_get_n_pages(notebook) - 1,
                                        NULL);
    return close_document_snapshot(app, documents, anchor);
}

gboolean
lmme_tabs_close_tabs_to_left(LmmeApp *app, LmmeDocument *anchor)
{
    GtkNotebook *notebook = NULL;
    int anchor_page = -1;
    g_autoptr(GPtrArray) documents = NULL;

    if (app == NULL || anchor == NULL || app->notebook == NULL || app->documents == NULL) {
        return FALSE;
    }

    notebook = GTK_NOTEBOOK(app->notebook);
    anchor_page = gtk_notebook_page_num(notebook, anchor->scroller);
    if (anchor_page < 0) {
        return FALSE;
    }

    documents = snapshot_notebook_range(app, 0, anchor_page - 1, NULL);
    return close_document_snapshot(app, documents, anchor);
}

gboolean
lmme_tabs_close_other_tabs(LmmeApp *app, LmmeDocument *anchor)
{
    GtkNotebook *notebook = NULL;
    int anchor_page = -1;
    g_autoptr(GPtrArray) documents = NULL;

    if (app == NULL || anchor == NULL || app->notebook == NULL || app->documents == NULL) {
        return FALSE;
    }
    notebook = GTK_NOTEBOOK(app->notebook);
    anchor_page = gtk_notebook_page_num(notebook, anchor->scroller);
    if (anchor_page < 0) {
        return FALSE;
    }
    documents = snapshot_notebook_range(app,
                                        0,
                                        gtk_notebook_get_n_pages(notebook) - 1,
                                        anchor);
    return close_document_snapshot(app, documents, anchor);
}

gboolean
lmme_tabs_close_all(LmmeApp *app)
{
    g_autoptr(GPtrArray) documents = NULL;

    if (app == NULL || app->notebook == NULL || app->documents == NULL) {
        return FALSE;
    }
    documents = snapshot_notebook_range(app,
                                        0,
                                        gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook)) - 1,
                                        NULL);
    return close_document_snapshot(app, documents, NULL);
}

gboolean
lmme_tabs_prepare_close_all(LmmeApp *app)
{
    if (app == NULL || app->documents == NULL) {
        return TRUE;
    }
    return prepare_document_snapshot(app->documents);
}

gboolean
lmme_tabs_commit_close_all(LmmeApp *app, GError **error)
{
    g_autoptr(GPtrArray) documents = NULL;

    if (app == NULL || app->notebook == NULL || app->documents == NULL) {
        return TRUE;
    }
    documents = snapshot_notebook_range(app,
                                        0,
                                        gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook)) - 1,
                                        NULL);
    if (!commit_dispositions_for_documents(documents, error)) {
        return FALSE;
    }
    remove_documents_from_snapshot(app, documents, NULL);
    return TRUE;
}

void
lmme_tabs_resume_pending_saves(LmmeApp *app)
{
    if (app == NULL || app->documents == NULL) {
        return;
    }
    for (guint i = 0; i < app->documents->len; i++) {
        LmmeDocument *doc = g_ptr_array_index(app->documents, i);
        if (doc->modified || doc->restored_from_recovery ||
            doc->disk_state != LMME_DISK_STATE_NORMAL) {
            lmme_document_schedule_recovery(doc);
            lmme_document_schedule_autosave(doc);
        }
    }
}

void
lmme_tabs_close_path(LmmeApp *app, const char *path)
{
    lmme_tabs_forget_subtree(app, path);
}

GPtrArray *
lmme_tabs_find_in_subtree(LmmeApp *app, const char *root)
{
    GPtrArray *documents = g_ptr_array_new();
    g_autofree char *canonical_root = NULL;

    if (app == NULL || app->documents == NULL || root == NULL) {
        return documents;
    }
    canonical_root = g_canonicalize_filename(root, NULL);
    for (guint i = 0; i < app->documents->len; i++) {
        LmmeDocument *doc = g_ptr_array_index(app->documents, i);
        if (lmme_path_is_inside(canonical_root, doc->path)) {
            g_ptr_array_add(documents, doc);
        }
    }
    return documents;
}

void
lmme_tabs_forget_subtree(LmmeApp *app, const char *root)
{
    g_autoptr(GPtrArray) documents = lmme_tabs_find_in_subtree(app, root);

    for (guint i = 0; i < documents->len; i++) {
        LmmeDocument *doc = g_ptr_array_index(documents, i);
        int page = gtk_notebook_page_num(GTK_NOTEBOOK(app->notebook), doc->scroller);
        if (page >= 0) {
            gtk_notebook_remove_page(GTK_NOTEBOOK(app->notebook), page);
        }
        if (!lmme_recovery_remove(app->recovery_store, doc->path, NULL)) {
            g_warning("Could not remove recovery data while forgetting subtree for \"%s\"",
                      doc->relative_path != NULL ? doc->relative_path : doc->path);
        }
        remove_document(app, doc);
    }
    lmme_window_update_status(app);
    lmme_window_schedule_preview(app);
}

GPtrArray *
lmme_tabs_open_paths(LmmeApp *app)
{
    GPtrArray *paths = g_ptr_array_new_with_free_func(g_free);

    if (app == NULL || app->documents == NULL) {
        return paths;
    }

    for (guint i = 0; i < app->documents->len; i++) {
        LmmeDocument *doc = g_ptr_array_index(app->documents, i);
        if (doc->path != NULL && app->workspace != NULL &&
            lmme_path_is_inside(app->workspace->path, doc->path)) {
            g_ptr_array_add(paths, g_strdup(doc->path));
        }
    }

    return paths;
}
