#ifndef LMME_INFRA_SAFE_WRITE_H
#define LMME_INFRA_SAFE_WRITE_H

#include <glib.h>

#include "infra/file_fingerprint.h"

typedef enum {
    LMME_SAFE_WRITE_NOT_COMMITTED,
    LMME_SAFE_WRITE_COMMITTED_DURABLE,
    LMME_SAFE_WRITE_COMMITTED_NOT_DURABLE
} LmmeSafeWriteResult;

typedef struct {
    LmmeSafeWriteResult result;
    LmmeFileFingerprint fingerprint;
} LmmeSafeWriteOutcome;

LmmeSafeWriteOutcome lmme_safe_write_file(const char *path,
                                           const char *contents,
                                           gsize length,
                                           GError **error);

#endif
