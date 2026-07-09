#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include "infra/safe_write.h"

static void
test_safe_write_file(void)
{
    g_autofree char *dir = g_dir_make_tmp("lmme-test-write-XXXXXX", NULL);
    g_autofree char *path = g_build_filename(dir, "note.md", NULL);
    g_autofree char *contents = NULL;

    g_assert_true(g_file_set_contents(path, "old", -1, NULL));
    g_assert_true(lmme_safe_write_file(path, "new content", strlen("new content"), NULL));
    g_assert_true(g_file_get_contents(path, &contents, NULL, NULL));
    g_assert_cmpstr(contents, ==, "new content");

    g_remove(path);
    g_rmdir(dir);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/safe-write/atomic-write", test_safe_write_file);
    return g_test_run();
}
