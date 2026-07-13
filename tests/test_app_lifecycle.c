#include <glib.h>
#include <gtk/gtk.h>

#include "app/app.h"
#include "command/command_actions.h"
#include "command/command_actions_test.h"

static gboolean
drain_main_context(guint max_iterations)
{
    guint iterations = 0;

    while (iterations < max_iterations) {
        if (!g_main_context_pending(NULL)) {
            return TRUE;
        }
        if (!g_main_context_iteration(NULL, FALSE)) {
            return TRUE;
        }
        iterations++;
    }
    return FALSE;
}

static LmmeApp *
test_app_with_gtk_application(GtkApplication **out_gtk_app)
{
    GtkApplication *gtk_app = gtk_application_new("org.lmme.TestLifecycle",
                                                  G_APPLICATION_NON_UNIQUE);
    LmmeApp *app = g_new0(LmmeApp, 1);

    app->gtk_app = gtk_app;
    app->documents = g_ptr_array_new();
    *out_gtk_app = gtk_app;
    return app;
}

static void
test_app_fixture_teardown(LmmeApp *app, GtkApplication *gtk_app)
{
    lmme_command_actions_cancel_refresh(app);
    if (app->documents != NULL) {
        g_ptr_array_unref(app->documents);
    }
    app->gtk_app = NULL;
    g_clear_object(&gtk_app);
    g_free(app);
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
    return g_test_run();
}
