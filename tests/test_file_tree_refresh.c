/*
 * Regression tests for file-tree crash and lifecycle issues.
 * Reproduces stale-node / load-failure paths through the test harness.
 */
#include <glib.h>
#include <glib/gstdio.h>

#include "app/app.h"
#include "ui/file_tree_view.h"
#include "ui/file_tree_view_test.h"
#include "workspace/workspace.h"

static gboolean gtk_available = FALSE;

typedef struct {
    guint events;
} DirectoryMonitorProbe;

static void
on_directory_probe_changed(GFileMonitor *monitor,
                           GFile *file,
                           GFile *other_file,
                           GFileMonitorEvent event_type,
                           gpointer user_data)
{
    DirectoryMonitorProbe *probe = user_data;
    (void)monitor;
    (void)file;
    (void)other_file;
    (void)event_type;
    probe->events++;
}

static void
on_tree_monitor_finalized(gpointer user_data, GObject *where_the_object_was)
{
    gboolean *finalized = user_data;
    (void)where_the_object_was;
    *finalized = TRUE;
}

static void
iterate_main_context_for(guint timeout_ms)
{
    const gint64 deadline = g_get_monotonic_time() +
                            ((gint64)timeout_ms * G_TIME_SPAN_MILLISECOND);

    do {
        while (g_main_context_pending(NULL)) {
            g_main_context_iteration(NULL, FALSE);
        }
        g_usleep(1000);
    } while (g_get_monotonic_time() < deadline);
}

static gboolean
wait_for_tree_monitor_finalized(const gboolean *finalized, guint timeout_ms)
{
    const gint64 deadline = g_get_monotonic_time() +
                            ((gint64)timeout_ms * G_TIME_SPAN_MILLISECOND);

    do {
        while (g_main_context_pending(NULL)) {
            g_main_context_iteration(NULL, FALSE);
        }
        if (*finalized) {
            return TRUE;
        }
        g_usleep(1000);
    } while (g_get_monotonic_time() < deadline);
    return FALSE;
}

static gboolean
wait_for_directory_event(const DirectoryMonitorProbe *probe,
                         guint previous,
                         guint timeout_ms)
{
    const gint64 deadline = g_get_monotonic_time() +
                            ((gint64)timeout_ms * G_TIME_SPAN_MILLISECOND);

    do {
        while (g_main_context_pending(NULL)) {
            g_main_context_iteration(NULL, FALSE);
        }
        if (probe->events != previous) {
            return TRUE;
        }
        g_usleep(1000);
    } while (g_get_monotonic_time() < deadline);
    return FALSE;
}

static gboolean
wait_for_bound_row(GtkWidget *tree_view,
                   const char *path,
                   LmmeFileTreeTestRowSnapshot *snapshot)
{
    const gint64 deadline = g_get_monotonic_time() + (2 * G_TIME_SPAN_SECOND);

    do {
        if (lmme_file_tree_test_snapshot_bound_row(tree_view, path, snapshot)) {
            return TRUE;
        }
        while (g_main_context_pending(NULL)) {
            g_main_context_iteration(NULL, FALSE);
        }
        g_usleep(1000);
    } while (g_get_monotonic_time() < deadline);
    return FALSE;
}

static gboolean
wait_for_unbound_expander(GtkWidget *expander,
                          LmmeFileTreeTestRowSnapshot *snapshot)
{
    const gint64 deadline = g_get_monotonic_time() + (2 * G_TIME_SPAN_SECOND);

    do {
        if (lmme_file_tree_test_snapshot_expander(expander, snapshot) &&
            !snapshot->has_list_row && snapshot->path == NULL &&
            snapshot->kind == 0 && snapshot->position == 0) {
            return TRUE;
        }
        while (g_main_context_pending(NULL)) {
            g_main_context_iteration(NULL, FALSE);
        }
        g_usleep(1000);
    } while (g_get_monotonic_time() < deadline);
    return FALSE;
}

static void
test_factory_bind_unbind_metadata(void)
{
    g_autofree char *root = NULL;
    g_autofree char *note = NULL;
    LmmeFileTreeTestRowSnapshot snapshot = {0};
    LmmeApp app = {0};
    GtkWidget *window = NULL;
    GtkWidget *expander = NULL;

    if (!gtk_available) {
        g_test_skip("GTK display backend is unavailable");
        return;
    }

    root = g_dir_make_tmp("lmme-test-tree-factory-XXXXXX", NULL);
    note = g_build_filename(root, "note.md", NULL);
    g_assert_true(g_file_set_contents(note, "# note\n", -1, NULL));
    app.workspace = lmme_workspace_new_scanned(root, TRUE, TRUE, NULL);
    g_assert_nonnull(app.workspace);

    window = gtk_window_new();
    gtk_window_set_default_size(GTK_WINDOW(window), 480, 320);
    app.tree_view = lmme_file_tree_create(&app);
    gtk_window_set_child(GTK_WINDOW(window), app.tree_view);
    lmme_file_tree_populate(app.tree_view, app.workspace, TRUE, TRUE);
    gtk_window_present(GTK_WINDOW(window));

    g_assert_true(wait_for_bound_row(app.tree_view, note, &snapshot));
    g_assert_true(snapshot.has_list_row);
    g_assert_true(snapshot.has_icon);
    g_assert_true(snapshot.has_label);
    g_assert_cmpstr(snapshot.path, ==, note);
    g_assert_cmpint(snapshot.kind, ==, LMME_FILE_KIND_MARKDOWN);
    g_assert_cmpuint(snapshot.position, >, 0);
    g_assert_cmpstr(snapshot.label, ==, "note.md");
    g_assert_cmpstr(snapshot.icon_name, ==, "text-x-markdown-symbolic");
    expander = lmme_file_tree_test_ref_bound_expander(app.tree_view, note);
    g_assert_nonnull(expander);

    lmme_file_tree_populate(app.tree_view, NULL, TRUE, TRUE);
    g_assert_true(wait_for_unbound_expander(expander, &snapshot));
    g_assert_true(snapshot.has_icon);
    g_assert_true(snapshot.has_label);

    lmme_file_tree_populate(app.tree_view, app.workspace, TRUE, TRUE);
    g_assert_true(wait_for_bound_row(app.tree_view, note, &snapshot));
    g_assert_cmpstr(snapshot.path, ==, note);
    g_assert_cmpint(snapshot.kind, ==, LMME_FILE_KIND_MARKDOWN);
    g_assert_cmpstr(snapshot.label, ==, "note.md");

    lmme_file_tree_test_row_snapshot_clear(&snapshot);
    g_object_unref(expander);
    gtk_window_destroy(GTK_WINDOW(window));
    app.tree_view = NULL;
    lmme_workspace_free(app.workspace);
    lmme_path_context_clear(&app.selection);
    lmme_path_context_clear(&app.tree_context);
    g_unlink(note);
    g_rmdir(root);
}

static void
test_directory_monitor_owner_teardown(void)
{
    g_autofree char *root = NULL;
    g_autofree char *event_path = NULL;
    g_autoptr(GFile) event_file = NULL;
    LmmeApp app = {0};
    GObject *monitor = NULL;
    gboolean monitor_finalized = FALSE;

    if (!gtk_available) {
        g_test_skip("GTK display backend is unavailable");
        return;
    }

    root = g_dir_make_tmp("lmme-test-tree-monitor-teardown-XXXXXX", NULL);
    event_path = g_build_filename(root, "event.md", NULL);
    event_file = g_file_new_for_path(event_path);
    app.workspace = lmme_workspace_new_scanned(root, TRUE, TRUE, NULL);
    app.tree_view = lmme_file_tree_create(&app);
    lmme_file_tree_populate(app.tree_view, app.workspace, TRUE, TRUE);
    monitor = lmme_file_tree_test_ref_directory_monitor(app.tree_view, root);
    if (!G_IS_FILE_MONITOR(monitor)) {
        g_clear_object(&monitor);
        g_test_skip("GFileMonitor directory backend is unavailable");
        g_object_ref_sink(app.tree_view);
        g_object_unref(app.tree_view);
        lmme_workspace_free(app.workspace);
        g_rmdir(root);
        return;
    }
    g_object_weak_ref(monitor, on_tree_monitor_finalized, &monitor_finalized);
    g_signal_emit_by_name(monitor,
                          "changed",
                          event_file,
                          NULL,
                          G_FILE_MONITOR_EVENT_CREATED);
    g_assert_cmpuint(lmme_file_tree_test_monitor_timeout_id(app.tree_view), !=, 0);
    g_object_unref(monitor);

    lmme_file_tree_populate(app.tree_view, NULL, TRUE, TRUE);
    g_assert_true(wait_for_tree_monitor_finalized(&monitor_finalized, 2000));
    iterate_main_context_for(300);

    lmme_file_tree_populate(app.tree_view, app.workspace, TRUE, TRUE);
    g_assert_nonnull(lmme_file_tree_model_identity(app.tree_view));
    g_assert_cmpuint(lmme_file_tree_monitor_count(app.tree_view), ==, 1);

    lmme_file_tree_populate(app.tree_view, NULL, TRUE, TRUE);
    g_object_ref_sink(app.tree_view);
    g_object_unref(app.tree_view);
    app.tree_view = NULL;
    lmme_workspace_free(app.workspace);
    lmme_path_context_clear(&app.selection);
    lmme_path_context_clear(&app.tree_context);
    g_rmdir(root);
}

static void
test_directory_monitor_filesystem_race(void)
{
    g_autofree char *root = NULL;
    g_autofree char *event_path = NULL;
    g_autoptr(GFile) directory = NULL;
    g_autoptr(GFileMonitor) probe_monitor = NULL;
    g_autoptr(GError) error = NULL;
    DirectoryMonitorProbe probe = {0};
    LmmeApp app = {0};
    GObject *target_monitor = NULL;
    gboolean target_finalized = FALSE;
    gboolean backend_exercised = FALSE;

    if (!gtk_available) {
        g_test_skip("GTK display backend is unavailable");
        return;
    }

    root = g_dir_make_tmp("lmme-test-tree-monitor-race-XXXXXX", NULL);
    event_path = g_build_filename(root, "event.md", NULL);
    directory = g_file_new_for_path(root);
    probe_monitor = g_file_monitor_directory(directory, G_FILE_MONITOR_NONE, NULL, &error);
    if (probe_monitor == NULL) {
        g_test_skip("GFileMonitor directory backend is unavailable");
        g_rmdir(root);
        return;
    }
    g_signal_connect(probe_monitor,
                     "changed",
                     G_CALLBACK(on_directory_probe_changed),
                     &probe);

    app.workspace = lmme_workspace_new_scanned(root, TRUE, TRUE, NULL);
    app.tree_view = lmme_file_tree_create(&app);
    lmme_file_tree_populate(app.tree_view, app.workspace, TRUE, TRUE);
    target_monitor = lmme_file_tree_test_ref_directory_monitor(app.tree_view, root);
    g_assert_true(G_IS_FILE_MONITOR(target_monitor));
    g_object_weak_ref(target_monitor, on_tree_monitor_finalized, &target_finalized);
    g_object_unref(target_monitor);

    g_assert_true(g_file_set_contents(event_path, "event", -1, NULL));
    lmme_file_tree_populate(app.tree_view, NULL, TRUE, TRUE);
    backend_exercised = wait_for_directory_event(&probe, 0, 2000);
    g_assert_true(wait_for_tree_monitor_finalized(&target_finalized, 2000));

    g_assert_true(lmme_workspace_refresh_directory(app.workspace,
                                                   root,
                                                   TRUE,
                                                   TRUE,
                                                   NULL));
    lmme_file_tree_populate(app.tree_view, app.workspace, TRUE, TRUE);
    g_assert_true(lmme_file_tree_select_path(app.tree_view, event_path));

    lmme_file_tree_populate(app.tree_view, NULL, TRUE, TRUE);
    g_object_ref_sink(app.tree_view);
    g_object_unref(app.tree_view);
    app.tree_view = NULL;
    g_file_monitor_cancel(probe_monitor);
    lmme_workspace_free(app.workspace);
    lmme_path_context_clear(&app.selection);
    lmme_path_context_clear(&app.tree_context);
    g_unlink(event_path);
    g_rmdir(root);

    if (!backend_exercised) {
        g_test_skip("GFileMonitor backend produced no event during the bounded race probe");
    }
}

static void
test_create_child_model_after_ancestor_refresh(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-tree-refresh-XXXXXX", NULL);
    g_autofree char *a = g_build_filename(root, "a", NULL);
    g_autofree char *b = g_build_filename(a, "b", NULL);
    g_autofree char *note = g_build_filename(b, "note.md", NULL);
    g_autofree char *retained_child_path = NULL;
    g_autoptr(GObject) retained_b_row = NULL;
    g_autoptr(GListStore) retained_b_store = NULL;
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
    g_assert_false(lmme_file_tree_test_has_monitor(model, b));
    g_assert_cmpuint(lmme_file_tree_test_monitor_count(model), ==, monitor_count - 1);

    g_assert_cmpint(g_mkdir(b, 0700), ==, 0);
    g_assert_true(g_file_set_contents(note, "# note\n", -1, NULL));
    g_assert_true(lmme_file_tree_test_refresh_directory(model, a, NULL));
    g_assert_true(lmme_file_tree_test_expand_path(model, b));
    g_assert_true(lmme_file_tree_test_has_monitor(model, b));
    g_assert_cmpuint(lmme_file_tree_test_refresh_count(model, a), >=, 3);
    g_assert_true(lmme_file_tree_test_contains_path(model, note));

    g_clear_object(&retained_child);
    g_clear_object(&resolved_b_store);
    g_clear_object(&retained_b_store);
    g_clear_object(&retained_b_row);
    lmme_file_tree_test_model_free(model);
    lmme_workspace_free(app.workspace);
    lmme_path_context_clear(&app.selection);
    lmme_path_context_clear(&app.tree_context);
    g_unlink(note);
    g_rmdir(b);
    g_rmdir(a);
    g_rmdir(root);
}

static void
test_create_child_model_load_error_returns_null(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-tree-load-error-XXXXXX", NULL);
    g_autofree char *child = g_build_filename(root, "child", NULL);
    g_autoptr(GObject) retained_row = NULL;
    g_autoptr(GListModel) child_model = NULL;
    LmmeApp app = {0};
    LmmeFileTreeTestModel *model = NULL;

    g_assert_cmpint(g_mkdir(child, 0700), ==, 0);
    app.workspace = lmme_workspace_new_scanned(root, TRUE, TRUE, NULL);
    model = lmme_file_tree_test_model_new(&app, app.workspace, TRUE, TRUE);
    g_assert_nonnull(model);
    g_assert_true(lmme_file_tree_test_expand_path(model, root));
    g_assert_true(lmme_file_tree_test_expand_path(model, child));
    retained_row = lmme_file_tree_test_ref_item(model, child);
    g_assert_nonnull(retained_row);

    g_assert_cmpint(g_rmdir(child), ==, 0);
    g_assert_true(lmme_file_tree_test_refresh_directory(model, root, NULL));
    g_assert_false(lmme_file_tree_test_has_monitor(model, child));

    g_test_expect_message(G_LOG_DOMAIN, G_LOG_LEVEL_WARNING, "*Could not load workspace directory*");
    child_model = lmme_file_tree_test_create_child_model(model, retained_row);
    g_test_assert_expected_messages();
    g_assert_null(child_model);

    g_clear_object(&retained_row);
    lmme_file_tree_test_model_free(model);
    lmme_workspace_free(app.workspace);
    lmme_path_context_clear(&app.selection);
    lmme_path_context_clear(&app.tree_context);
    g_rmdir(root);
}

static void
test_directory_recreate_monitor_and_refresh(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-tree-recreate-XXXXXX", NULL);
    g_autofree char *child = g_build_filename(root, "child", NULL);
    g_autofree char *note = g_build_filename(child, "note.md", NULL);
    LmmeApp app = {0};
    LmmeFileTreeTestModel *model = NULL;
    guint refresh_before = 0;

    g_assert_cmpint(g_mkdir(child, 0700), ==, 0);
    g_assert_true(g_file_set_contents(note, "one", -1, NULL));
    app.workspace = lmme_workspace_new_scanned(root, TRUE, TRUE, NULL);
    model = lmme_file_tree_test_model_new(&app, app.workspace, TRUE, TRUE);
    g_assert_true(lmme_file_tree_test_expand_path(model, root));
    g_assert_true(lmme_file_tree_test_expand_path(model, child));
    g_assert_true(lmme_file_tree_test_has_monitor(model, child));
    refresh_before = lmme_file_tree_test_refresh_count(model, root);

    g_assert_cmpint(g_unlink(note), ==, 0);
    g_assert_cmpint(g_rmdir(child), ==, 0);
    g_assert_true(lmme_file_tree_test_refresh_directory(model, root, NULL));
    g_assert_false(lmme_file_tree_test_has_monitor(model, child));

    g_assert_cmpint(g_mkdir(child, 0700), ==, 0);
    g_assert_true(g_file_set_contents(note, "two", -1, NULL));
    g_assert_true(lmme_file_tree_test_refresh_directory(model, root, NULL));
    g_assert_true(lmme_file_tree_test_expand_path(model, child));
    g_assert_true(lmme_file_tree_test_has_monitor(model, child));
    g_assert_cmpuint(lmme_file_tree_test_refresh_count(model, root), >, refresh_before);
    g_assert_true(lmme_file_tree_test_contains_path(model, note));

    lmme_file_tree_test_model_free(model);
    lmme_workspace_free(app.workspace);
    lmme_path_context_clear(&app.selection);
    lmme_path_context_clear(&app.tree_context);
    g_unlink(note);
    g_rmdir(child);
    g_rmdir(root);
}

static void
test_refresh_expand_cycles_stable_monitors(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-tree-cycles-XXXXXX", NULL);
    g_autofree char *child = g_build_filename(root, "child", NULL);
    g_autofree char *note = NULL;
    LmmeApp app = {0};
    LmmeFileTreeTestModel *model = NULL;
    const guint expected_monitors = 2;

    g_assert_cmpint(g_mkdir(child, 0700), ==, 0);
    app.workspace = lmme_workspace_new_scanned(root, TRUE, TRUE, NULL);
    model = lmme_file_tree_test_model_new(&app, app.workspace, TRUE, TRUE);
    g_assert_true(lmme_file_tree_test_expand_path(model, root));
    g_assert_true(lmme_file_tree_test_expand_path(model, child));
    g_assert_cmpuint(lmme_file_tree_test_monitor_count(model), ==, expected_monitors);

    for (guint cycle = 0; cycle < 100; cycle++) {
        note = g_strdup_printf("%s/note-%03u.md", child, cycle);
        g_assert_true(g_file_set_contents(note, "x", 1, NULL));
        g_assert_true(lmme_file_tree_test_refresh_directory(model, child, NULL));
        g_assert_cmpuint(lmme_file_tree_test_monitor_count(model), ==, expected_monitors);
        g_assert_true(lmme_file_tree_test_has_monitor(model, root));
        g_assert_true(lmme_file_tree_test_has_monitor(model, child));
        g_assert_true(lmme_file_tree_test_contains_path(model, note));
        g_assert_cmpint(g_unlink(note), ==, 0);
        g_free(note);
        note = NULL;
        g_assert_true(lmme_file_tree_test_refresh_directory(model, child, NULL));
        g_assert_cmpuint(lmme_file_tree_test_monitor_count(model), ==, expected_monitors);
    }

    lmme_file_tree_test_model_free(model);
    lmme_workspace_free(app.workspace);
    lmme_path_context_clear(&app.selection);
    lmme_path_context_clear(&app.tree_context);
    g_rmdir(child);
    g_rmdir(root);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_setenv("GTK_A11Y", "none", TRUE);
    gtk_available = gtk_init_check();
    g_test_add_func("/file-tree/factory/bind-unbind-metadata",
                    test_factory_bind_unbind_metadata);
    g_test_add_func("/file-tree/monitor/owner-teardown",
                    test_directory_monitor_owner_teardown);
    g_test_add_func("/file-tree/monitor/filesystem-race",
                    test_directory_monitor_filesystem_race);
    g_test_add_func("/file-tree/refresh/cached-nested-row",
                    test_create_child_model_after_ancestor_refresh);
    g_test_add_func("/file-tree/refresh/load-error-null-model",
                    test_create_child_model_load_error_returns_null);
    g_test_add_func("/file-tree/refresh/recreate-monitor",
                    test_directory_recreate_monitor_and_refresh);
    g_test_add_func("/file-tree/refresh/hundred-cycles",
                    test_refresh_expand_cycles_stable_monitors);
    return g_test_run();
}
