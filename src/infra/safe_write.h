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

/*
 * path and contents are borrowed for the call; contents may be NULL only when
 * length is zero. error may be NULL. The fingerprint describes the committed
 * replacement for either COMMITTED result. COMMITTED_NOT_DURABLE means the
 * target has changed even though directory durability could not be confirmed.
 */
LmmeSafeWriteOutcome lmme_safe_write_file(const char *path,
                                           const char *contents,
                                           gsize length,
                                           GError **error);

#endif
