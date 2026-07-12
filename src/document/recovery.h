#ifndef LMME_DOCUMENT_RECOVERY_H
#define LMME_DOCUMENT_RECOVERY_H

#include <glib.h>

#include "infra/file_fingerprint.h"

typedef struct _LmmeRecoveryStore LmmeRecoveryStore;

typedef struct {
    char *original_path;
    char *workspace_path;
    char *recovery_path;
    gint64 created_us;
    LmmeFileFingerprint original_fingerprint;
    gboolean legacy;
} LmmeRecoveryEntry;

LmmeRecoveryStore *lmme_recovery_store_new(const char *directory);
LmmeRecoveryStore *lmme_recovery_store_new_default(void);
void lmme_recovery_store_free(LmmeRecoveryStore *store);

const char *lmme_recovery_store_get_directory(const LmmeRecoveryStore *store);
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
gboolean lmme_recovery_remove(LmmeRecoveryStore *store,
                              const char *original_path,
                              GError **error);
GPtrArray *lmme_recovery_list(LmmeRecoveryStore *store, GError **error);
gboolean lmme_recovery_entry_original_changed(const LmmeRecoveryEntry *entry,
                                               GError **error);
gboolean lmme_recovery_entry_belongs_to_workspace(const LmmeRecoveryEntry *entry,
                                                   const char *workspace_path);
void lmme_recovery_entry_free(LmmeRecoveryEntry *entry);

#endif
