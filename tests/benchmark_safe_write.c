#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include "infra/safe_write.h"

static void
run_case(const char *directory, gsize bytes)
{
    g_autofree char *path = g_build_filename(directory, "autosave.md", NULL);
    g_autofree char *contents = g_malloc(bytes + 1);
    g_autoptr(GError) error = NULL;
    gint64 started;
    gint64 elapsed;

    memset(contents, 'x', bytes);
    contents[bytes] = '\0';

    started = g_get_monotonic_time();
    LmmeSafeWriteOutcome outcome = lmme_safe_write_file(path, contents, bytes, &error);

    g_assert_cmpint(outcome.result, ==, LMME_SAFE_WRITE_COMMITTED_DURABLE);
    g_assert_no_error(error);
    elapsed = g_get_monotonic_time() - started;

    g_print("%" G_GSIZE_FORMAT " bytes: %.3f ms\n", bytes, (double)elapsed / 1000.0);
}

int
main(void)
{
    g_autofree char *directory = g_dir_make_tmp("lmme-safe-write-benchmark-XXXXXX", NULL);
    g_autofree char *path = NULL;

    g_assert_nonnull(directory);
    run_case(directory, 100U * 1024U);
    run_case(directory, 1024U * 1024U);
    run_case(directory, 2U * 1024U * 1024U);

    path = g_build_filename(directory, "autosave.md", NULL);
    g_remove(path);
    g_rmdir(directory);
    return 0;
}
