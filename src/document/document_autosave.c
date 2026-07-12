#include "document/document_autosave.h"
#include "document/document_autosave_test.h"

#include "app/app.h"
#include "document/document.h"
#include "ui/window.h"

static gboolean
recovery_timeout_cb(gpointer user_data)
{
    LmmeDocument *doc = user_data;
    g_autoptr(GError) error = NULL;

    doc->recovery_id = 0;
    if (!lmme_document_flush_recovery(doc, &error)) {
        g_warning("Could not write recovery file: %s", error != NULL ? error->message : "unknown error");
        lmme_window_set_status_error(doc->app, "Could not write recovery data.");
    }

    return G_SOURCE_REMOVE;
}

gboolean
lmme_document_test_run_recovery_timeout(LmmeDocument *doc)
{
    if (doc == NULL) {
        return FALSE;
    }
    lmme_document_cancel_recovery(doc);
    (void)recovery_timeout_cb(doc);
    return !doc->recovery_failed && doc->recovery_id == 0;
}

static gboolean
autosave_timeout_cb(gpointer user_data)
{
    LmmeDocument *doc = user_data;
    g_autoptr(GError) error = NULL;
    LmmeDocumentSaveResult result;

    doc->autosave_id = 0;
    result = lmme_document_save(doc, &error);
    if (result == LMME_DOCUMENT_SAVE_COMMITTED_DURABLE) {
        lmme_document_set_save_state(doc, LMME_SAVE_STATE_AUTOSAVED);
    } else {
        if (result == LMME_DOCUMENT_SAVE_NOT_COMMITTED) {
            lmme_document_set_save_state(doc, LMME_SAVE_STATE_ERROR);
        }
        lmme_window_set_status_error(doc->app,
                                     result == LMME_DOCUMENT_SAVE_COMMITTED_NOT_DURABLE
                                         ? "File was saved, but durability could not be confirmed."
                                         : "Could not save file.");
    }

    return G_SOURCE_REMOVE;
}

void
lmme_document_schedule_recovery(LmmeDocument *doc)
{
    if (doc == NULL || doc->app->scheduling_blocked) {
        return;
    }
    lmme_document_cancel_recovery(doc);
    doc->recovery_id = g_timeout_add(2000, recovery_timeout_cb, doc);
}

void
lmme_document_cancel_recovery(LmmeDocument *doc)
{
    if (doc != NULL && doc->recovery_id != 0) {
        g_source_remove(doc->recovery_id);
        doc->recovery_id = 0;
    }
}

void
lmme_document_schedule_autosave(LmmeDocument *doc)
{
    if (doc == NULL || !doc->app->config.autosave || doc->app->scheduling_blocked ||
        doc->disk_state != LMME_DISK_STATE_NORMAL) {
        return;
    }

    lmme_document_cancel_autosave(doc);
    doc->autosave_id = g_timeout_add(doc->app->config.autosave_delay_ms, autosave_timeout_cb, doc);
}

void
lmme_document_cancel_autosave(LmmeDocument *doc)
{
    if (doc != NULL && doc->autosave_id != 0) {
        g_source_remove(doc->autosave_id);
        doc->autosave_id = 0;
    }
}
