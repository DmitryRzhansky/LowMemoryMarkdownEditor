#ifndef LMME_INFRA_SAFE_WRITE_TEST_H
#define LMME_INFRA_SAFE_WRITE_TEST_H

#include <glib.h>

typedef enum {
    LMME_SAFE_WRITE_TEST_FAIL_TEMP_CREATE,
    LMME_SAFE_WRITE_TEST_FAIL_FCHMOD,
    LMME_SAFE_WRITE_TEST_FAIL_WRITE,
    LMME_SAFE_WRITE_TEST_FAIL_FILE_FSYNC,
    LMME_SAFE_WRITE_TEST_FAIL_CLOSE,
    LMME_SAFE_WRITE_TEST_FAIL_RENAME,
    LMME_SAFE_WRITE_TEST_FAIL_DIRECTORY_OPEN,
    LMME_SAFE_WRITE_TEST_FAIL_DIRECTORY_FSYNC
} LmmeSafeWriteTestFault;

/* Private test seam. invocation is one-based and applies to the next writes. */
void lmme_safe_write_test_fail_at(LmmeSafeWriteTestFault fault, guint invocation);
void lmme_safe_write_test_reset(void);

#endif
