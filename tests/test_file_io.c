#define _GNU_SOURCE

#include <fcntl.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <unistd.h>

#include "document/file_io.h"

static void
test_file_read_utf8_valid_inputs(void)
{
    g_autofree char *directory = g_dir_make_tmp("lmme-file-io-XXXXXX", NULL);
    g_autofree char *path = g_build_filename(directory, "note.md", NULL);
    g_autofree char *contents = NULL;
    gsize length = 0;

    g_assert_true(g_file_set_contents(path, "", 0, NULL));
    g_assert_true(lmme_file_read_utf8(path, 0, &contents, &length, NULL));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmpstr(contents, ==, "");
    g_clear_pointer(&contents, g_free);

    g_assert_true(g_file_set_contents(path, "ASCII", 5, NULL));
    g_assert_true(lmme_file_read_utf8(path, 5, &contents, &length, NULL));
    g_assert_cmpuint(length, ==, 5);
    g_assert_cmpstr(contents, ==, "ASCII");
    g_clear_pointer(&contents, g_free);

    g_assert_true(g_file_set_contents(path, "Привет", -1, NULL));
    g_assert_true(lmme_file_read_utf8(path, 12, &contents, &length, NULL));
    g_assert_cmpuint(length, ==, 12);
    g_assert_cmpstr(contents, ==, "Привет");

    g_unlink(path);
    g_rmdir(directory);
}

static void
test_file_read_utf8_rejects_invalid_and_missing(void)
{
    static const char invalid_utf8[] = {(char)0xc3, (char)0x28};
    g_autofree char *directory = g_dir_make_tmp("lmme-file-io-XXXXXX", NULL);
    g_autofree char *path = g_build_filename(directory, "note.md", NULL);
    g_autofree char *contents = NULL;
    g_autoptr(GError) error = NULL;

    g_assert_true(g_file_set_contents(path, invalid_utf8, sizeof(invalid_utf8), NULL));
    g_assert_false(lmme_file_read_utf8(path, sizeof(invalid_utf8), &contents, NULL, &error));
    g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL);
    g_assert_null(contents);
    g_clear_error(&error);

    g_unlink(path);
    g_assert_false(lmme_file_read_utf8(path, 100, &contents, NULL, &error));
    g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_NOENT);
    g_assert_null(contents);
    g_rmdir(directory);
}

static void
test_file_read_utf8_enforces_size_boundary(void)
{
    g_autofree char *directory = g_dir_make_tmp("lmme-file-io-XXXXXX", NULL);
    g_autofree char *path = g_build_filename(directory, "note.md", NULL);
    g_autofree char *contents = NULL;
    g_autoptr(GError) error = NULL;
    int fd = -1;

    g_assert_true(g_file_set_contents(path, "12345", 5, NULL));
    g_assert_true(lmme_file_read_utf8(path, 5, &contents, NULL, NULL));
    g_clear_pointer(&contents, g_free);
    g_assert_false(lmme_file_read_utf8(path, 4, &contents, NULL, &error));
    g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED);
    g_clear_error(&error);

    fd = g_open(path, O_WRONLY | O_TRUNC | O_CLOEXEC, 0600);
    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(ftruncate(fd, 1024 * 1024), ==, 0);
    g_assert_cmpint(close(fd), ==, 0);
    g_assert_false(lmme_file_read_utf8(path, 1024, &contents, NULL, &error));
    g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED);

    g_unlink(path);
    g_rmdir(directory);
}

static void
test_file_read_utf8_permission_error(void)
{
    g_autofree char *directory = g_dir_make_tmp("lmme-file-io-XXXXXX", NULL);
    g_autofree char *path = g_build_filename(directory, "note.md", NULL);
    g_autofree char *contents = NULL;
    g_autoptr(GError) error = NULL;

    g_assert_true(g_file_set_contents(path, "private", 7, NULL));
    g_assert_cmpint(g_chmod(path, 0000), ==, 0);
    if (lmme_file_read_utf8(path, 7, &contents, NULL, &error)) {
        g_test_skip("Current process can read mode-000 files.");
        g_clear_pointer(&contents, g_free);
    } else {
        g_assert_nonnull(error);
    }
    g_assert_cmpint(g_chmod(path, 0600), ==, 0);
    g_unlink(path);
    g_rmdir(directory);
}

static void
test_file_read_utf8_document_open_limit(void)
{
    g_autofree char *directory = g_dir_make_tmp("lmme-file-io-limit-XXXXXX", NULL);
    g_autofree char *path = g_build_filename(directory, "note.md", NULL);
    g_autofree char *contents = NULL;
    g_autoptr(GError) error = NULL;

    g_assert_cmpuint(LMME_DOCUMENT_MAX_OPEN_BYTES, ==, 10U * 1024U * 1024U);
    g_assert_true(g_file_set_contents(path, "abc", 3, NULL));
    g_assert_true(lmme_file_read_utf8(path, LMME_DOCUMENT_MAX_OPEN_BYTES, &contents, NULL, NULL));
    g_clear_pointer(&contents, g_free);

    g_assert_false(lmme_file_read_utf8(path, 2, &contents, NULL, &error));
    g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED);

    g_unlink(path);
    g_rmdir(directory);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/file-io/valid", test_file_read_utf8_valid_inputs);
    g_test_add_func("/file-io/invalid-missing", test_file_read_utf8_rejects_invalid_and_missing);
    g_test_add_func("/file-io/size-boundary", test_file_read_utf8_enforces_size_boundary);
    g_test_add_func("/file-io/permission", test_file_read_utf8_permission_error);
    g_test_add_func("/file-io/document-open-limit", test_file_read_utf8_document_open_limit);
    return g_test_run();
}
