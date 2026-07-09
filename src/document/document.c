#include "document/document.h"

#include "app/app.h"
#include "document/document_autosave.h"
#include "document/file_monitor.h"
#include "document/recovery.h"
#include "editor/editor.h"
#include "features/image_insert.h"
#include "infra/safe_write.h"
#include "infra/util.h"
#include "ui/window.h"
#include "workspace/workspace.h"

#include <string.h>

static void on_buffer_changed(GtkTextBuffer *buffer, gpointer user_data);
static void on_cursor_moved(GObject *object, GParamSpec *pspec, gpointer user_data);

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

void
lmme_document_set_save_state(LmmeDocument *doc, LmmeSaveState state)
{
    if (doc == NULL) {
        return;
    }

    doc->save_state = state;
    doc->modified = state == LMME_SAVE_STATE_MODIFIED || state == LMME_SAVE_STATE_ERROR;
    update_tab_title(doc);
    lmme_window_update_status(doc->app);
}

LmmeDocument *
lmme_document_new(LmmeApp *app, const char *path, const char *contents, const char *relative_title)
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
    lmme_document_file_monitor_attach(doc);
    if (app->preview_enabled) {
        (void)lmme_document_set_preview_visible(doc, TRUE);
    }

    return doc;
}

LmmePreviewApplyResult
lmme_document_set_preview_visible(LmmeDocument *doc, gboolean visible)
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

gboolean
lmme_document_save(LmmeDocument *doc, GError **error)
{
    g_autofree char *text = NULL;

    if (doc == NULL) {
        return TRUE;
    }

    text = lmme_editor_dup_text(GTK_TEXT_BUFFER(doc->buffer));
    if (!lmme_safe_write_file(doc->path, text, strlen(text), error)) {
        return FALSE;
    }

    doc->last_internal_save_us = g_get_monotonic_time();
    gtk_text_buffer_set_modified(GTK_TEXT_BUFFER(doc->buffer), FALSE);
    lmme_recovery_remove(doc->path, NULL);
    lmme_document_cancel_autosave(doc);
    lmme_document_cancel_recovery(doc);
    lmme_document_set_save_state(doc, LMME_SAVE_STATE_SAVED);
    return TRUE;
}

void
lmme_document_free(LmmeDocument *doc)
{
    if (doc == NULL) {
        return;
    }

    lmme_document_cancel_autosave(doc);
    lmme_document_cancel_recovery(doc);
    lmme_document_file_monitor_detach(doc);
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
    lmme_document_schedule_recovery(doc);
    lmme_document_schedule_autosave(doc);
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
