#include "document/tabs.h"

#include "app/app.h"
#include "document/file_monitor.h"
#include "infra/dialogs.h"
#include "infra/util.h"
#include "ui/tab_context_menu.h"
#include "ui/window.h"
#include "workspace/workspace.h"

static void
on_close_tab_clicked(GtkButton *button, gpointer user_data)
{
    LmmeDocument *doc = user_data;
    GtkNotebook *notebook = GTK_NOTEBOOK(doc->app->notebook);
    int page = gtk_notebook_page_num(notebook, doc->scroller);

    (void)button;
    if (page >= 0) {
        gtk_notebook_set_current_page(notebook, page);
        lmme_tabs_close_active(doc->app);
    }
}

static void
update_tab_title(LmmeDocument *doc)
{
    g_autofree char *base = NULL;
    g_autofree char *title = NULL;

    if (doc == NULL || doc->title_label == NULL) {
        return;
    }

    base = g_path_get_basename(doc->path);
    title = doc->modified ? g_strdup_printf("● %s", base) : g_strdup(base);
    gtk_label_set_text(GTK_LABEL(doc->title_label), title);
}

static GtkWidget *
create_tab_label(LmmeDocument *doc)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    GtkWidget *label = gtk_label_new("");
    GtkWidget *close = gtk_button_new_from_icon_name("window-close-symbolic");

    gtk_widget_add_css_class(box, "tab-label");
    gtk_widget_add_css_class(close, "tab-close");
    gtk_button_set_has_frame(GTK_BUTTON(close), FALSE);
    gtk_widget_set_size_request(close, 18, 18);
    gtk_widget_set_tooltip_text(close, "Close");
    gtk_box_append(GTK_BOX(box), label);
    gtk_box_append(GTK_BOX(box), close);
    g_signal_connect(close, "clicked", G_CALLBACK(on_close_tab_clicked), doc);

    doc->title_label = label;
    doc->tab_box = box;
    update_tab_title(doc);
    lmme_tab_context_menu_attach(doc, box);
    return box;
}

LmmePreviewApplyResult
lmme_tabs_set_preview_visible(LmmeApp *app, gboolean visible)
{
    LmmeDocument *active = app != NULL ? lmme_tabs_get_active(app) : NULL;
    LmmePreviewApplyResult active_result = LMME_PREVIEW_APPLY_OK;

    if (app == NULL || app->documents == NULL) {
        return LMME_PREVIEW_APPLY_FAILED;
    }

    for (guint i = 0; i < app->documents->len; i++) {
        LmmeDocument *doc = g_ptr_array_index(app->documents, i);
        LmmePreviewApplyResult result = lmme_document_set_preview_visible(doc, visible);
        if (doc == active) {
            active_result = result;
        }
    }

    return active_result;
}

LmmeDocument *
lmme_tabs_find_by_path(LmmeApp *app, const char *path)
{
    g_autofree char *canon = NULL;

    if (app == NULL || app->documents == NULL || path == NULL) {
        return NULL;
    }

    canon = g_canonicalize_filename(path, NULL);
    for (guint i = 0; i < app->documents->len; i++) {
        LmmeDocument *doc = g_ptr_array_index(app->documents, i);
        if (g_strcmp0(doc->path, canon) == 0) {
            return doc;
        }
    }

    return NULL;
}

LmmeDocument *
lmme_tabs_get_active(LmmeApp *app)
{
    int page = -1;
    GtkWidget *child = NULL;

    if (app == NULL || app->notebook == NULL || app->documents == NULL) {
        return NULL;
    }

    page = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook));
    child = page >= 0 ? gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), page) : NULL;
    if (child == NULL) {
        return NULL;
    }

    for (guint i = 0; i < app->documents->len; i++) {
        LmmeDocument *doc = g_ptr_array_index(app->documents, i);
        if (doc->scroller == child) {
            return doc;
        }
    }

    return NULL;
}

static gboolean
open_file_contents(const char *path, char **contents, GError **error)
{
    gsize length = 0;

    if (!g_file_get_contents(path, contents, &length, error)) {
        return FALSE;
    }

    if (!g_utf8_validate(*contents, (gssize)length, NULL)) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "This file is not valid UTF-8.");
        g_clear_pointer(contents, g_free);
        return FALSE;
    }

    return TRUE;
}

static void
add_document_to_notebook(LmmeApp *app, LmmeDocument *doc)
{
    GtkWidget *label = create_tab_label(doc);
    int page = gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), doc->scroller, label);

    g_ptr_array_add(app->documents, doc);
    gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(app->notebook), doc->scroller, TRUE);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), page);
    gtk_widget_grab_focus(doc->source_view);
    lmme_window_update_status(app);
    lmme_window_schedule_preview(app);
}

gboolean
lmme_tabs_open_file(LmmeApp *app, const char *path, GError **error)
{
    LmmeDocument *existing = lmme_tabs_find_by_path(app, path);
    g_autofree char *contents = NULL;

    if (existing != NULL) {
        int page = gtk_notebook_page_num(GTK_NOTEBOOK(app->notebook), existing->scroller);
        gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), page);
        return TRUE;
    }
    if (!open_file_contents(path, &contents, error)) {
        return FALSE;
    }

    add_document_to_notebook(app, lmme_document_new(app, path, contents, NULL));
    return TRUE;
}

gboolean
lmme_tabs_open_recovery_file(LmmeApp *app, const char *path, const char *title, GError **error)
{
    g_autofree char *contents = NULL;

    if (!open_file_contents(path, &contents, error)) {
        return FALSE;
    }

    add_document_to_notebook(app, lmme_document_new(app, path, contents, title));
    return TRUE;
}

gboolean
lmme_tabs_save_document(LmmeDocument *doc, GError **error)
{
    return lmme_document_save(doc, error);
}

gboolean
lmme_tabs_save_active(LmmeApp *app, GError **error)
{
    return lmme_tabs_save_document(lmme_tabs_get_active(app), error);
}

static void
remove_document(LmmeApp *app, LmmeDocument *doc)
{
    if (app->tab_context_document == doc) {
        app->tab_context_document = NULL;
    }

    for (guint i = 0; i < app->documents->len; i++) {
        if (g_ptr_array_index(app->documents, i) == doc) {
            g_ptr_array_remove_index(app->documents, i);
            break;
        }
    }
    lmme_document_free(doc);
}

void
lmme_tabs_close_active(LmmeApp *app)
{
    LmmeDocument *doc = lmme_tabs_get_active(app);
    int page = doc != NULL ? gtk_notebook_page_num(GTK_NOTEBOOK(app->notebook), doc->scroller) : -1;

    if (doc == NULL || page < 0) {
        return;
    }

    if (doc->modified) {
        g_autoptr(GError) error = NULL;
        if (!lmme_document_save(doc, &error) &&
            !lmme_dialog_confirm_close_unsaved(GTK_WINDOW(app->window), doc->relative_path)) {
            return;
        }
    }

    gtk_notebook_remove_page(GTK_NOTEBOOK(app->notebook), page);
    remove_document(app, doc);
    lmme_window_update_status(app);
    lmme_window_schedule_preview(app);
}

gboolean
lmme_tabs_close_document(LmmeApp *app, LmmeDocument *doc)
{
    GtkNotebook *notebook = NULL;
    int page = -1;
    guint before = 0;

    if (app == NULL || doc == NULL || app->notebook == NULL || app->documents == NULL) {
        return FALSE;
    }

    notebook = GTK_NOTEBOOK(app->notebook);
    page = gtk_notebook_page_num(notebook, doc->scroller);
    if (page < 0) {
        return FALSE;
    }

    before = app->documents->len;
    gtk_notebook_set_current_page(notebook, page);
    lmme_tabs_close_active(app);
    return app->documents->len < before;
}

gboolean
lmme_tabs_close_tabs_to_right(LmmeApp *app, LmmeDocument *anchor)
{
    GtkNotebook *notebook = NULL;
    int anchor_page = -1;

    if (app == NULL || anchor == NULL || app->notebook == NULL || app->documents == NULL) {
        return FALSE;
    }

    notebook = GTK_NOTEBOOK(app->notebook);
    anchor_page = gtk_notebook_page_num(notebook, anchor->scroller);
    if (anchor_page < 0) {
        return FALSE;
    }

    while (gtk_notebook_get_n_pages(notebook) > anchor_page + 1) {
        guint before = app->documents->len;
        gtk_notebook_set_current_page(notebook, anchor_page + 1);
        lmme_tabs_close_active(app);
        if (app->documents->len == before) {
            return FALSE;
        }

        anchor_page = gtk_notebook_page_num(notebook, anchor->scroller);
        if (anchor_page < 0) {
            return FALSE;
        }
    }

    gtk_notebook_set_current_page(notebook, anchor_page);
    return TRUE;
}

gboolean
lmme_tabs_close_tabs_to_left(LmmeApp *app, LmmeDocument *anchor)
{
    GtkNotebook *notebook = NULL;
    int anchor_page = -1;

    if (app == NULL || anchor == NULL || app->notebook == NULL || app->documents == NULL) {
        return FALSE;
    }

    notebook = GTK_NOTEBOOK(app->notebook);
    anchor_page = gtk_notebook_page_num(notebook, anchor->scroller);
    if (anchor_page < 0) {
        return FALSE;
    }

    while (anchor_page > 0) {
        guint before = app->documents->len;
        gtk_notebook_set_current_page(notebook, 0);
        lmme_tabs_close_active(app);
        if (app->documents->len == before) {
            return FALSE;
        }

        anchor_page = gtk_notebook_page_num(notebook, anchor->scroller);
        if (anchor_page < 0) {
            return FALSE;
        }
    }

    gtk_notebook_set_current_page(notebook, anchor_page);
    return TRUE;
}

gboolean
lmme_tabs_close_other_tabs(LmmeApp *app, LmmeDocument *anchor)
{
    GtkNotebook *notebook = NULL;
    int anchor_page = -1;

    if (app == NULL || anchor == NULL || app->notebook == NULL || app->documents == NULL) {
        return FALSE;
    }
    if (!lmme_tabs_close_tabs_to_right(app, anchor) || !lmme_tabs_close_tabs_to_left(app, anchor)) {
        return FALSE;
    }

    notebook = GTK_NOTEBOOK(app->notebook);
    anchor_page = gtk_notebook_page_num(notebook, anchor->scroller);
    if (anchor_page >= 0) {
        gtk_notebook_set_current_page(notebook, anchor_page);
    }
    return TRUE;
}

gboolean
lmme_tabs_close_all(LmmeApp *app)
{
    while (app->documents->len > 0) {
        guint before = app->documents->len;
        gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), 0);
        lmme_tabs_close_active(app);
        if (app->documents->len == before) {
            return FALSE;
        }
    }
    return TRUE;
}

void
lmme_tabs_close_path(LmmeApp *app, const char *path)
{
    LmmeDocument *doc = lmme_tabs_find_by_path(app, path);
    int page = -1;

    if (doc == NULL) {
        return;
    }

    page = gtk_notebook_page_num(GTK_NOTEBOOK(app->notebook), doc->scroller);
    if (page >= 0) {
        gtk_notebook_remove_page(GTK_NOTEBOOK(app->notebook), page);
    }
    remove_document(app, doc);
}

void
lmme_tabs_update_path(LmmeApp *app, const char *old_path, const char *new_path)
{
    LmmeDocument *doc = lmme_tabs_find_by_path(app, old_path);

    if (doc == NULL) {
        return;
    }

    g_free(doc->path);
    doc->path = g_canonicalize_filename(new_path, NULL);
    g_free(doc->relative_path);
    doc->relative_path = app->workspace != NULL ? lmme_path_relative_to(app->workspace->path, doc->path) : g_path_get_basename(new_path);
    lmme_document_file_monitor_detach(doc);
    lmme_document_file_monitor_attach(doc);
    update_tab_title(doc);
}

GPtrArray *
lmme_tabs_open_paths(LmmeApp *app)
{
    GPtrArray *paths = g_ptr_array_new_with_free_func(g_free);

    if (app == NULL || app->documents == NULL) {
        return paths;
    }

    for (guint i = 0; i < app->documents->len; i++) {
        LmmeDocument *doc = g_ptr_array_index(app->documents, i);
        if (doc->path != NULL && app->workspace != NULL &&
            lmme_path_is_inside(app->workspace->path, doc->path)) {
            g_ptr_array_add(paths, g_strdup(doc->path));
        }
    }

    return paths;
}
