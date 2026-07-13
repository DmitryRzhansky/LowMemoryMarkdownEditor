#ifndef LMME_DOCUMENT_RECOVERY_TEST_H
#define LMME_DOCUMENT_RECOVERY_TEST_H

#include <glib.h>

typedef enum {
    LMME_RECOVERY_TEST_FAIL_INACTIVE_GENERATION_UNLINK,
    LMME_RECOVERY_TEST_FAIL_ACTIVE_PAYLOAD_UNLINK,
    LMME_RECOVERY_TEST_FAIL_METADATA_UNLINK,
    LMME_RECOVERY_TEST_FAIL_LEGACY_LOCATOR_UNLINK
} LmmeRecoveryTestFault;

/* Private test seam. invocation is one-based and applies to the next matching unlinks. */
void lmme_recovery_test_fail_at(LmmeRecoveryTestFault fault, guint invocation);
void lmme_recovery_test_reset(void);

#endif
