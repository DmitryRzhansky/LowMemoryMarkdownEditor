#include "document/document_paths.h"

#include "app/app.h"
#include "document/document.h"
#include "document/file_monitor.h"
#include "document/recovery.h"
#include "editor/editor.h"
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
try_remove_recovery(LmmeDocument *doc,
                    LmmeRecoveryStore *store,
                    const char *original_path,
                    const char *context)
{
    g_autoptr(GError) local_error = NULL;

    if (original_path == NULL) {
        return TRUE;
    }
    if (lmme_recovery_remove(store, original_path, &local_error)) {
        return TRUE;
    }
    note_recovery_cleanup_failure(doc, context, local_error);
    return FALSE;
}

typedef struct {
    LmmeDocument *document;
    char *old_path;
    char *new_path;
    char *new_relative_path;
    gboolean had_old_recovery;
    gboolean prepared_new_recovery;
} LmmeDocumentPathRemapEntry;

struct _LmmeDocumentPathRemapPlan {
    LmmeApp *app;
    GPtrArray *entries;
    gboolean committed;
};

static void
entry_free(LmmeDocumentPathRemapEntry *entry)
{
    if (entry == NULL) {
        return;
    }
    g_free(entry->old_path);
    g_free(entry->new_path);
    g_free(entry->new_relative_path);
    g_free(entry);
}

static char *
destination_path(const char *canonical_old,
                 const char *canonical_new,
                 const char *document_path)
{
    const char *suffix = document_path + strlen(canonical_old);

    while (*suffix == G_DIR_SEPARATOR) {
        suffix++;
    }
    return suffix[0] == '\0' ? g_strdup(canonical_new)
                              : g_build_filename(canonical_new, suffix, NULL);
}

static void
remove_prepared_recoveries(LmmeDocumentPathRemapPlan *plan)
{
    for (guint i = 0; i < plan->entries->len; i++) {
        LmmeDocumentPathRemapEntry *entry = g_ptr_array_index(plan->entries, i);
        if (entry->prepared_new_recovery) {
            try_remove_recovery(entry->document,
                                plan->app->recovery_store,
                                entry->new_path,
                                "Could not remove prepared recovery data");
            entry->prepared_new_recovery = FALSE;
        }
    }
}

static gboolean
prepare_recovery(LmmeDocumentPathRemapPlan *plan,
                 LmmeDocumentPathRemapEntry *entry,
                 GError **error)
{
    LmmeDocument *doc = entry->document;
    g_autofree char *snapshot = NULL;
    LmmeRecoveryStage *stage = NULL;
    const char *workspace_path = plan->app->workspace != NULL
                                     ? plan->app->workspace->path
                                     : NULL;

    if (!doc->modified && !doc->restored_from_recovery) {
        return TRUE;
    }
    snapshot = lmme_editor_dup_text(GTK_TEXT_BUFFER(doc->buffer));
    stage = lmme_recovery_stage_new(plan->app->recovery_store,
                                    entry->new_path,
                                    workspace_path,
                                    &doc->base_fingerprint,
                                    snapshot,
                                    strlen(snapshot),
                                    error);
    if (stage == NULL) {
        return FALSE;
    }
    if (!lmme_recovery_stage_commit(stage, error)) {
        lmme_recovery_stage_free(stage);
        try_remove_recovery(doc,
                            plan->app->recovery_store,
                            entry->new_path,
                            "Could not remove recovery data after a failed remap commit");
        return FALSE;
    }
    lmme_recovery_stage_free(stage);
    entry->prepared_new_recovery = TRUE;
    return TRUE;
}

LmmeDocumentPathRemapPlan *
lmme_document_path_remap_plan_new(LmmeApp *app,
                                  const char *old_root,
                                  const char *new_root,
                                  GError **error)
{
    LmmeDocumentPathRemapPlan *plan = NULL;
    g_autofree char *canonical_old = NULL;
    g_autofree char *canonical_new = NULL;
    g_autoptr(GHashTable) affected_documents = NULL;
    g_autoptr(GHashTable) destinations = NULL;

    if (app == NULL || app->documents == NULL || old_root == NULL || new_root == NULL) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Invalid document path remap.");
        return NULL;
    }

    canonical_old = g_canonicalize_filename(old_root, NULL);
    canonical_new = g_canonicalize_filename(new_root, NULL);
    plan = g_new0(LmmeDocumentPathRemapPlan, 1);
    plan->app = app;
    plan->entries = g_ptr_array_new_with_free_func((GDestroyNotify)entry_free);
    affected_documents = g_hash_table_new(g_direct_hash, g_direct_equal);
    destinations = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    for (guint i = 0; i < app->documents->len; i++) {
        LmmeDocument *doc = g_ptr_array_index(app->documents, i);
        LmmeDocumentPathRemapEntry *entry = NULL;

        if (!lmme_path_is_inside(canonical_old, doc->path)) {
            continue;
        }
        entry = g_new0(LmmeDocumentPathRemapEntry, 1);
        entry->document = doc;
        entry->old_path = g_strdup(doc->path);
        entry->new_path = destination_path(canonical_old, canonical_new, doc->path);
        entry->new_relative_path = app->workspace != NULL
                                       ? lmme_path_relative_to(app->workspace->path, entry->new_path)
                                       : g_path_get_basename(entry->new_path);
        entry->had_old_recovery = lmme_recovery_exists(app->recovery_store, doc->path);

        if (g_hash_table_contains(destinations, entry->new_path)) {
            g_set_error_literal(error,
                                G_FILE_ERROR,
                                G_FILE_ERROR_EXIST,
                                "Multiple open documents map to the same rename destination.");
            entry_free(entry);
            lmme_document_path_remap_plan_free(plan);
            return NULL;
        }
        g_hash_table_add(destinations, g_strdup(entry->new_path));
        g_hash_table_add(affected_documents, doc);
        g_ptr_array_add(plan->entries, entry);
    }

    for (guint i = 0; i < app->documents->len; i++) {
        LmmeDocument *doc = g_ptr_array_index(app->documents, i);
        if (!g_hash_table_contains(affected_documents, doc) &&
            g_hash_table_contains(destinations, doc->path)) {
            g_set_error_literal(error,
                                G_FILE_ERROR,
                                G_FILE_ERROR_EXIST,
                                "A rename destination is already open.");
            lmme_document_path_remap_plan_free(plan);
            return NULL;
        }
    }

    for (guint i = 0; i < plan->entries->len; i++) {
        LmmeDocumentPathRemapEntry *entry = g_ptr_array_index(plan->entries, i);
        if (lmme_recovery_exists(app->recovery_store, entry->new_path)) {
            g_set_error_literal(error,
                                G_FILE_ERROR,
                                G_FILE_ERROR_EXIST,
                                "Recovery data already exists for the rename destination.");
            lmme_document_path_remap_plan_free(plan);
            return NULL;
        }
    }

    for (guint i = 0; i < plan->entries->len; i++) {
        LmmeDocumentPathRemapEntry *entry = g_ptr_array_index(plan->entries, i);
        if (!prepare_recovery(plan, entry, error)) {
            remove_prepared_recoveries(plan);
            lmme_document_path_remap_plan_free(plan);
            return NULL;
        }
    }
    return plan;
}

void
lmme_document_path_remap_plan_commit(LmmeDocumentPathRemapPlan *plan)
{
    if (plan == NULL || plan->committed) {
        return;
    }

    for (guint i = 0; i < plan->entries->len; i++) {
        LmmeDocumentPathRemapEntry *entry = g_ptr_array_index(plan->entries, i);
        LmmeDocument *doc = entry->document;

        lmme_document_file_monitor_detach(doc);
        g_free(doc->path);
        doc->path = g_steal_pointer(&entry->new_path);
        g_free(doc->relative_path);
        doc->relative_path = g_steal_pointer(&entry->new_relative_path);

        if (entry->prepared_new_recovery) {
            if (try_remove_recovery(doc,
                                    plan->app->recovery_store,
                                    entry->old_path,
                                    "Could not remove recovery data for the previous path after remap")) {
                g_free(doc->recovery_source_path);
                doc->recovery_source_path = lmme_recovery_path_for_original(plan->app->recovery_store,
                                                                            doc->path);
            }
        } else if (entry->had_old_recovery) {
            if (try_remove_recovery(doc,
                                    plan->app->recovery_store,
                                    entry->old_path,
                                    "Could not remove recovery data for the previous path after remap")) {
                g_clear_pointer(&doc->recovery_source_path, g_free);
            }
        }
        lmme_document_file_monitor_attach(doc);
        lmme_document_refresh_title(doc);
    }
    plan->committed = TRUE;
}

void
lmme_document_path_remap_plan_free(LmmeDocumentPathRemapPlan *plan)
{
    if (plan == NULL) {
        return;
    }
    if (!plan->committed) {
        remove_prepared_recoveries(plan);
    }
    g_ptr_array_unref(plan->entries);
    g_free(plan);
}
