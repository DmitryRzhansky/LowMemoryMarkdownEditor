#ifndef LMME_DOCUMENT_RECOVERY_H
#define LMME_DOCUMENT_RECOVERY_H

#include <glib.h>

typedef struct {
    char *original_path;
    char *recovery_path;
} LmmeRecoveryEntry;

char *lmme_recovery_directory(void);
char *lmme_recovery_path_for_original(const char *original_path);
gboolean lmme_recovery_write(const char *original_path, const char *contents, gsize length, GError **error);
gboolean lmme_recovery_remove(const char *original_path, GError **error);
GPtrArray *lmme_recovery_list(GError **error);
void lmme_recovery_entry_free(LmmeRecoveryEntry *entry);

#endif
