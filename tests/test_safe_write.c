#define _GNU_SOURCE

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

static void
test_preserves_permissions(void)
{
    g_autofree char *dir = g_dir_make_tmp("lmme-test-write-XXXXXX", NULL);
    g_autofree char *path = g_build_filename(dir, "note.md", NULL);
    struct stat info;

    g_assert_true(g_file_set_contents(path, "old", -1, NULL));
    g_assert_cmpint(g_chmod(path, 0640), ==, 0);
    g_assert_true(lmme_safe_write_file(path, "new", 3, NULL));
    g_assert_cmpint(g_stat(path, &info), ==, 0);
    g_assert_cmpuint(info.st_mode & 0777, ==, 0640);

    g_remove(path);
    g_rmdir(dir);
}

static void
test_does_not_leave_temp_file(void)
{
    g_autofree char *dir = g_dir_make_tmp("lmme-test-write-XXXXXX", NULL);
    g_autofree char *path = g_build_filename(dir, "note.md", NULL);
    g_autoptr(GDir) opened = NULL;
    const char *name = NULL;

    g_assert_true(lmme_safe_write_file(path, "content", 7, NULL));
    opened = g_dir_open(dir, 0, NULL);
    g_assert_nonnull(opened);
    while ((name = g_dir_read_name(opened)) != NULL) {
        g_assert_false(g_str_has_prefix(name, ".note.md.lmme."));
    }

    g_remove(path);
    g_rmdir(dir);
}

static void
test_follows_existing_symlink_without_replacing_it(void)
{
    g_autofree char *dir = g_dir_make_tmp("lmme-test-write-XXXXXX", NULL);
    g_autofree char *target = g_build_filename(dir, "target.md", NULL);
    g_autofree char *link = g_build_filename(dir, "link.md", NULL);
    g_autofree char *contents = NULL;
    struct stat info;

    g_assert_true(g_file_set_contents(target, "old", -1, NULL));
    g_assert_cmpint(symlink(target, link), ==, 0);
    g_assert_true(lmme_safe_write_file(link, "new", 3, NULL));
    g_assert_cmpint(g_lstat(link, &info), ==, 0);
    g_assert_true(S_ISLNK(info.st_mode));
    g_assert_true(g_file_get_contents(target, &contents, NULL, NULL));
    g_assert_cmpstr(contents, ==, "new");

    g_remove(link);
    g_remove(target);
    g_rmdir(dir);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/safe-write/atomic-write", test_safe_write_file);
    g_test_add_func("/safe-write/preserves-permissions", test_preserves_permissions);
    g_test_add_func("/safe-write/no-temp-file", test_does_not_leave_temp_file);
    g_test_add_func("/safe-write/symlink", test_follows_existing_symlink_without_replacing_it);
    return g_test_run();
}
