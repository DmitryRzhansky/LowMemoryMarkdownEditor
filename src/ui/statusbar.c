#include "ui/statusbar.h"

#include "app/app.h"
#include "document/document.h"
#include "document/tabs.h"
#include "editor/editor.h"
#include "workspace/workspace.h"

char *
lmme_statusbar_format_document(const LmmeDocument *doc,
                               int line,
                               int column,
                               guint words,
                               gboolean words_valid,
                               gboolean preview_enabled)
{
    const char *recovery_status = "";

    if (doc != NULL && doc->recovery_failed) {
        recovery_status = " | Recovery failed";
    } else if (doc != NULL && doc->recovery_cleanup_failed) {
        recovery_status = " | Recovery cleanup failed";
    }

    if (doc == NULL) {
        return g_strdup("No file opened");
    }
    if (words_valid) {
        return g_strdup_printf("%s | Ln %d, Col %d | %u words | %s%s | %s",
                               doc->relative_path,
                               line,
                               column,
                               words,
                               lmme_document_save_state_label(doc),
                               recovery_status,
                               preview_enabled ? "Editable Preview" : "Source");
    }
    return g_strdup_printf("%s | Ln %d, Col %d | — words | %s%s | %s",
                           doc->relative_path,
                           line,
                           column,
                           lmme_document_save_state_label(doc),
                           recovery_status,
                           preview_enabled ? "Editable Preview" : "Source");
}

void
lmme_statusbar_update(LmmeApp *app)
{
    if (app == NULL || app->status_label == NULL) {
        return;
    }
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
    guint words = lmme_document_cached_word_count(doc);
    g_autofree char *status = lmme_statusbar_format_document(doc,
                                                             line,
                                                             column,
                                                             words,
                                                             lmme_document_word_count_is_valid(doc),
                                                             app->preview_enabled);
    gtk_label_set_text(GTK_LABEL(app->status_label), status);
}

void
lmme_statusbar_set_error(LmmeApp *app, const char *message)
{
    if (app == NULL || app->status_label == NULL) {
        return;
    }
    gtk_label_set_text(GTK_LABEL(app->status_label), message != NULL ? message : "Error");
}
