#include <glib.h>
#include <glib/gstdio.h>

#include "app/app.h"
#include "ui/file_tree_view.h"
#include "ui/file_tree_view_test.h"
#include "workspace/workspace.h"

static void
test_cached_nested_row_survives_ancestor_refresh(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-tree-refresh-XXXXXX", NULL);
    g_autofree char *a = g_build_filename(root, "a", NULL);
    g_autofree char *b = g_build_filename(a, "b", NULL);
    g_autofree char *note = g_build_filename(b, "note.md", NULL);
    g_autofree char *swap = g_build_filename(a, "swap.md", NULL);
    g_autofree char *swap_note = g_build_filename(swap, "inside.md", NULL);
    g_autofree char *retained_child_path = NULL;
    g_autoptr(GObject) retained_b_row = NULL;
    g_autoptr(GListStore) retained_b_store = NULL;
    g_autoptr(GListStore) swap_store = NULL;
    g_autoptr(GListModel) resolved_b_store = NULL;
    g_autoptr(GObject) retained_child = NULL;
    gpointer model_identity = NULL;
    guint monitor_count = 0;
    LmmeApp app = {0};
    LmmeFileTreeTestModel *model = NULL;
    LmmeFileNode *current = NULL;

    g_assert_cmpint(g_mkdir(a, 0700), ==, 0);
    g_assert_cmpint(g_mkdir(b, 0700), ==, 0);
    g_assert_true(g_file_set_contents(note, "# note\n", -1, NULL));

    app.workspace = lmme_workspace_new_scanned(root, TRUE, TRUE, NULL);
    g_assert_nonnull(app.workspace);
    model = lmme_file_tree_test_model_new(&app, app.workspace, TRUE, TRUE);
    g_assert_nonnull(model);
    model_identity = lmme_file_tree_test_model_identity(model);

    g_assert_true(lmme_file_tree_test_expand_path(model, root));
    g_assert_true(lmme_file_tree_test_expand_path(model, a));
    g_assert_true(lmme_file_tree_test_expand_path(model, b));
    retained_b_row = lmme_file_tree_test_ref_item(model, b);
    retained_b_store = lmme_file_tree_test_ref_child_store(model, b);
    g_assert_nonnull(retained_b_row);
    g_assert_nonnull(retained_b_store);
    g_assert_cmpuint(g_list_model_get_n_items(G_LIST_MODEL(retained_b_store)), ==, 1);
    monitor_count = lmme_file_tree_test_monitor_count(model);
    g_assert_cmpuint(monitor_count, ==, 3);

    /* Refreshing a frees its old b node while these exact UI objects stay alive. */
    g_assert_true(lmme_file_tree_test_refresh_directory(model, a, NULL));
    current = lmme_workspace_find_node(app.workspace, b);
    g_assert_nonnull(current);
    g_assert_cmpint(current->kind, ==, LMME_FILE_KIND_DIRECTORY);
    resolved_b_store = lmme_file_tree_test_create_child_model(model, retained_b_row);
    g_assert_nonnull(resolved_b_store);
    g_assert_true(resolved_b_store == G_LIST_MODEL(retained_b_store));
    retained_child = g_list_model_get_item(resolved_b_store, 0);
    retained_child_path = lmme_file_tree_test_dup_item_path(retained_child);
    g_assert_cmpstr(retained_child_path, ==, note);
    g_assert_true(lmme_file_tree_test_expand_path(model, b));
    g_assert_true(lmme_file_tree_test_contains_path(model, note));
    g_assert_true(lmme_file_tree_test_model_identity(model) == model_identity);
    g_assert_cmpuint(lmme_file_tree_test_monitor_count(model), ==, monitor_count);

    g_assert_cmpint(g_unlink(note), ==, 0);
    g_assert_cmpint(g_rmdir(b), ==, 0);
    g_assert_true(lmme_file_tree_test_refresh_directory(model, a, NULL));
    g_clear_object(&resolved_b_store);
    g_assert_null(lmme_file_tree_test_ref_child_store(model, b));
    g_assert_false(lmme_file_tree_test_contains_path(model, b));
    g_assert_cmpuint(lmme_file_tree_test_monitor_count(model), ==, monitor_count - 1);

    g_assert_cmpint(g_mkdir(swap, 0700), ==, 0);
    g_assert_true(g_file_set_contents(swap_note, "inside", -1, NULL));
    g_assert_true(lmme_file_tree_test_refresh_directory(model, a, NULL));
    g_assert_true(lmme_file_tree_test_expand_path(model, swap));
    swap_store = lmme_file_tree_test_ref_child_store(model, swap);
    g_assert_nonnull(swap_store);
    monitor_count = lmme_file_tree_test_monitor_count(model);
    g_assert_cmpint(g_unlink(swap_note), ==, 0);
    g_assert_cmpint(g_rmdir(swap), ==, 0);
    g_assert_true(g_file_set_contents(swap, "now markdown", -1, NULL));
    g_assert_true(lmme_file_tree_test_refresh_directory(model, a, NULL));
    current = lmme_workspace_find_node(app.workspace, swap);
    g_assert_nonnull(current);
    g_assert_cmpint(current->kind, ==, LMME_FILE_KIND_MARKDOWN);
    g_assert_null(lmme_file_tree_test_ref_child_store(model, swap));
    g_assert_cmpuint(lmme_file_tree_test_monitor_count(model), ==, monitor_count - 1);

    g_clear_object(&retained_child);
    g_clear_object(&resolved_b_store);
    g_clear_object(&swap_store);
    g_clear_object(&retained_b_store);
    g_clear_object(&retained_b_row);
    lmme_file_tree_test_model_free(model);
    lmme_workspace_free(app.workspace);
    lmme_path_context_clear(&app.selection);
    lmme_path_context_clear(&app.tree_context);
    g_unlink(swap);
    g_rmdir(a);
    g_rmdir(root);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/file-tree/refresh/cached-nested-row",
                    test_cached_nested_row_survives_ancestor_refresh);
    return g_test_run();
}
