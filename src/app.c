#include "app.h"

#include "dialogs.h"
#include "tabs.h"
#include "window.h"

static void
on_activate(GtkApplication *gtk_app, gpointer user_data)
{
    LmmeApp *app = user_data;
    app->gtk_app = gtk_app;

    if (app->window == NULL) {
        lmme_window_build(app);
    }

    gtk_window_present(GTK_WINDOW(app->window));
}

static void
on_shutdown(GApplication *application, gpointer user_data)
{
    LmmeApp *app = user_data;
    (void)application;

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
    app->config.focus_mode = app->focus_mode;
    if (app->main_paned != NULL) {
        app->config.sidebar_width = gtk_paned_get_position(GTK_PANED(app->main_paned));
    }
    if (app->editor_preview_paned != NULL) {
        int width = gtk_widget_get_width(app->editor_preview_paned);
        int position = gtk_paned_get_position(GTK_PANED(app->editor_preview_paned));
        if (width > 0 && position >= 0 && position <= width) {
            app->config.preview_split_ratio = (double)(width - position) / (double)width;
        }
    }

    g_autoptr(GPtrArray) paths = lmme_tabs_open_paths(app);
    lmme_config_set_open_tabs(&app->config, paths);

    g_autoptr(GError) error = NULL;
    if (!lmme_config_save(&app->config, app->config_path, &error)) {
        g_warning("Could not save config: %s", error != NULL ? error->message : "unknown error");
    }
}

static LmmeApp *
app_new(GtkApplication *gtk_app)
{
    LmmeApp *app = g_new0(LmmeApp, 1);
    g_autoptr(GError) error = NULL;

    app->gtk_app = gtk_app;
    app->config_path = lmme_config_default_path();
    app->documents = g_ptr_array_new();

    if (!lmme_config_load(&app->config, app->config_path, &error) && error != NULL) {
        g_warning("Could not load config: %s", error->message);
    } else if (error != NULL) {
        g_warning("Invalid config ignored: %s", error->message);
    }

    app->preview_enabled = app->config.preview_enabled;
    app->focus_mode = app->config.focus_mode;

    return app;
}

void
lmme_app_free(LmmeApp *app)
{
    if (app == NULL) {
        return;
    }

    if (app->preview_timeout_id != 0) {
        g_source_remove(app->preview_timeout_id);
    }

    if (app->documents != NULL) {
        while (app->documents->len > 0) {
            LmmeDocument *doc = g_ptr_array_index(app->documents, app->documents->len - 1);
            g_ptr_array_remove_index(app->documents, app->documents->len - 1);
            lmme_document_free(doc);
        }
        g_clear_pointer(&app->documents, g_ptr_array_unref);
    }
    g_clear_pointer(&app->workspace, lmme_workspace_free);
    g_clear_pointer(&app->selected_path, g_free);
    g_clear_pointer(&app->config_path, g_free);
    lmme_config_clear(&app->config);
    g_free(app);
}

int
lmme_app_run(int argc, char **argv)
{
    GtkApplication *gtk_app = gtk_application_new("org.lmme.LowMemoryMarkdownEditor", G_APPLICATION_DEFAULT_FLAGS);
    LmmeApp *app = app_new(gtk_app);
    int status = 0;

    g_signal_connect(gtk_app, "activate", G_CALLBACK(on_activate), app);
    g_signal_connect(gtk_app, "shutdown", G_CALLBACK(on_shutdown), app);

    status = g_application_run(G_APPLICATION(gtk_app), argc, argv);

    lmme_app_free(app);
    g_object_unref(gtk_app);
    return status;
}
