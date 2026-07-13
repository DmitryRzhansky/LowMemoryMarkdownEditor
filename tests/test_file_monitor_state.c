#include <glib.h>

#include "app/app.h"
#include "document/file_monitor.h"
#include "ui/external_conflict.h"

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

static void
test_external_conflict_reload_requires_current_recovery(void)
{
    g_assert_true(lmme_external_conflict_reload_allowed(TRUE, TRUE));
    g_assert_false(lmme_external_conflict_reload_allowed(TRUE, FALSE));
}

static void
test_external_delete_never_allows_reload(void)
{
    g_assert_false(lmme_external_conflict_reload_allowed(FALSE, TRUE));
    g_assert_false(lmme_external_conflict_reload_allowed(FALSE, FALSE));
}

static void
test_external_conflict_request_state_machine(void)
{
    LmmeApp app = {0};
    LmmeDocument doc = {0};

    doc.app = &app;
    doc.id = 7;
    doc.disk_state = LMME_DISK_STATE_EXTERNAL_CHANGED;

    lmme_external_conflict_request(&doc);
    g_assert_cmpint(doc.external_conflict_state, ==, LMME_EXTERNAL_CONFLICT_SCHEDULED);
    g_assert_cmpuint(doc.external_conflict_source_id, !=, 0);

    lmme_external_conflict_request(&doc);
    g_assert_true(doc.external_change_pending);
    g_assert_cmpuint(doc.external_conflict_source_id, !=, 0);

    lmme_external_conflict_cancel(&doc);
    g_assert_cmpint(doc.external_conflict_state, ==, LMME_EXTERNAL_CONFLICT_IDLE);
    g_assert_cmpuint(doc.external_conflict_source_id, ==, 0);
    g_assert_false(doc.external_change_pending);
}

static void
test_external_conflict_presenting_sets_pending(void)
{
    LmmeApp app = {0};
    LmmeDocument doc = {0};

    doc.app = &app;
    doc.disk_state = LMME_DISK_STATE_EXTERNAL_CHANGED;
    doc.external_conflict_state = LMME_EXTERNAL_CONFLICT_PRESENTING;

    lmme_external_conflict_request(&doc);
    g_assert_true(doc.external_change_pending);
    g_assert_cmpint(doc.external_conflict_state, ==, LMME_EXTERNAL_CONFLICT_PRESENTING);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/file-monitor/decisions", test_file_change_decisions);
    g_test_add_func("/file-monitor/internal-once", test_internal_fingerprint_is_consumed_once);
    g_test_add_func("/file-monitor/conflict/reload-policy",
                    test_external_conflict_reload_requires_current_recovery);
    g_test_add_func("/file-monitor/deleted/reload-policy",
                    test_external_delete_never_allows_reload);
    g_test_add_func("/file-monitor/conflict/request-state",
                    test_external_conflict_request_state_machine);
    g_test_add_func("/file-monitor/conflict/presenting-pending",
                    test_external_conflict_presenting_sets_pending);
    return g_test_run();
}
