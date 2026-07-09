#include "document/file_monitor.h"

#include "app/app.h"
#include "document/document.h"
#include "infra/dialogs.h"

static void
on_file_monitor_changed(GFileMonitor *monitor,
                        GFile *file,
                        GFile *other_file,
                        GFileMonitorEvent event_type,
                        gpointer user_data)
{
    LmmeDocument *doc = user_data;
    g_autofree char *contents = NULL;
    gsize length = 0;
    g_autoptr(GError) error = NULL;

    (void)monitor;
    (void)file;
    (void)other_file;

    if (event_type != G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT &&
        event_type != G_FILE_MONITOR_EVENT_CREATED &&
        event_type != G_FILE_MONITOR_EVENT_DELETED) {
        return;
    }

    if (g_get_monotonic_time() - doc->last_internal_save_us < 2000000) {
        return;
    }

    if (doc->modified) {
        lmme_dialog_error(GTK_WINDOW(doc->app->window),
                          "File changed outside the editor.",
                          "Local changes were kept. Save manually to overwrite the file.");
        return;
    }

    if (g_file_get_contents(doc->path, &contents, &length, &error) &&
        g_utf8_validate(contents, (gssize)length, NULL)) {
        g_signal_handler_block(doc->buffer, doc->changed_handler_id);
        gtk_text_buffer_set_text(GTK_TEXT_BUFFER(doc->buffer), contents, (int)length);
        g_signal_handler_unblock(doc->buffer, doc->changed_handler_id);
        lmme_document_set_save_state(doc, LMME_SAVE_STATE_SAVED);
        if (doc->app->preview_enabled) {
            (void)lmme_document_set_preview_visible(doc, TRUE);
        }
    }
}

void
lmme_document_file_monitor_attach(LmmeDocument *doc)
{
    g_autoptr(GFile) file = NULL;
    g_autoptr(GError) error = NULL;

    if (doc == NULL || doc->path == NULL) {
        return;
    }

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
