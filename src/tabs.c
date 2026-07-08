#include "tabs.h"

#include "autosave.h"
#include "dialogs.h"
#include "editor.h"
#include "image_insert.h"
#include "preview.h"
#include "util.h"
#include "window.h"

static GtkWidget *tab_context_popover;
static GMenuModel *tab_context_menu_model;

static void on_buffer_changed(GtkTextBuffer *buffer, gpointer user_data);
static void on_cursor_moved(GObject *object, GParamSpec *pspec, gpointer user_data);
static LmmePreviewApplyResult apply_document_preview_state(LmmeDocument *doc, gboolean visible);
static GMenuModel *create_tab_context_menu_model(void);
static void on_tab_right_click(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data);
static void attach_tab_context_menu(LmmeDocument *doc, GtkWidget *tab_box);
static GMenuModel *get_tab_context_menu_model(void);
static GtkWidget *get_tab_context_popover(void);

const char *
lmme_document_save_state_label(const LmmeDocument *doc)
{
    if (doc == NULL) {
        return "Saved";
    }

    switch (doc->save_state) {
    case LMME_SAVE_STATE_MODIFIED:
        return "Modified";
    case LMME_SAVE_STATE_AUTOSAVED:
        return "Autosaved";
    case LMME_SAVE_STATE_ERROR:
        return "Error";
    case LMME_SAVE_STATE_SAVED:
    default:
        return "Saved";
    }
}

static void
update_tab_title(LmmeDocument *doc)
{
    if (doc == NULL || doc->title_label == NULL) {
        return;
    }

    g_autofree char *base = g_path_get_basename(doc->path);
    g_autofree char *title = doc->modified ? g_strdup_printf("● %s", base) : g_strdup(base);

    gtk_label_set_text(GTK_LABEL(doc->title_label), title);
}

void
lmme_document_set_save_state(LmmeDocument *doc, LmmeSaveState state)
{
    doc->save_state = state;
    doc->modified = state == LMME_SAVE_STATE_MODIFIED || state == LMME_SAVE_STATE_ERROR;
    update_tab_title(doc);
    lmme_window_update_status(doc->app);
}

static gboolean
recovery_timeout_cb(gpointer user_data)
{
    LmmeDocument *doc = user_data;
    g_autofree char *text = lmme_editor_dup_text(GTK_TEXT_BUFFER(doc->buffer));
    g_autoptr(GError) error = NULL;

    doc->recovery_id = 0;
    if (!lmme_recovery_write(doc->path, text, strlen(text), &error)) {
        g_warning("Could not write recovery file: %s", error != NULL ? error->message : "unknown error");
    }

    return G_SOURCE_REMOVE;
}

static void
schedule_recovery(LmmeDocument *doc)
{
    if (doc->recovery_id != 0) {
        g_source_remove(doc->recovery_id);
    }
    doc->recovery_id = g_timeout_add(2000, recovery_timeout_cb, doc);
}

static gboolean
autosave_timeout_cb(gpointer user_data)
{
    LmmeDocument *doc = user_data;
    g_autoptr(GError) error = NULL;

    doc->autosave_id = 0;
    if (lmme_tabs_save_document(doc, &error)) {
        lmme_document_set_save_state(doc, LMME_SAVE_STATE_AUTOSAVED);
    } else {
        lmme_document_set_save_state(doc, LMME_SAVE_STATE_ERROR);
        lmme_window_set_status_error(doc->app, "Could not save file.");
    }

    return G_SOURCE_REMOVE;
}

static void
schedule_autosave(LmmeDocument *doc)
{
    if (!doc->app->config.autosave) {
        return;
    }
    if (doc->autosave_id != 0) {
        g_source_remove(doc->autosave_id);
    }
    doc->autosave_id = g_timeout_add(doc->app->config.autosave_delay_ms, autosave_timeout_cb, doc);
}

static void
on_close_tab_clicked(GtkButton *button, gpointer user_data)
{
    LmmeDocument *doc = user_data;
    (void)button;

    GtkNotebook *notebook = GTK_NOTEBOOK(doc->app->notebook);
    int page = gtk_notebook_page_num(notebook, doc->scroller);
    if (page >= 0) {
        gtk_notebook_set_current_page(notebook, page);
        lmme_tabs_close_active(doc->app);
    }
}

static GMenuModel *
create_tab_context_menu_model(void)
{
    GMenu *menu = g_menu_new();
    GMenu *close_section = g_menu_new();
    GMenu *path_section = g_menu_new();

    g_menu_append(close_section, "Close Tab", "app.close-tab");
    g_menu_append(close_section, "Close Other Tabs", "app.close-other-tabs");
    g_menu_append(close_section, "Close Tabs to the Right", "app.close-tabs-right");
    g_menu_append(close_section, "Close Tabs to the Left", "app.close-tabs-left");
    g_menu_append(close_section, "Close All Tabs", "app.close-all-tabs");

    g_menu_append(path_section, "Copy Relative Path", "app.copy-relative-path");
    g_menu_append(path_section, "Copy Full Path", "app.copy-full-path");

    g_menu_append_section(menu, NULL, G_MENU_MODEL(close_section));
    g_menu_append_section(menu, NULL, G_MENU_MODEL(path_section));

    g_object_unref(close_section);
    g_object_unref(path_section);

    return G_MENU_MODEL(menu);
}

static GMenuModel *
get_tab_context_menu_model(void)
{
    if (tab_context_menu_model == NULL) {
        tab_context_menu_model = create_tab_context_menu_model();
    }

    return tab_context_menu_model;
}

static GtkWidget *
get_tab_context_popover(void)
{
    if (tab_context_popover == NULL) {
        tab_context_popover = gtk_popover_menu_new_from_model(get_tab_context_menu_model());
        gtk_popover_set_has_arrow(GTK_POPOVER(tab_context_popover), FALSE);
        gtk_popover_set_autohide(GTK_POPOVER(tab_context_popover), TRUE);
    }

    return tab_context_popover;
}

static void
on_tab_right_click(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data)
{
    LmmeDocument *doc = user_data;
    LmmeApp *app = doc != NULL ? doc->app : NULL;
    GtkNotebook *notebook = NULL;
    GtkWidget *popover = NULL;
    GtkWidget *anchor = NULL;
    int page = -1;
    GdkRectangle rect;
    graphene_point_t src_point;
    graphene_point_t dest_point;

    (void)gesture;

    if (doc == NULL || app == NULL || app->notebook == NULL || app->root_box == NULL || app->window == NULL ||
        doc->scroller == NULL || doc->tab_box == NULL) {
        return;
    }

    if (n_press != 1) {
        return;
    }

    notebook = GTK_NOTEBOOK(app->notebook);
    page = gtk_notebook_page_num(notebook, doc->scroller);
    if (page < 0) {
        return;
    }

    gtk_notebook_set_current_page(notebook, page);
    app->tab_context_document = doc;

    popover = get_tab_context_popover();
    anchor = app->root_box;

    if (gtk_widget_get_parent(popover) != anchor) {
        if (gtk_widget_get_parent(popover) != NULL) {
            gtk_widget_unparent(popover);
        }
        gtk_widget_set_parent(popover, anchor);
    }

    if (gtk_widget_get_visible(popover)) {
        gtk_popover_popdown(GTK_POPOVER(popover));
    }

    graphene_point_init(&src_point, (float)x, (float)y);
    if (!gtk_widget_compute_point(doc->tab_box, anchor, &src_point, &dest_point)) {
        return;
    }

    rect.x = (int)dest_point.x;
    rect.y = (int)dest_point.y;
    rect.width = 1;
    rect.height = 1;

    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
    gtk_popover_popup(GTK_POPOVER(popover));
}

static void
attach_tab_context_menu(LmmeDocument *doc, GtkWidget *tab_box)
{
    GtkGesture *gesture = NULL;

    if (doc == NULL || tab_box == NULL) {
        return;
    }

    gesture = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_SECONDARY);
    g_signal_connect(gesture, "pressed", G_CALLBACK(on_tab_right_click), doc);
    gtk_widget_add_controller(tab_box, GTK_EVENT_CONTROLLER(gesture));
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
    attach_tab_context_menu(doc, box);

    doc->title_label = label;
    doc->tab_box = box;
    update_tab_title(doc);

    return box;
}

static void
on_file_monitor_changed(GFileMonitor *monitor,
                        GFile *file,
                        GFile *other_file,
                        GFileMonitorEvent event_type,
                        gpointer user_data)
{
    LmmeDocument *doc = user_data;
    (void)monitor;
    (void)file;
    (void)other_file;

    if (event_type != G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT &&
        event_type != G_FILE_MONITOR_EVENT_CREATED &&
        event_type != G_FILE_MONITOR_EVENT_DELETED) {
        return;
    }

    if (g_get_monotonic_time() - doc->last_internal_save_us < 2000000) {
        return;
    }

    if (doc->modified) {
        lmme_dialog_error(GTK_WINDOW(doc->app->window),
                          "File changed outside the editor.",
                          "Local changes were kept. Save manually to overwrite the file.");
        return;
    }

    g_autofree char *contents = NULL;
    gsize length = 0;
    g_autoptr(GError) error = NULL;
    if (g_file_get_contents(doc->path, &contents, &length, &error) && g_utf8_validate(contents, (gssize)length, NULL)) {
        g_signal_handler_block(doc->buffer, doc->changed_handler_id);
        gtk_text_buffer_set_text(GTK_TEXT_BUFFER(doc->buffer), contents, (int)length);
        g_signal_handler_unblock(doc->buffer, doc->changed_handler_id);
        lmme_document_set_save_state(doc, LMME_SAVE_STATE_SAVED);
        if (doc->app->preview_enabled) {
            (void)apply_document_preview_state(doc, TRUE);
        }
    }
}

static void
attach_monitor(LmmeDocument *doc)
{
    g_autoptr(GFile) file = g_file_new_for_path(doc->path);
    g_autoptr(GError) error = NULL;

    doc->monitor = g_file_monitor_file(file, G_FILE_MONITOR_NONE, NULL, &error);
    if (doc->monitor != NULL) {
        g_signal_connect(doc->monitor, "changed", G_CALLBACK(on_file_monitor_changed), doc);
    }
}

static LmmeDocument *
document_new(LmmeApp *app, const char *path, const char *contents, const char *relative_title)
{
    LmmeDocument *doc = g_new0(LmmeDocument, 1);
    doc->app = app;
    doc->path = g_canonicalize_filename(path, NULL);
    doc->relative_path = app->workspace != NULL ? lmme_path_relative_to(app->workspace->path, doc->path) : g_strdup(relative_title);
    doc->source_view = lmme_editor_create_view(&doc->buffer, &app->config);
    lmme_editor_setup_zoom_keys(doc->source_view, G_ACTION_GROUP(app->gtk_app));
    doc->scroller = gtk_scrolled_window_new();
    doc->save_state = LMME_SAVE_STATE_SAVED;

    gtk_widget_set_hexpand(doc->scroller, TRUE);
    gtk_widget_set_vexpand(doc->scroller, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(doc->scroller), doc->source_view);
    lmme_image_insert_setup_for_document(doc);
    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(doc->buffer), contents != NULL ? contents : "", -1);
    gtk_text_buffer_set_modified(GTK_TEXT_BUFFER(doc->buffer), FALSE);

    doc->changed_handler_id = g_signal_connect(doc->buffer, "changed", G_CALLBACK(on_buffer_changed), doc);
    g_signal_connect(doc->buffer, "notify::cursor-position", G_CALLBACK(on_cursor_moved), doc);
    attach_monitor(doc);
    if (app->preview_enabled) {
        (void)apply_document_preview_state(doc, TRUE);
    }

    return doc;
}

static LmmePreviewApplyResult
apply_document_preview_state(LmmeDocument *doc, gboolean visible)
{
    GtkTextBuffer *buffer = doc != NULL ? GTK_TEXT_BUFFER(doc->buffer) : NULL;
    GtkAdjustment *hadjustment = NULL;
    GtkAdjustment *vadjustment = NULL;
    GtkTextIter iter;
    GtkTextIter selection_start;
    GtkTextIter selection_end;
    gboolean had_selection = FALSE;
    int insert_offset = 0;
    int selection_start_offset = 0;
    int selection_end_offset = 0;
    double hvalue = 0.0;
    double vvalue = 0.0;
    gboolean buffer_modified = FALSE;
    gboolean doc_modified = FALSE;
    LmmeSaveState save_state = LMME_SAVE_STATE_SAVED;
    LmmePreviewApplyResult result = LMME_PREVIEW_APPLY_OK;

    if (doc == NULL || buffer == NULL || doc->source_view == NULL) {
        return LMME_PREVIEW_APPLY_FAILED;
    }

    buffer_modified = gtk_text_buffer_get_modified(buffer);
    doc_modified = doc->modified;
    save_state = doc->save_state;

    if (gtk_text_buffer_get_selection_bounds(buffer, &selection_start, &selection_end)) {
        had_selection = TRUE;
        selection_start_offset = gtk_text_iter_get_offset(&selection_start);
        selection_end_offset = gtk_text_iter_get_offset(&selection_end);
    } else {
        gtk_text_buffer_get_iter_at_mark(buffer, &iter, gtk_text_buffer_get_insert(buffer));
        insert_offset = gtk_text_iter_get_offset(&iter);
    }

    if (doc->scroller != NULL) {
        hadjustment = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(doc->scroller));
        vadjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(doc->scroller));
        hvalue = gtk_adjustment_get_value(hadjustment);
        vvalue = gtk_adjustment_get_value(vadjustment);
    }

    if (doc->changed_handler_id != 0) {
        g_signal_handler_block(buffer, doc->changed_handler_id);
    }

    if (visible) {
        result = lmme_preview_apply_editable_preview(buffer, doc->app->config.preview_hide_frontmatter, TRUE);
        if (result == LMME_PREVIEW_APPLY_FAILED) {
            lmme_preview_clear_editable_preview(buffer);
            gtk_widget_remove_css_class(doc->source_view, "preview-edit-mode");
        } else {
            gtk_widget_add_css_class(doc->source_view, "preview-edit-mode");
        }
    } else {
        lmme_preview_clear_editable_preview(buffer);
        gtk_widget_remove_css_class(doc->source_view, "preview-edit-mode");
    }

    if (doc->changed_handler_id != 0) {
        g_signal_handler_unblock(buffer, doc->changed_handler_id);
    }

    if (had_selection) {
        GtkTextIter current_start;
        GtkTextIter current_end;
        gboolean has_current_selection = gtk_text_buffer_get_selection_bounds(buffer, &current_start, &current_end);
        if (!has_current_selection ||
            gtk_text_iter_get_offset(&current_start) != selection_start_offset ||
            gtk_text_iter_get_offset(&current_end) != selection_end_offset) {
            gtk_text_buffer_get_iter_at_offset(buffer, &selection_start, selection_start_offset);
            gtk_text_buffer_get_iter_at_offset(buffer, &selection_end, selection_end_offset);
            gtk_text_buffer_select_range(buffer, &selection_start, &selection_end);
        }
    } else {
        gtk_text_buffer_get_iter_at_mark(buffer, &iter, gtk_text_buffer_get_insert(buffer));
        if (gtk_text_iter_get_offset(&iter) != insert_offset) {
            gtk_text_buffer_get_iter_at_offset(buffer, &iter, insert_offset);
            gtk_text_buffer_place_cursor(buffer, &iter);
        }
    }

    if (hadjustment != NULL && vadjustment != NULL) {
        gtk_adjustment_set_value(hadjustment, hvalue);
        gtk_adjustment_set_value(vadjustment, vvalue);
    }

    gtk_text_buffer_set_modified(buffer, buffer_modified);
    doc->modified = doc_modified;
    doc->save_state = save_state;
    update_tab_title(doc);

    return result;
}

LmmePreviewApplyResult
lmme_document_set_preview_visible(LmmeDocument *doc, gboolean visible)
{
    return apply_document_preview_state(doc, visible);
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
        LmmePreviewApplyResult result = apply_document_preview_state(doc, visible);
        if (doc == active) {
            active_result = result;
        }
    }

    return active_result;
}

LmmeDocument *
lmme_tabs_find_by_path(LmmeApp *app, const char *path)
{
    g_autofree char *canon = g_canonicalize_filename(path, NULL);

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
    int page = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook));
    GtkWidget *child = page >= 0 ? gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), page) : NULL;

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

    add_document_to_notebook(app, document_new(app, path, contents, NULL));
    return TRUE;
}

gboolean
lmme_tabs_open_recovery_file(LmmeApp *app, const char *path, const char *title, GError **error)
{
    g_autofree char *contents = NULL;

    if (!open_file_contents(path, &contents, error)) {
        return FALSE;
    }

    add_document_to_notebook(app, document_new(app, path, contents, title));
    return TRUE;
}

gboolean
lmme_tabs_save_document(LmmeDocument *doc, GError **error)
{
    g_autofree char *text = lmme_editor_dup_text(GTK_TEXT_BUFFER(doc->buffer));

    if (!lmme_safe_write_file(doc->path, text, strlen(text), error)) {
        return FALSE;
    }

    doc->last_internal_save_us = g_get_monotonic_time();
    gtk_text_buffer_set_modified(GTK_TEXT_BUFFER(doc->buffer), FALSE);
    lmme_recovery_remove(doc->path, NULL);
    if (doc->autosave_id != 0) {
        g_source_remove(doc->autosave_id);
        doc->autosave_id = 0;
    }
    if (doc->recovery_id != 0) {
        g_source_remove(doc->recovery_id);
        doc->recovery_id = 0;
    }

    lmme_document_set_save_state(doc, LMME_SAVE_STATE_SAVED);
    return TRUE;
}

gboolean
lmme_tabs_save_active(LmmeApp *app, GError **error)
{
    LmmeDocument *doc = lmme_tabs_get_active(app);
    if (doc == NULL) {
        return TRUE;
    }
    return lmme_tabs_save_document(doc, error);
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
        if (!lmme_tabs_save_document(doc, &error) &&
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

    if (!lmme_tabs_close_tabs_to_right(app, anchor)) {
        return FALSE;
    }

    if (!lmme_tabs_close_tabs_to_left(app, anchor)) {
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
    if (doc == NULL) {
        return;
    }

    int page = gtk_notebook_page_num(GTK_NOTEBOOK(app->notebook), doc->scroller);
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

    if (doc->monitor != NULL) {
        g_file_monitor_cancel(doc->monitor);
        g_clear_object(&doc->monitor);
    }
    attach_monitor(doc);
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
        if (doc->path != NULL &&
            app->workspace != NULL &&
            lmme_path_is_inside(app->workspace->path, doc->path)) {
            g_ptr_array_add(paths, g_strdup(doc->path));
        }
    }

    return paths;
}

void
lmme_document_free(LmmeDocument *doc)
{
    if (doc == NULL) {
        return;
    }

    if (doc->autosave_id != 0) {
        g_source_remove(doc->autosave_id);
    }
    if (doc->recovery_id != 0) {
        g_source_remove(doc->recovery_id);
    }
    if (doc->monitor != NULL) {
        g_file_monitor_cancel(doc->monitor);
        g_clear_object(&doc->monitor);
    }

    g_clear_object(&doc->buffer);
    g_free(doc->path);
    g_free(doc->relative_path);
    g_free(doc);
}

static void
on_buffer_changed(GtkTextBuffer *buffer, gpointer user_data)
{
    LmmeDocument *doc = user_data;
    (void)buffer;

    lmme_document_set_save_state(doc, LMME_SAVE_STATE_MODIFIED);
    schedule_recovery(doc);
    schedule_autosave(doc);
    lmme_window_schedule_preview(doc->app);
}

static void
on_cursor_moved(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    LmmeDocument *doc = user_data;
    (void)object;
    (void)pspec;
    if (doc->app->preview_enabled) {
        lmme_window_refresh_preview_now(doc->app);
    } else {
        lmme_window_update_status(doc->app);
    }
}
