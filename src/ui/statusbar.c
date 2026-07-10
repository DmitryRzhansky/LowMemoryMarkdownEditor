#include "ui/statusbar.h"

#include "app/app.h"
#include "document/document.h"
#include "document/tabs.h"
#include "editor/editor.h"
#include "workspace/workspace.h"

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
lmme_statusbar_set_error(LmmeApp *app, const char *message)
{
    if (app == NULL || app->status_label == NULL) {
        return;
    }
    gtk_label_set_text(GTK_LABEL(app->status_label), message != NULL ? message : "Error");
}
