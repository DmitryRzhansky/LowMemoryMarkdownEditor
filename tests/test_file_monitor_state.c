#include <glib.h>

#include "document/file_monitor.h"

static LmmeFileFingerprint
fingerprint(guint64 inode, guint64 size)
{
    LmmeFileFingerprint value = {
        .exists = TRUE,
        .size = size,
        .mtime_ns = (gint64)inode,
        .inode = inode,
        .device = 1,
    };
    return value;
}

static void
test_file_change_decisions(void)
{
    LmmeFileFingerprint old = fingerprint(1, 10);
    LmmeFileFingerprint current = fingerprint(2, 20);
    LmmeFileFingerprint missing = {0};

    g_assert_cmpint(lmme_file_change_decide(&old,
                                             &old,
                                             NULL,
                                             FALSE,
                                             FALSE,
                                             FALSE,
                                             LMME_DISK_STATE_NORMAL),
                    ==,
                    LMME_FILE_CHANGE_IGNORE);
    g_assert_cmpint(lmme_file_change_decide(&current,
                                             &old,
                                             &current,
                                             TRUE,
                                             TRUE,
                                             FALSE,
                                             LMME_DISK_STATE_NORMAL),
                    ==,
                    LMME_FILE_CHANGE_INTERNAL_WRITE);
    g_assert_cmpint(lmme_file_change_decide(&current,
                                             &old,
                                             NULL,
                                             FALSE,
                                             FALSE,
                                             FALSE,
                                             LMME_DISK_STATE_NORMAL),
                    ==,
                    LMME_FILE_CHANGE_RELOAD);
    g_assert_cmpint(lmme_file_change_decide(&current,
                                             &old,
                                             NULL,
                                             FALSE,
                                             TRUE,
                                             FALSE,
                                             LMME_DISK_STATE_NORMAL),
                    ==,
                    LMME_FILE_CHANGE_EXTERNAL_CHANGED);
    g_assert_cmpint(lmme_file_change_decide(&current,
                                             &old,
                                             NULL,
                                             FALSE,
                                             FALSE,
                                             TRUE,
                                             LMME_DISK_STATE_NORMAL),
                    ==,
                    LMME_FILE_CHANGE_EXTERNAL_CHANGED);
    g_assert_cmpint(lmme_file_change_decide(&missing,
                                             &old,
                                             NULL,
                                             FALSE,
                                             FALSE,
                                             FALSE,
                                             LMME_DISK_STATE_NORMAL),
                    ==,
                    LMME_FILE_CHANGE_EXTERNAL_DELETED);
}

static void
test_internal_fingerprint_is_consumed_once(void)
{
    LmmeFileFingerprint old = fingerprint(1, 10);
    LmmeFileFingerprint saved = fingerprint(2, 20);

    g_assert_cmpint(lmme_file_change_decide(&saved,
                                             &old,
                                             &saved,
                                             TRUE,
                                             TRUE,
                                             FALSE,
                                             LMME_DISK_STATE_NORMAL),
                    ==,
                    LMME_FILE_CHANGE_INTERNAL_WRITE);
    g_assert_cmpint(lmme_file_change_decide(&saved,
                                             &saved,
                                             &saved,
                                             FALSE,
                                             TRUE,
                                             FALSE,
                                             LMME_DISK_STATE_NORMAL),
                    ==,
                    LMME_FILE_CHANGE_IGNORE);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/file-monitor/decisions", test_file_change_decisions);
    g_test_add_func("/file-monitor/internal-once", test_internal_fingerprint_is_consumed_once);
    return g_test_run();
}
