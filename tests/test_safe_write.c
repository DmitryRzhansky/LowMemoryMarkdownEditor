#define _GNU_SOURCE

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "infra/safe_write.h"
#include "infra/safe_write_test.h"

static guint
count_open_file_descriptors_for_path(const char *path)
{
    g_autoptr(GDir) directory = g_dir_open("/proc/self/fd", 0, NULL);
    const char *name = NULL;
    guint count = 0;

    if (directory == NULL) {
        return 0;
    }
    while ((name = g_dir_read_name(directory)) != NULL) {
        g_autofree char *descriptor_path = g_build_filename("/proc/self/fd", name, NULL);
        g_autofree char *target = g_file_read_link(descriptor_path, NULL);

        if (target != NULL && g_str_has_prefix(target, path)) {
            count++;
        }
    }
    return count;
}

static guint
count_save_tempfiles(const char *directory_path)
{
    g_autoptr(GDir) directory = g_dir_open(directory_path, 0, NULL);
    const char *name = NULL;
    guint count = 0;

    while (directory != NULL && (name = g_dir_read_name(directory)) != NULL) {
        if (g_str_has_prefix(name, ".note.md.lmme.")) {
            count++;
        }
    }
    return count;
}

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

static void
test_safe_write_fault_matrix(void)
{
    static const LmmeSafeWriteTestFault faults[] = {
        LMME_SAFE_WRITE_TEST_FAIL_TEMP_CREATE,
        LMME_SAFE_WRITE_TEST_FAIL_FCHMOD,
        LMME_SAFE_WRITE_TEST_FAIL_WRITE,
        LMME_SAFE_WRITE_TEST_FAIL_FILE_FSYNC,
        LMME_SAFE_WRITE_TEST_FAIL_CLOSE,
        LMME_SAFE_WRITE_TEST_FAIL_RENAME,
        LMME_SAFE_WRITE_TEST_FAIL_DIRECTORY_OPEN,
        LMME_SAFE_WRITE_TEST_FAIL_DIRECTORY_FSYNC,
    };

    for (guint i = 0; i < G_N_ELEMENTS(faults); i++) {
        g_autofree char *directory = g_dir_make_tmp("lmme-safe-write-fault-XXXXXX", NULL);
        g_autofree char *path = g_build_filename(directory, "note.md", NULL);
        g_autofree char *contents = NULL;
        g_autoptr(GError) error = NULL;
        gboolean post_commit = faults[i] == LMME_SAFE_WRITE_TEST_FAIL_DIRECTORY_OPEN ||
                               faults[i] == LMME_SAFE_WRITE_TEST_FAIL_DIRECTORY_FSYNC;

        g_assert_nonnull(directory);
        g_assert_true(g_file_set_contents(path, "old", 3, NULL));
        lmme_safe_write_test_fail_at(faults[i], 1);
        g_assert_false(lmme_safe_write_file(path, "new", 3, &error));
        lmme_safe_write_test_reset();

        g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_IO);
        g_assert_true(g_file_get_contents(path, &contents, NULL, NULL));
        g_assert_cmpstr(contents, ==, post_commit ? "new" : "old");
        g_assert_cmpuint(count_save_tempfiles(directory), ==, 0);

        g_assert_cmpuint(count_open_file_descriptors_for_path(directory), ==, 0);

        g_assert_cmpint(g_unlink(path), ==, 0);
        g_assert_cmpint(g_rmdir(directory), ==, 0);
    }
}

static void
test_safe_write_fault_targets_invocation(void)
{
    g_autofree char *directory = g_dir_make_tmp("lmme-safe-write-call-XXXXXX", NULL);
    g_autofree char *first = g_build_filename(directory, "first.md", NULL);
    g_autofree char *second = g_build_filename(directory, "second.md", NULL);
    g_autoptr(GError) error = NULL;

    lmme_safe_write_test_fail_at(LMME_SAFE_WRITE_TEST_FAIL_WRITE, 2);
    g_assert_true(lmme_safe_write_file(first, "first", 5, NULL));
    g_assert_false(lmme_safe_write_file(second, "second", 6, &error));
    lmme_safe_write_test_reset();

    g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_IO);
    g_assert_true(g_file_test(first, G_FILE_TEST_IS_REGULAR));
    g_assert_false(g_file_test(second, G_FILE_TEST_EXISTS));

    g_assert_cmpint(g_unlink(first), ==, 0);
    g_assert_cmpint(g_rmdir(directory), ==, 0);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/safe-write/atomic-write", test_safe_write_file);
    g_test_add_func("/safe-write/preserves-permissions", test_preserves_permissions);
    g_test_add_func("/safe-write/no-temp-file", test_does_not_leave_temp_file);
    g_test_add_func("/safe-write/symlink", test_follows_existing_symlink_without_replacing_it);
    g_test_add_func("/safe-write/fault-matrix", test_safe_write_fault_matrix);
    g_test_add_func("/safe-write/fault-invocation", test_safe_write_fault_targets_invocation);
    return g_test_run();
}
