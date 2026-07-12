/*
 * Diagnostic reproducer for file-tree UAF class (ancestor refresh frees node).
 * After the fix, create_child_model must return NULL without dereferencing freed nodes.
 */
#include <glib.h>
#include <glib/gstdio.h>

#include "app/app.h"
#include "ui/file_tree_view_test.h"
#include "workspace/workspace.h"

static void
test_stale_row_create_child_model_is_safe(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-crash-repro-XXXXXX", NULL);
    g_autofree char *parent = g_build_filename(root, "parent", NULL);
    g_autofree char *child = g_build_filename(parent, "child", NULL);
    g_autoptr(GObject) retained_row = NULL;
    g_autoptr(GListModel) child_model = NULL;
    LmmeApp app = {0};
    LmmeFileTreeTestModel *model = NULL;

    g_assert_cmpint(g_mkdir(parent, 0700), ==, 0);
    g_assert_cmpint(g_mkdir(child, 0700), ==, 0);

    app.workspace = lmme_workspace_new_scanned(root, TRUE, TRUE, NULL);
    model = lmme_file_tree_test_model_new(&app, app.workspace, TRUE, TRUE);
    g_assert_nonnull(model);
    g_assert_true(lmme_file_tree_test_expand_path(model, root));
    g_assert_true(lmme_file_tree_test_expand_path(model, parent));
    g_assert_true(lmme_file_tree_test_expand_path(model, child));
    retained_row = lmme_file_tree_test_ref_item(model, child);
    g_assert_nonnull(retained_row);

    g_assert_true(lmme_file_tree_test_refresh_directory(model, parent, NULL));
    g_assert_cmpint(g_rmdir(child), ==, 0);
    g_assert_true(lmme_file_tree_test_refresh_directory(model, parent, NULL));

    g_test_expect_message(G_LOG_DOMAIN, G_LOG_LEVEL_WARNING, "*Could not load workspace directory*");
    child_model = lmme_file_tree_test_create_child_model(model, retained_row);
    g_test_assert_expected_messages();
    g_assert_null(child_model);

    g_clear_object(&retained_row);
    lmme_file_tree_test_model_free(model);
    lmme_workspace_free(app.workspace);
    lmme_path_context_clear(&app.selection);
    lmme_path_context_clear(&app.tree_context);
    g_rmdir(parent);
    g_rmdir(root);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/file-tree/crash-repro/stale-row-safe",
                    test_stale_row_create_child_model_is_safe);
    return g_test_run();
}
