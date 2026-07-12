#include "document/document_persistence.h"

#include "app/app.h"
#include "document/document_autosave.h"
#include "document/file_monitor.h"
#include "document/recovery.h"
#include "editor/editor.h"
#include "infra/safe_write.h"
#include "infra/util.h"
#include "workspace/workspace.h"

#include <string.h>

static LmmeDocumentSaveResult
document_result_from_write_result(LmmeSafeWriteResult result)
{
    switch (result) {
    case LMME_SAFE_WRITE_COMMITTED_DURABLE:
        return LMME_DOCUMENT_SAVE_COMMITTED_DURABLE;
    case LMME_SAFE_WRITE_COMMITTED_NOT_DURABLE:
        return LMME_DOCUMENT_SAVE_COMMITTED_NOT_DURABLE;
    case LMME_SAFE_WRITE_NOT_COMMITTED:
    default:
        return LMME_DOCUMENT_SAVE_NOT_COMMITTED;
    }
}

LmmeDocumentSaveResult
lmme_document_persist(LmmeDocument *doc,
                      const char *target_path,
                      gboolean allow_external_overwrite,
                      gboolean change_document_path,
                      GError **error)
{
    g_autofree char *canonical_target = NULL;
    g_autofree char *new_relative_path = NULL;
    g_autofree char *old_path = NULL;
    g_autofree char *snapshot = NULL;
    LmmeSafeWriteOutcome write_outcome;
    LmmeDocumentSaveResult result;
    gboolean path_changed = FALSE;
    const char *workspace_path = NULL;

    if (doc == NULL) {
        return LMME_DOCUMENT_SAVE_COMMITTED_DURABLE;
    }
    if (target_path == NULL || target_path[0] == '\0') {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Invalid save destination.");
        return LMME_DOCUMENT_SAVE_NOT_COMMITTED;
    }
    if (!allow_external_overwrite && doc->disk_state != LMME_DISK_STATE_NORMAL) {
        g_set_error_literal(error,
                            G_FILE_ERROR,
                            G_FILE_ERROR_AGAIN,
                            "Resolve the external file conflict before saving.");
        return LMME_DOCUMENT_SAVE_NOT_COMMITTED;
    }

    canonical_target = g_canonicalize_filename(target_path, NULL);
    if (doc->app->workspace != NULL &&
        !lmme_workspace_validate_save_target(doc->app->workspace,
                                             canonical_target,
                                             error)) {
        return LMME_DOCUMENT_SAVE_NOT_COMMITTED;
    }
    if (change_document_path && doc->app->workspace == NULL) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Invalid Save As destination.");
        return LMME_DOCUMENT_SAVE_NOT_COMMITTED;
    }

    path_changed = g_strcmp0(doc->path, canonical_target) != 0;
    old_path = g_strdup(doc->path);
    if (path_changed) {
        new_relative_path = doc->app->workspace != NULL
                                ? lmme_path_relative_to(doc->app->workspace->path, canonical_target)
                                : g_path_get_basename(canonical_target);
    }
    snapshot = lmme_editor_dup_text(GTK_TEXT_BUFFER(doc->buffer));

    write_outcome = lmme_safe_write_file(canonical_target,
                                         snapshot,
                                         strlen(snapshot),
                                         error);
    result = document_result_from_write_result(write_outcome.result);
    if (result == LMME_DOCUMENT_SAVE_NOT_COMMITTED) {
        return result;
    }

    if (path_changed) {
        lmme_document_file_monitor_detach(doc);
        g_free(doc->path);
        doc->path = g_steal_pointer(&canonical_target);
        g_free(doc->relative_path);
        doc->relative_path = g_steal_pointer(&new_relative_path);
    }
    doc->expected_internal_fingerprint = write_outcome.fingerprint;
    doc->last_known_fingerprint = write_outcome.fingerprint;
    doc->base_fingerprint = write_outcome.fingerprint;
    doc->has_expected_internal_fingerprint = TRUE;
    doc->disk_state = LMME_DISK_STATE_NORMAL;
    if (path_changed) {
        lmme_document_file_monitor_attach(doc);
        lmme_document_refresh_title(doc);
    }

    lmme_document_cancel_autosave(doc);
    lmme_document_cancel_recovery(doc);
    if (result == LMME_DOCUMENT_SAVE_COMMITTED_DURABLE) {
        gtk_text_buffer_set_modified(GTK_TEXT_BUFFER(doc->buffer), FALSE);
        (void)lmme_recovery_remove(doc->app->recovery_store, doc->path, NULL);
        if (path_changed) {
            (void)lmme_recovery_remove(doc->app->recovery_store, old_path, NULL);
        }
        g_clear_pointer(&doc->recovery_source_path, g_free);
        doc->restored_from_recovery = FALSE;
        lmme_document_set_save_state(doc, LMME_SAVE_STATE_SAVED);
        return result;
    }

    gtk_text_buffer_set_modified(GTK_TEXT_BUFFER(doc->buffer), TRUE);
    lmme_document_set_save_state(doc, LMME_SAVE_STATE_ERROR);
    workspace_path = doc->app->workspace != NULL ? doc->app->workspace->path : NULL;
    if (lmme_recovery_write(doc->app->recovery_store,
                            doc->path,
                            workspace_path,
                            &doc->base_fingerprint,
                            snapshot,
                            strlen(snapshot),
                            NULL)) {
        if (path_changed) {
            (void)lmme_recovery_remove(doc->app->recovery_store, old_path, NULL);
        }
        g_free(doc->recovery_source_path);
        doc->recovery_source_path = lmme_recovery_path_for_original(doc->app->recovery_store,
                                                                    doc->path);
    }
    return result;
}
