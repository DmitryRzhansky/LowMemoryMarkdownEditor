#include "document/file_monitor.h"

#include "app/app.h"
#include "document/document.h"
#include "document/document_autosave.h"
#include "document/document.h"
#include "document/file_io.h"
#include "infra/file_fingerprint.h"
#include "document/recovery.h"
#include "infra/util.h"
#include "ui/external_conflict.h"
#include "ui/window.h"

#include <string.h>

static gboolean
is_relevant_event(GFileMonitorEvent event_type)
{
    return event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT ||
           event_type == G_FILE_MONITOR_EVENT_CREATED ||
           event_type == G_FILE_MONITOR_EVENT_DELETED ||
           event_type == G_FILE_MONITOR_EVENT_MOVED_IN ||
           event_type == G_FILE_MONITOR_EVENT_MOVED_OUT ||
           event_type == G_FILE_MONITOR_EVENT_RENAMED;
}

gboolean
lmme_document_reload_from_disk(LmmeDocument *doc,
                               const LmmeFileFingerprint *fingerprint,
                               GError **error)
{
    g_autofree char *contents = NULL;
    gsize length = 0;

    if (doc == NULL || fingerprint == NULL) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Invalid reload request.");
        return FALSE;
    }
    if (!lmme_file_read_utf8(doc->path, LMME_DOCUMENT_MAX_OPEN_BYTES, &contents, &length, error)) {
        return FALSE;
    }

    g_signal_handler_block(doc->buffer, doc->changed_handler_id);
    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(doc->buffer), contents, (gint)length);
    gtk_text_buffer_set_modified(GTK_TEXT_BUFFER(doc->buffer), FALSE);
    g_signal_handler_unblock(doc->buffer, doc->changed_handler_id);
    lmme_document_cancel_autosave(doc);
    lmme_document_cancel_recovery(doc);
    if (!lmme_recovery_remove(doc->app->recovery_store, doc->path, error)) {
        doc->recovery_cleanup_failed = TRUE;
        g_warning("Could not remove recovery data after external reload: %s",
                  error != NULL ? (*error)->message : "unknown error");
    } else {
        doc->recovery_cleanup_failed = FALSE;
        g_clear_pointer(&doc->recovery_source_path, g_free);
    }
    doc->disk_state = LMME_DISK_STATE_NORMAL;
    doc->has_expected_internal_fingerprint = FALSE;
    doc->last_known_fingerprint = *fingerprint;
    doc->base_fingerprint = *fingerprint;
    doc->content_revision++;
    doc->restored_from_recovery = FALSE;
    doc->recovery_failed = FALSE;
    lmme_document_set_save_state(doc, LMME_SAVE_STATE_SAVED);
    lmme_document_mark_stats_dirty(doc);
    lmme_document_request_stats_update(doc);
    if (doc->app->preview_enabled) {
        (void)lmme_document_set_preview_visible(doc, TRUE);
    }
    return TRUE;
}

LmmeFileChangeAction
lmme_file_change_decide(const LmmeFileFingerprint *current,
                        const LmmeFileFingerprint *last_known,
                        const LmmeFileFingerprint *expected_internal,
                        gboolean has_expected_internal,
                        gboolean modified,
                        gboolean restored,
                        LmmeDiskState disk_state)
{
    (void)disk_state;

    if (current == NULL || last_known == NULL) {
        return LMME_FILE_CHANGE_IGNORE;
    }
    if (has_expected_internal && expected_internal != NULL &&
        lmme_file_fingerprint_equal(current, expected_internal)) {
        return LMME_FILE_CHANGE_INTERNAL_WRITE;
    }
    if (lmme_file_fingerprint_equal(current, last_known)) {
        return LMME_FILE_CHANGE_IGNORE;
    }
    if (!current->exists) {
        return LMME_FILE_CHANGE_EXTERNAL_DELETED;
    }
    if (!modified && !restored) {
        return LMME_FILE_CHANGE_RELOAD;
    }
    return LMME_FILE_CHANGE_EXTERNAL_CHANGED;
}

gboolean
lmme_external_conflict_reload_allowed(gboolean file_exists,
                                      gboolean recovery_durable)
{
    return file_exists && recovery_durable;
}

static void
note_external_conflict(LmmeDocument *doc)
{
    doc->external_change_generation++;
    lmme_external_conflict_request(doc);
}

static void
on_file_monitor_changed(GFileMonitor *monitor,
                        GFile *file,
                        GFile *other_file,
                        GFileMonitorEvent event_type,
                        gpointer user_data)
{
    LmmeDocument *doc = user_data;
    LmmeFileFingerprint current = {0};
    LmmeFileChangeAction action;
    g_autoptr(GError) error = NULL;

    (void)monitor;
    (void)file;
    (void)other_file;

    if (!is_relevant_event(event_type) ||
        !lmme_file_fingerprint_read(doc->path, &current, &error)) {
        return;
    }
    action = lmme_file_change_decide(&current,
                                     &doc->last_known_fingerprint,
                                     &doc->expected_internal_fingerprint,
                                     doc->has_expected_internal_fingerprint,
                                     doc->modified,
                                     doc->restored_from_recovery,
                                     doc->disk_state);
    if (action == LMME_FILE_CHANGE_IGNORE) {
        return;
    }
    if (action == LMME_FILE_CHANGE_INTERNAL_WRITE) {
        doc->last_known_fingerprint = current;
        doc->has_expected_internal_fingerprint = FALSE;
        return;
    }

    doc->last_known_fingerprint = current;
    if (action == LMME_FILE_CHANGE_EXTERNAL_DELETED) {
        doc->disk_state = LMME_DISK_STATE_EXTERNAL_DELETED;
        lmme_document_cancel_autosave(doc);
        lmme_window_update_status(doc->app);
        note_external_conflict(doc);
        return;
    }

    if (action == LMME_FILE_CHANGE_RELOAD) {
        if (!lmme_document_reload_from_disk(doc, &current, &error)) {
            lmme_window_set_status_error(doc->app, "Could not reload externally changed file");
        }
        return;
    }

    doc->disk_state = LMME_DISK_STATE_EXTERNAL_CHANGED;
    lmme_document_cancel_autosave(doc);
    lmme_window_update_status(doc->app);
    note_external_conflict(doc);
}

void
lmme_document_file_monitor_attach(LmmeDocument *doc)
{
    g_autoptr(GFile) file = NULL;
    g_autoptr(GError) error = NULL;

    if (doc == NULL || doc->path == NULL) {
        return;
    }
    lmme_document_file_monitor_detach(doc);
    file = g_file_new_for_path(doc->path);
    doc->monitor = g_file_monitor_file(file, G_FILE_MONITOR_NONE, NULL, &error);
    if (doc->monitor != NULL) {
        g_signal_connect(doc->monitor, "changed", G_CALLBACK(on_file_monitor_changed), doc);
    }
}

void
lmme_document_file_monitor_detach(LmmeDocument *doc)
{
    if (doc != NULL && doc->monitor != NULL) {
        g_file_monitor_cancel(doc->monitor);
        g_clear_object(&doc->monitor);
    }
}
