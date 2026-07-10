#include <glib.h>
#include <glib/gstdio.h>

#include "document/file_fingerprint.h"

static void
test_fingerprint_tracks_replace_and_delete(void)
{
    g_autofree char *dir = g_dir_make_tmp("lmme-test-fingerprint-XXXXXX", NULL);
    g_autofree char *path = g_build_filename(dir, "note.md", NULL);
    LmmeFileFingerprint first = {0};
    LmmeFileFingerprint second = {0};
    LmmeFileFingerprint missing = {0};

    g_assert_true(g_file_set_contents(path, "one", -1, NULL));
    g_assert_true(lmme_file_fingerprint_read(path, &first, NULL));
    g_assert_true(first.exists);
    g_assert_true(g_file_set_contents(path, "different length", -1, NULL));
    g_assert_true(lmme_file_fingerprint_read(path, &second, NULL));
    g_assert_false(lmme_file_fingerprint_equal(&first, &second));
    g_assert_cmpint(g_remove(path), ==, 0);
    g_assert_true(lmme_file_fingerprint_read(path, &missing, NULL));
    g_assert_false(missing.exists);
    g_assert_false(lmme_file_fingerprint_equal(&second, &missing));

    g_rmdir(dir);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/file-fingerprint/replace-delete", test_fingerprint_tracks_replace_and_delete);
    return g_test_run();
}
