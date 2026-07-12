#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "app/app.h"
#include "ui/file_tree_view.h"
#include "workspace/workspace.h"

static void
create_fixture(const char *root)
{
    for (guint directory_index = 0; directory_index < 100; directory_index++) {
        g_autofree char *directory_name = g_strdup_printf("dir-%03u", directory_index);
        g_autofree char *directory = g_build_filename(root, directory_name, NULL);
        g_assert_cmpint(g_mkdir(directory, 0700), ==, 0);
        for (guint file_index = 0; file_index < 100; file_index++) {
            g_autofree char *file_name = g_strdup_printf("note-%03u.md", file_index);
            g_autofree char *path = g_build_filename(directory, file_name, NULL);
            g_assert_true(g_file_set_contents(path, "", 0, NULL));
        }
    }
}

static void
remove_fixture(const char *root)
{
    for (guint directory_index = 0; directory_index < 100; directory_index++) {
        g_autofree char *directory_name = g_strdup_printf("dir-%03u", directory_index);
        g_autofree char *directory = g_build_filename(root, directory_name, NULL);
        for (guint file_index = 0; file_index < 100; file_index++) {
            g_autofree char *file_name = g_strdup_printf("note-%03u.md", file_index);
            g_autofree char *path = g_build_filename(directory, file_name, NULL);
            g_unlink(path);
        }
        g_rmdir(directory);
    }
}

int
main(void)
{
    g_autofree char *root = NULL;
    g_autofree char *created = NULL;
    LmmeApp app = {0};
    GtkWidget *tree = NULL;
    gpointer model_identity = NULL;
    guint monitor_count = 0;
    gint64 started = 0;
    gint64 elapsed = 0;

    if (!gtk_init_check()) {
        g_print("tree refresh benchmark skipped: no display\n");
        return 0;
    }

    root = g_dir_make_tmp("lmme-tree-benchmark-XXXXXX", NULL);
    create_fixture(root);
    app.workspace = lmme_workspace_new_scanned(root, TRUE, TRUE, NULL);
    tree = g_object_ref_sink(lmme_file_tree_create(&app));
    app.tree_view = tree;
    lmme_file_tree_populate(tree, app.workspace, TRUE, TRUE);
    model_identity = lmme_file_tree_model_identity(tree);
    monitor_count = lmme_file_tree_monitor_count(tree);

    created = g_build_filename(root, "created.md", NULL);
    g_assert_true(g_file_set_contents(created, "", 0, NULL));
    started = g_get_monotonic_time();
    g_assert_true(lmme_file_tree_refresh_directory(tree, root, NULL));
    elapsed = g_get_monotonic_time() - started;

    g_assert_true(lmme_file_tree_model_identity(tree) == model_identity);
    g_assert_cmpuint(lmme_file_tree_monitor_count(tree), ==, monitor_count);
    g_print("100 directories x 100 files, root create refresh: %.3f ms, %u monitors\n",
            (double)elapsed / 1000.0,
            monitor_count);

    g_object_unref(tree);
    g_unlink(created);
    remove_fixture(root);
    lmme_workspace_free(app.workspace);
    lmme_path_context_clear(&app.selection);
    lmme_path_context_clear(&app.tree_context);
    g_rmdir(root);
    return 0;
}
