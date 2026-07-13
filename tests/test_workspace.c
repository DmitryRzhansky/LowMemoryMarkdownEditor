#define _GNU_SOURCE

#include <glib.h>
#include <glib/gstdio.h>
#include <unistd.h>

#include "workspace/workspace.h"
#include "workspace/workspace_delete_test.h"

static void
test_recursive_delete_and_root_guard(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-workspace-XXXXXX", NULL);
    g_autofree char *folder = g_build_filename(root, "notes", NULL);
    g_autofree char *nested = g_build_filename(folder, "nested", NULL);
    g_autofree char *file = g_build_filename(nested, "note.md", NULL);
    g_autoptr(GError) error = NULL;
    LmmeWorkspace *workspace = lmme_workspace_new(root);
    LmmeWorkspaceDeleteOutcome outcome = {0};

    g_assert_cmpint(g_mkdir_with_parents(nested, 0700), ==, 0);
    g_assert_true(g_file_set_contents(file, "text", -1, NULL));
    outcome = lmme_workspace_delete_path(workspace, folder, NULL);
    g_assert_cmpint(outcome.result, ==, LMME_WORKSPACE_DELETE_COMPLETE);
    g_assert_false(g_file_test(folder, G_FILE_TEST_EXISTS));
    outcome = lmme_workspace_delete_path(workspace, root, &error);
    g_assert_cmpint(outcome.result, ==, LMME_WORKSPACE_DELETE_UNCHANGED);
    g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_PERM);

    lmme_workspace_free(workspace);
    g_rmdir(root);
}

static void
test_symlink_delete_does_not_follow_target(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-workspace-XXXXXX", NULL);
    g_autofree char *outside = g_dir_make_tmp("lmme-test-outside-XXXXXX", NULL);
    g_autofree char *outside_file = g_build_filename(outside, "keep.md", NULL);
    g_autofree char *link = g_build_filename(root, "linked", NULL);
    LmmeWorkspace *workspace = lmme_workspace_new(root);

    g_assert_true(g_file_set_contents(outside_file, "keep", -1, NULL));
    g_assert_cmpint(symlink(outside, link), ==, 0);
    g_assert_cmpint(lmme_workspace_delete_path(workspace, link, NULL).result,
                    ==,
                    LMME_WORKSPACE_DELETE_COMPLETE);
    g_assert_false(g_file_test(link, G_FILE_TEST_EXISTS));
    g_assert_true(g_file_test(outside_file, G_FILE_TEST_IS_REGULAR));

    lmme_workspace_free(workspace);
    g_remove(outside_file);
    g_rmdir(outside);
    g_rmdir(root);
}

static void
test_intermediate_symlink_cannot_escape_workspace(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-workspace-XXXXXX", NULL);
    g_autofree char *outside = g_dir_make_tmp("lmme-test-outside-XXXXXX", NULL);
    g_autofree char *outside_file = g_build_filename(outside, "keep.md", NULL);
    g_autofree char *escaped_created = g_build_filename(outside, "escaped.md", NULL);
    g_autofree char *link = g_build_filename(root, "linked", NULL);
    g_autofree char *escaped_path = g_build_filename(link, "keep.md", NULL);
    g_autofree char *created_path = NULL;
    g_autoptr(GError) error = NULL;
    LmmeWorkspace *workspace = lmme_workspace_new(root);

    g_assert_true(g_file_set_contents(outside_file, "keep", -1, NULL));
    g_assert_cmpint(symlink(outside, link), ==, 0);
    g_assert_cmpint(lmme_workspace_delete_path(workspace, escaped_path, &error).result,
                    ==,
                    LMME_WORKSPACE_DELETE_UNCHANGED);
    g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_PERM);
    g_assert_true(g_file_test(outside_file, G_FILE_TEST_IS_REGULAR));

    g_clear_error(&error);
    g_assert_false(lmme_workspace_create_markdown_file(workspace,
                                                       link,
                                                       "escaped.md",
                                                       &created_path,
                                                       &error));
    g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_PERM);
    g_assert_null(created_path);
    g_assert_false(g_file_test(escaped_created, G_FILE_TEST_EXISTS));

    lmme_workspace_free(workspace);
    g_remove(link);
    g_remove(outside_file);
    g_rmdir(outside);
    g_rmdir(root);
}

static void
test_rename_is_committed_before_result(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-workspace-XXXXXX", NULL);
    g_autofree char *old_path = g_build_filename(root, "old", NULL);
    g_autofree char *new_path = NULL;
    LmmeWorkspace *workspace = lmme_workspace_new(root);

    g_assert_cmpint(g_mkdir(old_path, 0700), ==, 0);
    g_assert_true(lmme_workspace_rename_path(workspace, old_path, "new", &new_path, NULL));
    g_assert_false(g_file_test(old_path, G_FILE_TEST_EXISTS));
    g_assert_true(g_file_test(new_path, G_FILE_TEST_IS_DIR));

    lmme_workspace_free(workspace);
    g_rmdir(new_path);
    g_rmdir(root);
}

static void
test_save_target_containment(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-workspace-XXXXXX", NULL);
    g_autofree char *outside = g_dir_make_tmp("lmme-test-outside-XXXXXX", NULL);
    g_autofree char *nested = g_build_filename(root, "nested", NULL);
    g_autofree char *existing = g_build_filename(nested, "existing.md", NULL);
    g_autofree char *new_file = g_build_filename(nested, "new.md", NULL);
    g_autofree char *missing = g_build_filename(root, "missing", "new.md", NULL);
    g_autofree char *outside_file = g_build_filename(outside, "outside.md", NULL);
    g_autofree char *outside_new = g_build_filename(outside, "new.md", NULL);
    g_autofree char *link_outside_dir = g_build_filename(root, "outside-dir", NULL);
    g_autofree char *link_inside_dir = g_build_filename(root, "inside-dir", NULL);
    g_autofree char *via_outside_dir = g_build_filename(link_outside_dir, "new.md", NULL);
    g_autofree char *via_inside_dir = g_build_filename(link_inside_dir, "new.md", NULL);
    g_autofree char *link_outside_file = g_build_filename(root, "outside.md", NULL);
    g_autofree char *link_inside_file = g_build_filename(root, "inside.md", NULL);
    g_autoptr(GError) error = NULL;
    LmmeWorkspace *workspace = lmme_workspace_new(root);

    g_assert_cmpint(g_mkdir(nested, 0700), ==, 0);
    g_assert_true(g_file_set_contents(existing, "inside", -1, NULL));
    g_assert_true(g_file_set_contents(outside_file, "outside", -1, NULL));

    g_assert_true(lmme_workspace_validate_save_target(workspace, existing, NULL));
    g_assert_true(lmme_workspace_validate_save_target(workspace, new_file, NULL));

    g_assert_cmpint(symlink(outside, link_outside_dir), ==, 0);
    g_assert_false(lmme_workspace_validate_save_target(workspace, via_outside_dir, &error));
    g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_PERM);
    g_clear_error(&error);

    g_assert_cmpint(symlink(nested, link_inside_dir), ==, 0);
    g_assert_true(lmme_workspace_validate_save_target(workspace, via_inside_dir, NULL));

    g_assert_cmpint(symlink(outside_file, link_outside_file), ==, 0);
    g_assert_false(lmme_workspace_validate_save_target(workspace, link_outside_file, &error));
    g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_PERM);
    g_clear_error(&error);

    g_assert_cmpint(symlink(existing, link_inside_file), ==, 0);
    g_assert_true(lmme_workspace_validate_save_target(workspace, link_inside_file, NULL));

    g_assert_false(lmme_workspace_validate_save_target(workspace, missing, &error));
    g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_NOENT);
    g_clear_error(&error);

    g_assert_false(lmme_workspace_validate_save_target(workspace, root, &error));
    g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_PERM);
    g_clear_error(&error);

    g_assert_false(lmme_workspace_validate_save_target(workspace, outside_new, &error));
    g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_PERM);

    lmme_workspace_free(workspace);
    g_unlink(link_inside_file);
    g_unlink(link_outside_file);
    g_unlink(link_inside_dir);
    g_unlink(link_outside_dir);
    g_unlink(existing);
    g_rmdir(nested);
    g_unlink(outside_file);
    g_rmdir(outside);
    g_rmdir(root);
}

static void
test_large_workspace_is_loaded_lazily(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-workspace-XXXXXX", NULL);
    LmmeWorkspace *workspace = NULL;

    for (guint directory_index = 0; directory_index < 100; directory_index++) {
        g_autofree char *name = g_strdup_printf("dir-%03u", directory_index);
        g_autofree char *directory = g_build_filename(root, name, NULL);
        g_assert_cmpint(g_mkdir(directory, 0700), ==, 0);
        for (guint file_index = 0; file_index < 100; file_index++) {
            g_autofree char *file_name = g_strdup_printf("note-%03u.md", file_index);
            g_autofree char *path = g_build_filename(directory, file_name, NULL);
            g_assert_true(g_file_set_contents(path, "", 0, NULL));
        }
    }

    workspace = lmme_workspace_new(root);
    g_assert_true(lmme_workspace_rescan(workspace, TRUE, TRUE, NULL));
    g_assert_cmpuint(workspace->root->children->len, ==, 100);
    for (guint i = 0; i < workspace->root->children->len; i++) {
        LmmeFileNode *directory = g_ptr_array_index(workspace->root->children, i);
        g_assert_false(directory->loaded);
        g_assert_cmpuint(directory->children->len, ==, 0);
    }
    LmmeFileNode *first = g_ptr_array_index(workspace->root->children, 0);
    g_assert_true(lmme_workspace_load_directory(workspace, first, TRUE, TRUE, NULL));
    g_assert_true(first->loaded);
    g_assert_cmpuint(first->children->len, ==, 100);
    g_assert_false(((LmmeFileNode *)g_ptr_array_index(workspace->root->children, 1))->loaded);

    for (guint i = 0; i < workspace->root->children->len; i++) {
        LmmeFileNode *directory = g_ptr_array_index(workspace->root->children, i);
        LmmeWorkspaceDeleteOutcome outcome =
            lmme_workspace_delete_path(workspace, directory->path, NULL);
        g_assert_cmpint(outcome.result, ==, LMME_WORKSPACE_DELETE_COMPLETE);
    }
    lmme_workspace_free(workspace);
    g_rmdir(root);
}

static void
test_scanned_workspace_candidate_is_committed_only_on_success(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-workspace-candidate-XXXXXX", NULL);
    g_autofree char *missing = g_build_filename(root, "missing", NULL);
    g_autoptr(GError) error = NULL;
    LmmeWorkspace *current = lmme_workspace_new(root);
    LmmeWorkspace *candidate = lmme_workspace_new_scanned(missing, TRUE, TRUE, &error);

    g_assert_null(candidate);
    g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_NOENT);
    g_assert_cmpstr(current->path, ==, root);
    g_clear_error(&error);

    candidate = lmme_workspace_new_scanned(root, TRUE, TRUE, &error);
    g_assert_nonnull(candidate);
    g_assert_no_error(error);
    g_assert_nonnull(candidate->root);
    g_assert_cmpstr(current->path, ==, root);

    lmme_workspace_free(candidate);
    lmme_workspace_free(current);
    g_rmdir(root);
}

static void
test_delete_empty_directory(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-delete-empty-XXXXXX", NULL);
    g_autofree char *folder = g_build_filename(root, "empty", NULL);
    LmmeWorkspace *workspace = lmme_workspace_new(root);
    LmmeWorkspaceDeleteOutcome outcome = {0};

    g_assert_cmpint(g_mkdir(folder, 0700), ==, 0);
    outcome = lmme_workspace_delete_path(workspace, folder, NULL);
    g_assert_cmpint(outcome.result, ==, LMME_WORKSPACE_DELETE_COMPLETE);
    g_assert_false(outcome.target_still_exists);

    lmme_workspace_free(workspace);
    g_rmdir(root);
}

static void
test_delete_partial_failure_reports_outcome(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-delete-partial-XXXXXX", NULL);
    g_autofree char *folder = g_build_filename(root, "target", NULL);
    g_autofree char *file_a = g_build_filename(folder, "a.md", NULL);
    g_autofree char *file_b = g_build_filename(folder, "b.md", NULL);
    g_autoptr(GError) partial_error = NULL;
    g_autoptr(GError) unchanged_error = NULL;
    LmmeWorkspace *workspace = lmme_workspace_new(root);
    LmmeWorkspaceDeleteOutcome outcome = {0};

    g_assert_cmpint(g_mkdir(folder, 0700), ==, 0);
    g_assert_true(g_file_set_contents(file_a, "a", -1, NULL));
    g_assert_true(g_file_set_contents(file_b, "b", -1, NULL));

    lmme_workspace_delete_test_fail_at(1, 1);
    outcome = lmme_workspace_delete_path(workspace, folder, &partial_error);
    lmme_workspace_delete_test_reset();
    g_assert_cmpint(outcome.result, ==, LMME_WORKSPACE_DELETE_PARTIAL);
    g_assert_cmpuint(outcome.deleted_entries, ==, 1);
    g_assert_true(outcome.target_still_exists);
    g_assert_nonnull(partial_error);

    lmme_workspace_delete_test_fail_at(0, 1);
    outcome = lmme_workspace_delete_path(workspace, folder, &unchanged_error);
    lmme_workspace_delete_test_reset();
    g_assert_cmpint(outcome.result, ==, LMME_WORKSPACE_DELETE_UNCHANGED);
    g_assert_cmpuint(outcome.deleted_entries, ==, 0);
    g_assert_nonnull(unchanged_error);

    lmme_workspace_free(workspace);
    g_rmdir(folder);
    g_rmdir(root);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/workspace/delete/root-guard", test_recursive_delete_and_root_guard);
    g_test_add_func("/workspace/delete/symlink", test_symlink_delete_does_not_follow_target);
    g_test_add_func("/workspace/delete/intermediate-symlink", test_intermediate_symlink_cannot_escape_workspace);
    g_test_add_func("/workspace/rename/committed", test_rename_is_committed_before_result);
    g_test_add_func("/workspace/save-target/containment", test_save_target_containment);
    g_test_add_func("/workspace/lazy/ten-thousand-files", test_large_workspace_is_loaded_lazily);
    g_test_add_func("/workspace/delete/empty-directory", test_delete_empty_directory);
    g_test_add_func("/workspace/delete/partial-failure", test_delete_partial_failure_reports_outcome);
    g_test_add_func("/workspace/candidate/transactional", test_scanned_workspace_candidate_is_committed_only_on_success);
    return g_test_run();
}
