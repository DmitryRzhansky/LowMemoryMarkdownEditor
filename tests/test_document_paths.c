#include <glib.h>
#include <glib/gstdio.h>

#include "app/app.h"
#include "document/file_monitor.h"
#include "document/recovery.h"
#include "document/tabs.h"
#include "workspace/workspace.h"

static void
test_directory_rename_remaps_descendants(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-paths-XXXXXX", NULL);
    g_autofree char *cache = g_build_filename(root, "cache", NULL);
    g_autofree char *old_dir = g_build_filename(root, "notes", NULL);
    g_autofree char *new_dir = NULL;
    g_autofree char *old_first = g_build_filename(old_dir, "first.md", NULL);
    g_autofree char *nested = g_build_filename(old_dir, "nested", NULL);
    g_autofree char *old_second = g_build_filename(nested, "second.md", NULL);
    g_autofree char *expected_first = NULL;
    g_autofree char *expected_second = NULL;
    LmmeApp app = {0};
    LmmeDocument first = {0};
    LmmeDocument second = {0};

    g_assert_cmpint(g_mkdir_with_parents(nested, 0700), ==, 0);
    g_assert_true(g_file_set_contents(old_first, "first", -1, NULL));
    g_assert_true(g_file_set_contents(old_second, "second", -1, NULL));
    app.workspace = lmme_workspace_new(root);
    app.recovery_store = lmme_recovery_store_new(cache);
    app.documents = g_ptr_array_new();
    first.app = &app;
    first.path = g_strdup(old_first);
    second.app = &app;
    second.path = g_strdup(old_second);
    g_ptr_array_add(app.documents, &first);
    g_ptr_array_add(app.documents, &second);

    g_assert_true(lmme_workspace_rename_path(app.workspace, old_dir, "archive", &new_dir, NULL));
    g_assert_true(lmme_tabs_remap_subtree(&app, old_dir, new_dir, NULL));
    expected_first = g_build_filename(new_dir, "first.md", NULL);
    expected_second = g_build_filename(new_dir, "nested", "second.md", NULL);
    g_assert_cmpstr(first.path, ==, expected_first);
    g_assert_cmpstr(second.path, ==, expected_second);

    lmme_document_file_monitor_detach(&first);
    lmme_document_file_monitor_detach(&second);
    g_ptr_array_unref(app.documents);
    g_free(first.path);
    g_free(first.relative_path);
    g_free(second.path);
    g_free(second.relative_path);
    lmme_recovery_store_free(app.recovery_store);
    lmme_workspace_delete_path(app.workspace, new_dir, NULL);
    lmme_workspace_free(app.workspace);
    g_rmdir(cache);
    g_rmdir(root);
}

static void
test_recovery_collision_blocks_rename_preflight(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-paths-XXXXXX", NULL);
    g_autofree char *cache = g_build_filename(root, "cache", NULL);
    g_autofree char *old_dir = g_build_filename(root, "notes", NULL);
    g_autofree char *old_file = g_build_filename(old_dir, "note.md", NULL);
    g_autofree char *new_dir = g_build_filename(root, "archive", NULL);
    g_autofree char *new_file = g_build_filename(new_dir, "note.md", NULL);
    g_autoptr(GError) error = NULL;
    LmmeFileFingerprint missing = {0};
    LmmeApp app = {0};
    LmmeDocument doc = {0};

    g_assert_cmpint(g_mkdir(old_dir, 0700), ==, 0);
    g_assert_true(g_file_set_contents(old_file, "text", -1, NULL));
    app.workspace = lmme_workspace_new(root);
    app.recovery_store = lmme_recovery_store_new(cache);
    app.documents = g_ptr_array_new();
    doc.app = &app;
    doc.path = g_strdup(old_file);
    g_ptr_array_add(app.documents, &doc);
    g_assert_true(lmme_recovery_write(app.recovery_store,
                                      new_file,
                                      root,
                                      &missing,
                                      "stale",
                                      5,
                                      NULL));

    g_assert_false(lmme_tabs_validate_subtree_remap(&app, old_dir, new_dir, &error));
    g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_EXIST);
    g_assert_true(g_file_test(old_file, G_FILE_TEST_IS_REGULAR));
    g_assert_false(g_file_test(new_dir, G_FILE_TEST_EXISTS));

    lmme_recovery_remove(app.recovery_store, new_file, NULL);
    g_ptr_array_unref(app.documents);
    g_free(doc.path);
    lmme_recovery_store_free(app.recovery_store);
    lmme_workspace_free(app.workspace);
    g_remove(old_file);
    g_rmdir(old_dir);
    g_rmdir(cache);
    g_rmdir(root);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/document-paths/rename-descendants", test_directory_rename_remaps_descendants);
    g_test_add_func("/document-paths/recovery-collision", test_recovery_collision_blocks_rename_preflight);
    return g_test_run();
}
