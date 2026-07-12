#ifndef LMME_DOCUMENT_RECOVERY_H
#define LMME_DOCUMENT_RECOVERY_H

#include <glib.h>

#include "infra/file_fingerprint.h"

typedef struct _LmmeRecoveryStore LmmeRecoveryStore;
typedef struct _LmmeRecoveryStage LmmeRecoveryStage;

typedef struct {
    char *original_path;
    char *workspace_path;
    char *recovery_path;
    gint64 created_us;
    LmmeFileFingerprint original_fingerprint;
    gboolean legacy;
} LmmeRecoveryEntry;

/* Constructors and stage_new return owned objects; NULL indicates failure. */
LmmeRecoveryStore *lmme_recovery_store_new(const char *directory);
LmmeRecoveryStore *lmme_recovery_store_new_default(void);
void lmme_recovery_store_free(LmmeRecoveryStore *store);

/* Borrowed from store and valid until the store is freed. */
const char *lmme_recovery_store_get_directory(const LmmeRecoveryStore *store);
/* Returns an owned active-generation or legacy-compatible path; NULL on invalid input. */
char *lmme_recovery_path_for_original(const LmmeRecoveryStore *store,
                                      const char *original_path);
gboolean lmme_recovery_exists(const LmmeRecoveryStore *store,
                              const char *original_path);
gboolean lmme_recovery_write(LmmeRecoveryStore *store,
                             const char *original_path,
                             const char *workspace_path,
                             const LmmeFileFingerprint *original_fingerprint,
                             const char *contents,
                             gsize length,
                             GError **error);
LmmeRecoveryStage *lmme_recovery_stage_new(LmmeRecoveryStore *store,
                                           const char *original_path,
                                           const char *workspace_path,
                                           const LmmeFileFingerprint *original_fingerprint,
                                           const char *contents,
                                           gsize length,
                                           GError **error);
/* Commit does not consume stage; every returned stage must still be freed. */
gboolean lmme_recovery_stage_commit(LmmeRecoveryStage *stage, GError **error);
void lmme_recovery_stage_free(LmmeRecoveryStage *stage);
gboolean lmme_recovery_remove(LmmeRecoveryStore *store,
                              const char *original_path,
                              GError **error);
/*
 * Returns an owned GPtrArray with owned LmmeRecoveryEntry elements, or NULL on
 * error. Unref the array to release the entries.
 */
GPtrArray *lmme_recovery_list(LmmeRecoveryStore *store, GError **error);
gboolean lmme_recovery_entry_original_changed(const LmmeRecoveryEntry *entry,
                                               GError **error);
gboolean lmme_recovery_entry_belongs_to_workspace(const LmmeRecoveryEntry *entry,
                                                   const char *workspace_path);
void lmme_recovery_entry_free(LmmeRecoveryEntry *entry);

#endif
