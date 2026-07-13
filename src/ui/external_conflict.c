#include "ui/external_conflict.h"

#include "app/app.h"
#include "document/document.h"
#include "document/document_autosave.h"
#include "document/file_monitor.h"
#include "document/tabs.h"
#include "infra/dialogs.h"
#include "infra/file_fingerprint.h"
#include "ui/window.h"
#include "workspace/workspace.h"

#include <string.h>

#define LMME_EXTERNAL_CONFLICT_ERROR (lmme_external_conflict_error_quark())

static GQuark
lmme_external_conflict_error_quark(void)
{
    return g_quark_from_static_string("lmme-external-conflict-error");
}

enum {
    LMME_EXTERNAL_CONFLICT_ERROR_CHANGED = 1,
    LMME_EXTERNAL_CONFLICT_ERROR_CANCELLED
};

typedef struct {
    LmmeApp *app;
    guint64 document_id;
} LmmeExternalConflictIdleData;

static char *
recovered_save_path(const LmmeDocument *doc)
{
    g_autofree char *directory = g_path_get_dirname(doc->path);
    g_autofree char *base = g_path_get_basename(doc->path);
    char *dot = strrchr(base, '.');
    g_autofree char *name = NULL;

    if (dot != NULL) {
        *dot = '\0';
    }
    name = g_strdup_printf("%s-recovered.md", base);
    return g_build_filename(directory, name, NULL);
}

static gboolean
conflict_disk_state(const LmmeDocument *doc)
{
    return doc != NULL &&
           (doc->disk_state == LMME_DISK_STATE_EXTERNAL_CHANGED ||
            doc->disk_state == LMME_DISK_STATE_EXTERNAL_DELETED);
}

static gboolean
validate_apply_context(const LmmeDocument *doc,
                       guint64 captured_generation,
                       const char *captured_document_path,
                       const char *captured_workspace_path,
                       LmmeFileFingerprint *fingerprint,
                       GError **error)
{
    if (doc == NULL) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Document is not available.");
        return FALSE;
    }
    if (doc->external_change_generation != captured_generation) {
        g_set_error_literal(error,
                            LMME_EXTERNAL_CONFLICT_ERROR,
                            LMME_EXTERNAL_CONFLICT_ERROR_CHANGED,
                            "External conflict changed.");
        return FALSE;
    }
    if (captured_document_path == NULL || g_strcmp0(doc->path, captured_document_path) != 0) {
        g_set_error_literal(error,
                            LMME_EXTERNAL_CONFLICT_ERROR,
                            LMME_EXTERNAL_CONFLICT_ERROR_CHANGED,
                            "Document path changed.");
        return FALSE;
    }
    if (doc->app->workspace == NULL || captured_workspace_path == NULL ||
        g_strcmp0(doc->app->workspace->path, captured_workspace_path) != 0) {
        g_set_error_literal(error,
                            LMME_EXTERNAL_CONFLICT_ERROR,
                            LMME_EXTERNAL_CONFLICT_ERROR_CHANGED,
                            "Workspace changed.");
        return FALSE;
    }
    if (!conflict_disk_state(doc)) {
        g_set_error_literal(error,
                            LMME_EXTERNAL_CONFLICT_ERROR,
                            LMME_EXTERNAL_CONFLICT_ERROR_CHANGED,
                            "Document is no longer in conflict.");
        return FALSE;
    }
    if (!lmme_file_fingerprint_read(doc->path, fingerprint, error)) {
        return FALSE;
    }
    if (doc->disk_state == LMME_DISK_STATE_EXTERNAL_DELETED && fingerprint->exists) {
        g_set_error_literal(error,
                            LMME_EXTERNAL_CONFLICT_ERROR,
                            LMME_EXTERNAL_CONFLICT_ERROR_CHANGED,
                            "External conflict changed.");
        return FALSE;
    }
    return TRUE;
}

gboolean
lmme_document_apply_external_conflict_choice(LmmeDocument *doc,
                                             LmmeExternalConflictChoice choice,
                                             guint64 captured_generation,
                                             const char *captured_document_path,
                                             const char *captured_workspace_path,
                                             GError **error)
{
    LmmeFileFingerprint fingerprint = {0};

    if (!validate_apply_context(doc,
                                captured_generation,
                                captured_document_path,
                                captured_workspace_path,
                                &fingerprint,
                                error)) {
        return FALSE;
    }

    if (choice == LMME_EXTERNAL_CONFLICT_KEEP_LOCAL) {
        return TRUE;
    }
    if (choice == LMME_EXTERNAL_CONFLICT_RELOAD) {
        if (!fingerprint.exists) {
            g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_NOENT, "File no longer exists.");
            return FALSE;
        }
        return lmme_document_reload_from_disk(doc, &fingerprint, error);
    }
    if (choice == LMME_EXTERNAL_CONFLICT_OVERWRITE) {
        LmmeDocumentSaveResult result = lmme_document_overwrite(doc, error);
        if (result == LMME_DOCUMENT_SAVE_NOT_COMMITTED) {
            return FALSE;
        }
        lmme_window_refresh_tree_directory(doc->app, doc->app->workspace->path);
        return TRUE;
    }

    g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Unknown external conflict choice.");
    return FALSE;
}

static gboolean
apply_choice_with_ui(LmmeDocument *doc,
                     LmmeExternalConflictChoice choice,
                     guint64 captured_generation,
                     const char *captured_document_path,
                     const char *captured_workspace_path)
{
    g_autoptr(GError) error = NULL;
    gboolean ok = FALSE;

    if (choice == LMME_EXTERNAL_CONFLICT_KEEP_LOCAL) {
        lmme_window_update_status(doc->app);
        return FALSE;
    }

    if (choice == LMME_EXTERNAL_CONFLICT_SAVE_AS) {
        g_autofree char *suggested = recovered_save_path(doc);
        g_autofree char *path = lmme_dialog_save_markdown(GTK_WINDOW(doc->app->window), suggested);
        LmmeDocumentSaveResult result;
        LmmeFileFingerprint fingerprint = {0};

        if (path == NULL) {
            return FALSE;
        }
        if (!validate_apply_context(doc,
                                    captured_generation,
                                    captured_document_path,
                                    captured_workspace_path,
                                    &fingerprint,
                                    &error)) {
            return FALSE;
        }
        if (g_file_test(path, G_FILE_TEST_EXISTS) &&
            !lmme_dialog_confirm_overwrite(GTK_WINDOW(doc->app->window), path)) {
            return FALSE;
        }
        result = lmme_document_save_as(doc, path, &error);
        if (result == LMME_DOCUMENT_SAVE_NOT_COMMITTED) {
            lmme_dialog_error(GTK_WINDOW(doc->app->window),
                              "Could not save file.",
                              error != NULL ? error->message : NULL);
            return FALSE;
        }
        if (result == LMME_DOCUMENT_SAVE_COMMITTED_NOT_DURABLE) {
            lmme_dialog_error(GTK_WINDOW(doc->app->window),
                              "File was saved, but durability could not be confirmed.",
                              error != NULL ? error->message : NULL);
        }
        lmme_window_refresh_tree_directory(doc->app, doc->app->workspace->path);
        return TRUE;
    }

    ok = lmme_document_apply_external_conflict_choice(doc,
                                                      choice,
                                                      captured_generation,
                                                      captured_document_path,
                                                      captured_workspace_path,
                                                      &error);
    if (!ok) {
        if (error != NULL &&
            !g_error_matches(error,
                             LMME_EXTERNAL_CONFLICT_ERROR,
                             LMME_EXTERNAL_CONFLICT_ERROR_CHANGED) &&
            !g_error_matches(error,
                             LMME_EXTERNAL_CONFLICT_ERROR,
                             LMME_EXTERNAL_CONFLICT_ERROR_CANCELLED)) {
            const char *title = "Could not resolve external conflict.";
            if (choice == LMME_EXTERNAL_CONFLICT_RELOAD) {
                title = "Could not reload file.";
            } else if (choice == LMME_EXTERNAL_CONFLICT_OVERWRITE) {
                title = "Could not overwrite file.";
            }
            lmme_dialog_error(GTK_WINDOW(doc->app->window), title, error->message);
        }
        return FALSE;
    }

    if (choice == LMME_EXTERNAL_CONFLICT_OVERWRITE && error != NULL) {
        lmme_dialog_error(GTK_WINDOW(doc->app->window),
                          "File was saved, but durability could not be confirmed.",
                          error->message);
    }
    return TRUE;
}

static void
finish_presenting(LmmeDocument *doc, gboolean had_pending)
{
    doc->external_conflict_state = LMME_EXTERNAL_CONFLICT_IDLE;
    if (had_pending && conflict_disk_state(doc)) {
        lmme_external_conflict_request(doc);
    }
}

static gboolean
external_conflict_idle_cb(gpointer user_data)
{
    LmmeExternalConflictIdleData *data = user_data;
    LmmeDocument *doc = NULL;
    LmmeExternalConflictChoice choice = LMME_EXTERNAL_CONFLICT_KEEP_LOCAL;
    gboolean file_exists = FALSE;
    gboolean recovery_ok = FALSE;
    gboolean had_pending = FALSE;
    guint64 captured_generation = 0;
    g_autofree char *captured_document_path = NULL;
    g_autofree char *captured_workspace_path = NULL;
    g_autoptr(GError) recovery_error = NULL;

    doc = lmme_tabs_find_by_id(data->app, data->document_id);
    if (doc == NULL || !conflict_disk_state(doc)) {
        return G_SOURCE_REMOVE;
    }

    doc->external_conflict_source_id = 0;
    doc->external_conflict_state = LMME_EXTERNAL_CONFLICT_PRESENTING;

    captured_generation = doc->external_change_generation;
    captured_document_path = g_strdup(doc->path);
    if (doc->app->workspace != NULL) {
        captured_workspace_path = g_strdup(doc->app->workspace->path);
    }

    file_exists = doc->disk_state != LMME_DISK_STATE_EXTERNAL_DELETED;
    recovery_ok = lmme_document_flush_recovery(doc, &recovery_error);
    if (!recovery_ok) {
        lmme_dialog_error(GTK_WINDOW(doc->app->window),
                          "Could not write recovery data.",
                          recovery_error != NULL ? recovery_error->message : NULL);
    }

    choice = lmme_dialog_external_conflict(
        GTK_WINDOW(doc->app->window),
        doc->path,
        file_exists,
        lmme_external_conflict_reload_allowed(file_exists, recovery_ok));

    had_pending = doc->external_change_pending;
    doc->external_change_pending = FALSE;
    (void)apply_choice_with_ui(doc,
                               choice,
                               captured_generation,
                               captured_document_path,
                               captured_workspace_path);
    finish_presenting(doc, had_pending);
    return G_SOURCE_REMOVE;
}

void
lmme_external_conflict_cancel(LmmeDocument *doc)
{
    if (doc == NULL) {
        return;
    }
    if (doc->external_conflict_source_id != 0) {
        g_source_remove(doc->external_conflict_source_id);
        doc->external_conflict_source_id = 0;
    }
    doc->external_conflict_state = LMME_EXTERNAL_CONFLICT_IDLE;
    doc->external_change_pending = FALSE;
}

void
lmme_external_conflict_request(LmmeDocument *doc)
{
    LmmeExternalConflictIdleData *data = NULL;

    if (doc == NULL || !conflict_disk_state(doc)) {
        return;
    }
    if (doc->external_conflict_state == LMME_EXTERNAL_CONFLICT_PRESENTING ||
        doc->external_conflict_state == LMME_EXTERNAL_CONFLICT_SCHEDULED) {
        doc->external_change_pending = TRUE;
        return;
    }

    data = g_new(LmmeExternalConflictIdleData, 1);
    data->app = doc->app;
    data->document_id = doc->id;
    doc->external_conflict_source_id =
        g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
                        external_conflict_idle_cb,
                        data,
                        (GDestroyNotify)g_free);
    doc->external_conflict_state = LMME_EXTERNAL_CONFLICT_SCHEDULED;
}
