#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include "app/app.h"
#include "app/app_test.h"
#include "command/command_actions.h"
#include "command/command_actions_test.h"
#include "document/document.h"
#include "document/document_autosave.h"
#include "document/recovery.h"
#include "document/tabs.h"
#include "document/tabs_test.h"
#include "features/image_insert.h"
#include "infra/config.h"
#include "ui/external_conflict.h"
#include "ui/window.h"
#include "workspace/workspace.h"

typedef struct {
    LmmeDocument *doc;
    gboolean view_destroy_notified;
    gboolean document_alive_on_view_destroy;
} ViewDestroyTracker;

typedef struct {
    LmmeApp *app;
    char *workspace_root;
    char *note_a;
    char *note_b;
} SessionRunContext;

static guint test_window_app_serial = 0;

static gboolean
drain_main_context(guint max_iterations)
{
    return lmme_app_test_drain_main_context(max_iterations);
}

static LmmeApp *
test_app_with_gtk_application(GtkApplication **out_gtk_app)
{
    GtkApplication *gtk_app = gtk_application_new("org.lmme.TestLifecycle",
                                                  G_APPLICATION_NON_UNIQUE);
    LmmeApp *app = g_new0(LmmeApp, 1);

    app->gtk_app = gtk_app;
    app->documents = g_ptr_array_new();
    lmme_app_test_attach_lifetime(app);
    *out_gtk_app = gtk_app;
    return app;
}

static void
test_app_fixture_teardown(LmmeApp *app, GtkApplication *gtk_app)
{
    lmme_command_actions_cancel_refresh(app);
    if (app->lifetime != NULL) {
        lmme_app_test_cancel_pending_work(app);
        lmme_app_lifetime_unref(app->lifetime);
        app->lifetime = NULL;
    }
    if (app->documents != NULL) {
        g_ptr_array_unref(app->documents);
    }
    app->gtk_app = NULL;
    g_clear_object(&gtk_app);
    g_free(app);
}

static LmmeApp *
test_app_with_window(GtkApplication **out_gtk_app, char **out_root)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-app-lifecycle-XXXXXX", NULL);
    g_autofree char *recovery_dir = g_build_filename(root, "recovery", NULL);
    g_autofree char *config_path = g_build_filename(root, "config.ini", NULL);
    g_autofree char *app_id = g_strdup_printf("org.lmme.test.w%04u",
                                              (guint)++test_window_app_serial);
    GtkApplication *gtk_app = gtk_application_new(app_id, G_APPLICATION_NON_UNIQUE);
    LmmeApp *app = g_new0(LmmeApp, 1);

    g_mkdir_with_parents(recovery_dir, 0700);
    app->gtk_app = gtk_app;
    app->documents = g_ptr_array_new();
    app->config_path = g_strdup(config_path);
    app->recovery_store = lmme_recovery_store_new(recovery_dir);
    lmme_config_init_defaults(&app->config);
    app->config.autosave = TRUE;
    app->config.autosave_delay_ms = 50;
    app->config.show_statusbar = TRUE;
    app->preview_enabled = TRUE;
    app->config.preview_update_delay_ms = 50;

    lmme_app_test_attach_lifetime(app);
    g_assert_true(g_application_register(G_APPLICATION(gtk_app), NULL, NULL));
    lmme_window_build(app);
    g_assert_true(lmme_window_open_workspace_path(app, root));

    *out_gtk_app = gtk_app;
    *out_root = g_strdup(root);
    return app;
}

static void
test_full_app_teardown(LmmeApp *app, GtkApplication *gtk_app)
{
    if (!app->scheduling_blocked) {
        app->scheduling_blocked = TRUE;
    }
    lmme_app_test_cancel_pending_work(app);
    lmme_app_test_destroy_runtime_ui(app);
    app->gtk_app = NULL;
    g_clear_object(&gtk_app);
    lmme_app_free(app);
}

static void
on_view_weak_notify(gpointer data, GObject *where_the_object_was)
{
    ViewDestroyTracker *tracker = data;
    (void)where_the_object_was;
    tracker->view_destroy_notified = TRUE;
    tracker->document_alive_on_view_destroy = tracker->doc != NULL;
}

static void
test_command_refresh_deduplicates(void)
{
    GtkApplication *gtk_app = NULL;
    LmmeApp *app = test_app_with_gtk_application(&gtk_app);
    guint first_id = 0;

    lmme_command_actions_test_reset_refresh_count();
    lmme_command_actions_request_refresh(app);
    first_id = app->command_refresh_source_id;
    g_assert_cmpuint(first_id, !=, 0);

    lmme_command_actions_request_refresh(app);
    g_assert_cmpuint(app->command_refresh_source_id, ==, first_id);

    lmme_command_actions_cancel_refresh(app);
    test_app_fixture_teardown(app, gtk_app);
}

static void
test_command_refresh_cancel_before_dispatch(void)
{
    GtkApplication *gtk_app = NULL;
    LmmeApp *app = test_app_with_gtk_application(&gtk_app);

    lmme_command_actions_test_reset_refresh_count();
    lmme_command_actions_request_refresh(app);
    g_assert_cmpuint(app->command_refresh_source_id, !=, 0);

    lmme_command_actions_cancel_refresh(app);
    g_assert_cmpuint(app->command_refresh_source_id, ==, 0);

    g_assert_true(drain_main_context(64));
    g_assert_cmpuint(lmme_command_actions_test_refresh_count(), ==, 0);

    test_app_fixture_teardown(app, gtk_app);
}

static void
test_command_refresh_dispatch_clears_source(void)
{
    GtkApplication *gtk_app = NULL;
    LmmeApp *app = test_app_with_gtk_application(&gtk_app);
    guint second_id = 0;

    lmme_command_actions_test_reset_refresh_count();
    lmme_command_actions_register(app);
    lmme_command_actions_test_reset_refresh_count();

    lmme_command_actions_request_refresh(app);
    g_assert_cmpuint(app->command_refresh_source_id, !=, 0);

    g_assert_true(drain_main_context(64));
    g_assert_cmpuint(app->command_refresh_source_id, ==, 0);
    g_assert_cmpuint(lmme_command_actions_test_refresh_count(), ==, 1);

    lmme_command_actions_request_refresh(app);
    second_id = app->command_refresh_source_id;
    g_assert_cmpuint(second_id, !=, 0);

    lmme_command_actions_cancel_refresh(app);
    test_app_fixture_teardown(app, gtk_app);
}

static void
test_command_refresh_per_app_state(void)
{
    GtkApplication *gtk_app_a = NULL;
    GtkApplication *gtk_app_b = NULL;
    LmmeApp *app_a = test_app_with_gtk_application(&gtk_app_a);
    LmmeApp *app_b = test_app_with_gtk_application(&gtk_app_b);
    guint id_b = 0;

    lmme_command_actions_request_refresh(app_a);
    lmme_command_actions_request_refresh(app_b);
    g_assert_cmpuint(app_a->command_refresh_source_id, !=, 0);
    g_assert_cmpuint(app_b->command_refresh_source_id, !=, 0);
    g_assert_cmpuint(app_a->command_refresh_source_id, !=, app_b->command_refresh_source_id);

    lmme_command_actions_cancel_refresh(app_a);
    g_assert_cmpuint(app_a->command_refresh_source_id, ==, 0);
    g_assert_cmpuint(app_b->command_refresh_source_id, !=, 0);

    id_b = app_b->command_refresh_source_id;
    lmme_command_actions_cancel_refresh(app_b);
    g_assert_cmpuint(app_b->command_refresh_source_id, ==, 0);
    (void)id_b;

    test_app_fixture_teardown(app_a, gtk_app_a);
    test_app_fixture_teardown(app_b, gtk_app_b);
}

static void
test_teardown_window_before_app_state(void)
{
    GtkApplication *gtk_app = NULL;
    g_autofree char *root = NULL;
    g_autofree char *note_path = NULL;
    LmmeApp *app = test_app_with_window(&gtk_app, &root);
    ViewDestroyTracker tracker = {0};
    LmmeDocument *doc = NULL;

    note_path = g_build_filename(root, "note.md", NULL);
    g_assert_true(g_file_set_contents(note_path, "hello", -1, NULL));
    g_assert_true(lmme_tabs_open_file(app, note_path, NULL));
    g_assert_cmpuint(app->documents->len, ==, 1);

    doc = g_ptr_array_index(app->documents, 0);
    tracker.doc = doc;
    g_object_weak_ref(G_OBJECT(doc->scroller), on_view_weak_notify, &tracker);

    g_assert_nonnull(app->window);
    test_full_app_teardown(app, gtk_app);

    g_assert_true(tracker.view_destroy_notified);
    g_assert_true(tracker.document_alive_on_view_destroy);
}

static void
test_window_semantic_styling_hooks(void)
{
    GtkApplication *gtk_app = NULL;
    g_autofree char *root = NULL;
    g_autofree char *note_path = NULL;
    LmmeApp *app = test_app_with_window(&gtk_app, &root);
    LmmeDocument *doc = NULL;

    g_assert_true(gtk_widget_has_css_class(app->window, "lmme-window"));
    g_assert_true(gtk_widget_has_css_class(app->root_box, "app-root"));
    g_assert_true(gtk_widget_has_css_class(app->menu_bar, "app-menu"));
    g_assert_true(gtk_widget_has_css_class(app->main_paned, "main-paned"));
    g_assert_true(gtk_widget_has_css_class(app->tree_view, "file-tree"));
    g_assert_true(gtk_widget_has_css_class(app->right_box, "content-area"));
    g_assert_true(gtk_widget_has_css_class(app->breadcrumbs_label, "breadcrumbs"));
    g_assert_true(gtk_widget_has_css_class(app->notebook, "editor-tabs"));
    g_assert_true(gtk_label_get_single_line_mode(GTK_LABEL(app->breadcrumbs_label)));
    g_assert_cmpint(gtk_label_get_ellipsize(GTK_LABEL(app->breadcrumbs_label)),
                    ==,
                    PANGO_ELLIPSIZE_MIDDLE);

    note_path = g_build_filename(root, "semantic-hooks.md", NULL);
    g_assert_true(g_file_set_contents(note_path, "# Semantic hooks\n", -1, NULL));
    g_assert_true(lmme_tabs_open_file(app, note_path, NULL));
    doc = lmme_tabs_get_active(app);
    g_assert_nonnull(doc);
    g_assert_true(gtk_widget_has_css_class(doc->tab_box, "tab-label"));
    g_assert_true(gtk_widget_has_css_class(doc->title_label, "tab-title"));

    test_full_app_teardown(app, gtk_app);
}

static void
test_teardown_cancels_pending_sources(void)
{
    GtkApplication *gtk_app = NULL;
    g_autofree char *root = NULL;
    g_autofree char *note_path = NULL;
    LmmeApp *app = test_app_with_window(&gtk_app, &root);
    LmmeDocument *doc = NULL;

    note_path = g_build_filename(root, "note.md", NULL);
    g_assert_true(g_file_set_contents(note_path, "hello", -1, NULL));
    g_assert_true(lmme_tabs_open_file(app, note_path, NULL));
    doc = g_ptr_array_index(app->documents, 0);

    lmme_command_actions_request_refresh(app);
    lmme_window_schedule_preview(app);
    lmme_document_mark_stats_dirty(doc);
    lmme_document_request_stats_update(doc);
    lmme_document_schedule_autosave(doc);
    lmme_document_schedule_recovery(doc);
    doc->disk_state = LMME_DISK_STATE_EXTERNAL_CHANGED;
    lmme_external_conflict_request(doc);

    g_assert_cmpuint(app->command_refresh_source_id, !=, 0);
    g_assert_cmpuint(app->preview_timeout_id, !=, 0);
    g_assert_cmpuint(doc->stats_timeout_id, !=, 0);
    g_assert_cmpuint(doc->autosave_id, !=, 0);
    g_assert_cmpuint(doc->recovery_id, !=, 0);
    g_assert_cmpuint(doc->external_conflict_source_id, !=, 0);

    app->scheduling_blocked = TRUE;
    lmme_app_test_cancel_pending_work(app);
    g_assert_cmpuint(app->command_refresh_source_id, ==, 0);
    g_assert_cmpuint(app->preview_timeout_id, ==, 0);
    g_assert_cmpuint(doc->autosave_id, ==, 0);
    g_assert_cmpuint(doc->recovery_id, ==, 0);
    g_assert_cmpuint(doc->stats_timeout_id, ==, 0);
    g_assert_cmpuint(doc->external_conflict_source_id, ==, 0);

    lmme_app_test_destroy_runtime_ui(app);
    app->gtk_app = NULL;
    g_clear_object(&gtk_app);
    lmme_app_free(app);
    g_assert_true(drain_main_context(64));
}

static void
test_teardown_repeated(void)
{
    guint cycle = 0;

    for (cycle = 0; cycle < 100; cycle++) {
        GtkApplication *gtk_app = NULL;
        g_autofree char *root = NULL;
        g_autofree char *note_path = NULL;
        LmmeApp *app = test_app_with_window(&gtk_app, &root);

        note_path = g_build_filename(root, "note.md", NULL);
        g_assert_true(g_file_set_contents(note_path, "hello", -1, NULL));
        g_assert_true(lmme_tabs_open_file(app, note_path, NULL));
        lmme_command_actions_request_refresh(app);
        lmme_window_schedule_preview(app);
        test_full_app_teardown(app, gtk_app);
        g_assert_true(drain_main_context(16));
    }
}

static gboolean
prepare_close_cancel(LmmeDocument *doc, gpointer user_data)
{
    (void)doc;
    (void)user_data;
    return FALSE;
}

static void
test_teardown_shutdown_prepare_cancelled(void)
{
    GtkApplication *gtk_app = NULL;
    g_autofree char *root = NULL;
    g_autofree char *note_path = NULL;
    LmmeApp *app = test_app_with_window(&gtk_app, &root);
    LmmeDocument *doc = NULL;

    note_path = g_build_filename(root, "note.md", NULL);
    g_assert_true(g_file_set_contents(note_path, "hello", -1, NULL));
    g_assert_true(lmme_tabs_open_file(app, note_path, NULL));
    doc = g_ptr_array_index(app->documents, 0);
    doc->modified = TRUE;

    lmme_tabs_test_set_prepare_close_override(prepare_close_cancel, NULL);
    g_assert_false(lmme_app_request_shutdown(app));
    lmme_tabs_test_clear_prepare_close_override();

    g_assert_false(app->shutdown_in_progress);
    g_assert_false(app->scheduling_blocked);
    g_assert_nonnull(app->window);

    lmme_document_schedule_autosave(doc);
    lmme_document_schedule_recovery(doc);
    lmme_command_actions_request_refresh(app);
    g_assert_cmpuint(doc->autosave_id, !=, 0);
    g_assert_cmpuint(doc->recovery_id, !=, 0);
    g_assert_cmpuint(app->command_refresh_source_id, !=, 0);

    test_full_app_teardown(app, gtk_app);
}

static void
test_session_serialization(void)
{
    GtkApplication *gtk_app = NULL;
    g_autofree char *root = NULL;
    g_autofree char *note_a = NULL;
    g_autofree char *note_b = NULL;
    LmmeApp *app = test_app_with_window(&gtk_app, &root);
    LmmeConfig reloaded = {0};
    g_autoptr(GError) error = NULL;

    note_a = g_build_filename(root, "a.md", NULL);
    note_b = g_build_filename(root, "b.md", NULL);
    g_assert_true(g_file_set_contents(note_a, "a", -1, NULL));
    g_assert_true(g_file_set_contents(note_b, "b", -1, NULL));
    g_assert_true(lmme_tabs_open_file(app, note_a, NULL));
    g_assert_true(lmme_tabs_open_file(app, note_b, NULL));

    lmme_config_set_last_workspace(&app->config, root);
    app->config.restore_tabs = TRUE;
    lmme_app_test_save_session(app);
    g_assert_true(lmme_config_load(&reloaded, app->config_path, &error));
    g_assert_cmpstr(reloaded.last_workspace, ==, root);
    g_assert_nonnull(reloaded.open_tabs);
    g_assert_cmpuint(reloaded.open_tabs->len, ==, 2);

    lmme_config_clear(&reloaded);
    test_full_app_teardown(app, gtk_app);
}

static gboolean
session_idle_quit(gpointer user_data)
{
    g_application_quit(G_APPLICATION(user_data));
    return G_SOURCE_REMOVE;
}

static void
on_session_test_shutdown(GApplication *application, gpointer user_data)
{
    LmmeApp *app = user_data;
    (void)application;
    lmme_app_test_save_session(app);
}

static void
on_session_test_activate(GtkApplication *gtk_app, gpointer user_data)
{
    SessionRunContext *ctx = user_data;

    ctx->app->gtk_app = gtk_app;
    lmme_window_build(ctx->app);
    lmme_config_set_last_workspace(&ctx->app->config, ctx->workspace_root);
    g_assert_true(lmme_window_open_workspace_path(ctx->app, ctx->workspace_root));
    g_assert_true(lmme_tabs_open_file(ctx->app, ctx->note_a, NULL));
    g_assert_true(lmme_tabs_open_file(ctx->app, ctx->note_b, NULL));
    g_idle_add(session_idle_quit, gtk_app);
}

static void
test_session_shutdown_signal(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-app-session-XXXXXX", NULL);
    g_autofree char *recovery_dir = g_build_filename(root, "recovery", NULL);
    g_autofree char *config_path = g_build_filename(root, "config.ini", NULL);
    g_autofree char *note_a = g_build_filename(root, "a.md", NULL);
    g_autofree char *note_b = g_build_filename(root, "b.md", NULL);
    GtkApplication *gtk_app = gtk_application_new("org.lmme.TestLifecycleSession",
                                                    G_APPLICATION_NON_UNIQUE);
    LmmeApp *app = g_new0(LmmeApp, 1);
    SessionRunContext ctx = {.app = app,
                           .workspace_root = root,
                           .note_a = note_a,
                           .note_b = note_b};
    LmmeConfig reloaded = {0};
    g_autoptr(GError) error = NULL;

    g_mkdir_with_parents(recovery_dir, 0700);
    g_assert_true(g_file_set_contents(note_a, "a", -1, NULL));
    g_assert_true(g_file_set_contents(note_b, "b", -1, NULL));

    app->documents = g_ptr_array_new();
    app->config_path = g_strdup(config_path);
    app->recovery_store = lmme_recovery_store_new(recovery_dir);
    app->config.restore_tabs = TRUE;
    lmme_config_init_defaults(&app->config);
    g_free(app->config_path);
    app->config_path = g_strdup(config_path);

    g_signal_connect(gtk_app, "activate", G_CALLBACK(on_session_test_activate), &ctx);
    g_signal_connect(gtk_app, "shutdown", G_CALLBACK(on_session_test_shutdown), app);

    g_application_run(G_APPLICATION(gtk_app), 0, NULL);

    g_assert_true(lmme_config_load(&reloaded, config_path, &error));
    g_assert_cmpstr(reloaded.last_workspace, ==, root);
    g_assert_nonnull(reloaded.open_tabs);
    g_assert_cmpuint(reloaded.open_tabs->len, ==, 2);

    lmme_config_clear(&reloaded);
    app->scheduling_blocked = TRUE;
    lmme_app_test_cancel_pending_work(app);
    lmme_app_test_destroy_runtime_ui(app);
    app->gtk_app = NULL;
    g_clear_object(&gtk_app);
    lmme_app_free(app);
}

typedef struct {
    LmmeAppLifetime *lifetime;
    gboolean finished;
} ImageAsyncTestState;

static void
on_image_async_png_saved(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    ImageAsyncTestState *state = user_data;
    g_autoptr(GError) error = NULL;

    (void)source_object;
    state->finished = lmme_image_texture_save_png_finish(GDK_TEXTURE(source_object), result, &error);
    lmme_app_lifetime_unref(state->lifetime);
    g_free(state);
}

static void
test_image_async_survives_app_teardown(void)
{
    GtkApplication *gtk_app = NULL;
    g_autofree char *root = NULL;
    g_autofree char *note_path = NULL;
    g_autofree char *png_path = NULL;
    LmmeApp *app = test_app_with_window(&gtk_app, &root);
    const guint8 pixels[] = {
        255, 0, 0, 255,
        0, 255, 0, 255,
        0, 0, 255, 255,
        255, 255, 255, 255,
    };
    g_autoptr(GBytes) bytes = g_bytes_new_static(pixels, sizeof(pixels));
    g_autoptr(GdkTexture) texture = GDK_TEXTURE(gdk_memory_texture_new(2,
                                                                       2,
                                                                       GDK_MEMORY_R8G8B8A8,
                                                                       bytes,
                                                                       8));
    ImageAsyncTestState *state = g_new0(ImageAsyncTestState, 1);

    note_path = g_build_filename(root, "note.md", NULL);
    png_path = g_build_filename(root, "img", "pending.png", NULL);
    g_mkdir_with_parents(g_path_get_dirname(png_path), 0700);
    g_assert_true(g_file_set_contents(note_path, "hello", -1, NULL));
    g_assert_true(lmme_tabs_open_file(app, note_path, NULL));

    state->lifetime = lmme_app_lifetime_ref(app);
    lmme_image_texture_save_png_async(texture, png_path, NULL, on_image_async_png_saved, state);

    test_full_app_teardown(app, gtk_app);
    g_assert_true(drain_main_context(256));
}

int
main(int argc, char **argv)
{
    gtk_init();

    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/app/command-refresh/deduplicates", test_command_refresh_deduplicates);
    g_test_add_func("/app/command-refresh/cancel-before-dispatch",
                    test_command_refresh_cancel_before_dispatch);
    g_test_add_func("/app/command-refresh/dispatch-clears-source",
                    test_command_refresh_dispatch_clears_source);
    g_test_add_func("/app/command-refresh/per-app-state", test_command_refresh_per_app_state);
    g_test_add_func("/app/teardown/window-before-app-state", test_teardown_window_before_app_state);
    g_test_add_func("/app/window/semantic-styling-hooks", test_window_semantic_styling_hooks);
    g_test_add_func("/app/teardown/cancels-pending-sources", test_teardown_cancels_pending_sources);
    g_test_add_func("/app/teardown/repeated", test_teardown_repeated);
    g_test_add_func("/app/teardown/shutdown-prepare-cancelled",
                    test_teardown_shutdown_prepare_cancelled);
    g_test_add_func("/app/session/serialization", test_session_serialization);
    g_test_add_func("/app/session/shutdown-signal", test_session_shutdown_signal);
    g_test_add_func("/app/image-async/survives-app-teardown", test_image_async_survives_app_teardown);
    return g_test_run();
}
