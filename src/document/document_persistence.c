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

static void
note_recovery_cleanup_failure(LmmeDocument *doc, const char *context, GError *error)
{
    if (doc == NULL) {
        return;
    }
    doc->recovery_cleanup_failed = TRUE;
    g_warning("%s: %s",
              context != NULL ? context : "Recovery cleanup failed",
              error != NULL ? error->message : "unknown error");
}

static gboolean
remove_recovery_for_document(LmmeDocument *doc,
                             const char *original_path,
                             gboolean blocking,
                             const char *context,
                             GError **error)
{
    g_autoptr(GError) local_error = NULL;

    if (doc == NULL || original_path == NULL) {
        return TRUE;
    }
    if (lmme_recovery_remove(doc->app->recovery_store, original_path, &local_error)) {
        return TRUE;
    }
    if (blocking) {
        g_propagate_error(error, g_steal_pointer(&local_error));
        return FALSE;
    }
    note_recovery_cleanup_failure(doc, context, local_error);
    return FALSE;
}

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
    guint64 snapshot_revision = 0;

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
    snapshot_revision = doc->content_revision;

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
        if (snapshot_revision != doc->content_revision) {
            lmme_document_set_save_state(doc, LMME_SAVE_STATE_MODIFIED);
            lmme_document_schedule_recovery(doc);
            return result;
        }
        gtk_text_buffer_set_modified(GTK_TEXT_BUFFER(doc->buffer), FALSE);
        doc->restored_from_recovery = FALSE;
        if (!remove_recovery_for_document(doc,
                                          doc->path,
                                          FALSE,
                                          "Could not remove recovery data after save",
                                          NULL)) {
            doc->recovery_cleanup_failed = TRUE;
            lmme_document_set_save_state(doc, LMME_SAVE_STATE_SAVED);
            return result;
        }
        if (path_changed &&
            !remove_recovery_for_document(doc,
                                        old_path,
                                        FALSE,
                                        "Could not remove recovery data for the previous path after save",
                                        NULL)) {
            doc->recovery_cleanup_failed = TRUE;
            lmme_document_set_save_state(doc, LMME_SAVE_STATE_SAVED);
            return result;
        }
        doc->recovery_cleanup_failed = FALSE;
        g_clear_pointer(&doc->recovery_source_path, g_free);
        doc->recovery_failed = FALSE;
        lmme_document_set_save_state(doc, LMME_SAVE_STATE_SAVED);
        return result;
    }

    gtk_text_buffer_set_modified(GTK_TEXT_BUFFER(doc->buffer), TRUE);
    lmme_document_set_save_state(doc, LMME_SAVE_STATE_ERROR);
    if (lmme_document_write_recovery_snapshot(doc,
                                              snapshot,
                                              strlen(snapshot),
                                              snapshot_revision,
                                              NULL)) {
        if (path_changed) {
            (void)remove_recovery_for_document(doc,
                                               old_path,
                                               FALSE,
                                               "Could not remove recovery data for the previous path",
                                               NULL);
        }
        g_free(doc->recovery_source_path);
        doc->recovery_source_path = lmme_recovery_path_for_original(doc->app->recovery_store,
                                                                    doc->path);
    }
    return result;
}
