#include "app/app.h"
#include "app/app_test.h"

#include "command/command_actions.h"
#include "document/document.h"
#include "document/document_autosave.h"
#include "document/recovery.h"
#include "document/tabs.h"
#include "infra/config.h"
#include "infra/dialogs.h"
#include "ui/window.h"
#include "workspace/workspace.h"

struct _LmmeAppLifetime {
    gatomicrefcount ref_count;
    LmmeApp *app;
};

static LmmeAppLifetime *
lmme_app_lifetime_new(LmmeApp *app)
{
    LmmeAppLifetime *lifetime = g_new(LmmeAppLifetime, 1);

    g_atomic_ref_count_init(&lifetime->ref_count);
    lifetime->app = app;
    return lifetime;
}

LmmeAppLifetime *
lmme_app_lifetime_ref(LmmeApp *app)
{
    if (app == NULL || app->lifetime == NULL) {
        return NULL;
    }
    g_atomic_ref_count_inc(&app->lifetime->ref_count);
    return app->lifetime;
}

void
lmme_app_lifetime_unref(LmmeAppLifetime *lifetime)
{
    if (lifetime != NULL && g_atomic_ref_count_dec(&lifetime->ref_count)) {
        g_free(lifetime);
    }
}

LmmeApp *
lmme_app_lifetime_get_app(LmmeAppLifetime *lifetime)
{
    return lifetime != NULL ? lifetime->app : NULL;
}

static void
lmme_app_invalidate_lifetime(LmmeApp *app)
{
    if (app != NULL && app->lifetime != NULL) {
        app->lifetime->app = NULL;
    }
}

static void lmme_app_save_session(LmmeApp *app);
static void lmme_app_cancel_pending_work(LmmeApp *app);
static void lmme_app_destroy_runtime_ui(LmmeApp *app);

static void
on_activate(GApplication *application, gpointer user_data)
{
    GtkApplication *gtk_app = GTK_APPLICATION(application);
    LmmeApp *app = user_data;
    app->gtk_app = gtk_app;

    if (app->window == NULL) {
        lmme_window_build(app);
    }

    gtk_window_present(GTK_WINDOW(app->window));
}

void
lmme_path_context_clear(LmmePathContext *context)
{
    if (context == NULL) {
        return;
    }
    g_clear_pointer(&context->path, g_free);
    context->kind = LMME_FILE_KIND_OTHER;
    context->empty_area = FALSE;
}

void
lmme_path_context_set(LmmePathContext *context,
                      const char *path,
                      LmmeFileKind kind,
                      gboolean empty_area)
{
    if (context == NULL) {
        return;
    }
    lmme_path_context_clear(context);
    context->empty_area = empty_area;
    if (!empty_area && path != NULL) {
        context->path = g_strdup(path);
        context->kind = kind;
    }
}

void
lmme_app_clear_widget_refs(LmmeApp *app)
{
    if (app == NULL) {
        return;
    }

    app->window = NULL;
    app->root_box = NULL;
    app->menu_bar = NULL;
    app->toolbar = NULL;
    app->notebook = NULL;
    app->tree_view = NULL;
    app->status_label = NULL;
    app->breadcrumbs_label = NULL;
    app->main_paned = NULL;
    app->sidebar = NULL;
    app->right_box = NULL;
    app->search_bar = NULL;
    app->find_entry = NULL;
    app->replace_entry = NULL;
    app->replace_button = NULL;
}

static void
lmme_app_save_session(LmmeApp *app)
{
    if (app == NULL) {
        return;
    }

    if (app->window != NULL) {
        int width = 0;
        int height = 0;
        gtk_window_get_default_size(GTK_WINDOW(app->window), &width, &height);
        if (width > 0) {
            app->config.window_width = width;
        }
        if (height > 0) {
            app->config.window_height = height;
        }
        app->config.window_maximized = gtk_window_is_maximized(GTK_WINDOW(app->window));
    }

    app->config.preview_enabled = app->preview_enabled;
    if (app->main_paned != NULL) {
        app->config.sidebar_width = gtk_paned_get_position(GTK_PANED(app->main_paned));
    }

    g_autoptr(GPtrArray) paths = lmme_tabs_open_paths(app);
    lmme_config_set_open_tabs(&app->config, paths);

    g_autoptr(GError) error = NULL;
    LmmeConfigSaveResult save_result = lmme_config_save(&app->config, app->config_path, &error);

    if (save_result == LMME_CONFIG_SAVE_NOT_COMMITTED) {
        g_warning("Could not save config: %s", error != NULL ? error->message : "unknown error");
    } else if (save_result == LMME_CONFIG_SAVE_COMMITTED_NOT_DURABLE) {
        g_warning("Config was replaced, but durability could not be confirmed.");
    }
}

static void
on_shutdown(GApplication *application, gpointer user_data)
{
    LmmeApp *app = user_data;
    (void)application;

    lmme_app_save_session(app);
}

static void
lmme_app_cancel_pending_work(LmmeApp *app)
{
    if (app == NULL) {
        return;
    }

    lmme_app_invalidate_lifetime(app);
    lmme_command_actions_cancel_refresh(app);
    if (app->preview_timeout_id != 0) {
        g_source_remove(app->preview_timeout_id);
        app->preview_timeout_id = 0;
    }
    if (app->documents != NULL) {
        for (guint i = 0; i < app->documents->len; i++) {
            lmme_document_cancel_pending_work(g_ptr_array_index(app->documents, i));
        }
    }
}

static void
lmme_app_destroy_runtime_ui(LmmeApp *app)
{
    GtkWidget *window = NULL;

    if (app == NULL) {
        return;
    }

    window = app->window;
    if (window != NULL) {
        gtk_window_destroy(GTK_WINDOW(window));
    }
    lmme_app_clear_widget_refs(app);
}

static LmmeApp *
app_new(GtkApplication *gtk_app)
{
    LmmeApp *app = g_new0(LmmeApp, 1);
    g_autoptr(GError) error = NULL;

    app->gtk_app = gtk_app;
    app->config_path = lmme_config_default_path();
    app->documents = g_ptr_array_new();
    app->recovery_store = lmme_recovery_store_new_default();
    app->next_document_id = 1;

    if (!lmme_config_load(&app->config, app->config_path, &error) && error != NULL) {
        g_warning("Could not load config: %s", error->message);
    } else if (error != NULL) {
        g_warning("Invalid config ignored: %s", error->message);
    }

    app->preview_enabled = app->config.preview_enabled;
    app->focus_mode = FALSE;
    app->lifetime = lmme_app_lifetime_new(app);

    return app;
}

gboolean
lmme_app_request_shutdown(LmmeApp *app)
{
    if (app == NULL) {
        return FALSE;
    }
    if (app->shutdown_in_progress) {
        return TRUE;
    }

    app->shutdown_in_progress = TRUE;

    if (!lmme_tabs_prepare_close_all(app)) {
        app->shutdown_in_progress = FALSE;
        return FALSE;
    }

    g_autoptr(GError) commit_error = NULL;
    if (!lmme_tabs_commit_pending_dispositions(app, &commit_error)) {
        app->shutdown_in_progress = FALSE;
        if (app->window != NULL) {
            lmme_dialog_error(GTK_WINDOW(app->window),
                              "Could not finish closing documents.",
                              commit_error != NULL ? commit_error->message : NULL);
        }
        return FALSE;
    }

    app->scheduling_blocked = TRUE;
    lmme_app_cancel_pending_work(app);
    g_application_quit(G_APPLICATION(app->gtk_app));
    return TRUE;
}

void
lmme_app_free(LmmeApp *app)
{
    if (app == NULL) {
        return;
    }

    lmme_app_cancel_pending_work(app);

    if (app->documents != NULL) {
        while (app->documents->len > 0) {
            LmmeDocument *doc = g_ptr_array_index(app->documents, app->documents->len - 1);
            g_ptr_array_remove_index(app->documents, app->documents->len - 1);
            lmme_document_free(doc);
        }
        g_clear_pointer(&app->documents, g_ptr_array_unref);
    }
    g_clear_pointer(&app->workspace, lmme_workspace_free);
    g_clear_pointer(&app->recovery_store, lmme_recovery_store_free);
    lmme_path_context_clear(&app->selection);
    lmme_path_context_clear(&app->tree_context);
    g_clear_pointer(&app->config_path, g_free);
    lmme_config_clear(&app->config);
    if (app->lifetime != NULL) {
        LmmeAppLifetime *lifetime = app->lifetime;
        app->lifetime = NULL;
        lifetime->app = NULL;
        lmme_app_lifetime_unref(lifetime);
    }
    g_free(app);
}

int
lmme_app_run(int argc, char **argv)
{
    /* gtk_app is the only strong owner of GtkApplication; app->gtk_app is borrowed. */
    GtkApplication *gtk_app = gtk_application_new("org.lmme.LowMemoryMarkdownEditor", G_APPLICATION_DEFAULT_FLAGS);
    LmmeApp *app = app_new(gtk_app);
    int status = 0;

    g_signal_connect(gtk_app, "activate", G_CALLBACK(on_activate), app);
    g_signal_connect(gtk_app, "shutdown", G_CALLBACK(on_shutdown), app);

    status = g_application_run(G_APPLICATION(gtk_app), argc, argv);

    if (!app->scheduling_blocked) {
        app->scheduling_blocked = TRUE;
    }
    lmme_app_cancel_pending_work(app);
    lmme_app_destroy_runtime_ui(app);

    app->gtk_app = NULL;
    g_clear_object(&gtk_app);

    lmme_app_free(app);
    return status;
}

#ifdef LMME_TESTING

void
lmme_app_test_save_session(LmmeApp *app)
{
    lmme_app_save_session(app);
}

void
lmme_app_test_cancel_pending_work(LmmeApp *app)
{
    lmme_app_cancel_pending_work(app);
}

void
lmme_app_test_destroy_runtime_ui(LmmeApp *app)
{
    lmme_app_destroy_runtime_ui(app);
}

gboolean
lmme_app_test_drain_main_context(guint max_iterations)
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

void
lmme_app_test_attach_lifetime(LmmeApp *app)
{
    if (app != NULL && app->lifetime == NULL) {
        app->lifetime = lmme_app_lifetime_new(app);
    }
}

#endif
