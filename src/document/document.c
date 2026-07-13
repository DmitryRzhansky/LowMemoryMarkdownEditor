#define _GNU_SOURCE

#include "document/document.h"
#include "document/tabs.h"

#include "app/app.h"
#include "document/document_autosave.h"
#include "document/document_persistence.h"
#include "document/file_monitor.h"
#include "document/recovery.h"
#include "editor/editor.h"
#include "features/image_insert.h"
#include "infra/util.h"
#include "ui/external_conflict.h"
#include "ui/window.h"
#include "workspace/workspace.h"

#include <string.h>
static void on_buffer_changed(GtkTextBuffer *buffer, gpointer user_data);
static void on_cursor_moved(GObject *object, GParamSpec *pspec, gpointer user_data);

#define LMME_DOCUMENT_STATS_DEBOUNCE_MS 250

static gboolean
document_is_active_tab(const LmmeDocument *doc)
{
    return doc != NULL && doc->app != NULL && lmme_tabs_get_active(doc->app) == doc;
}

static gsize
document_buffer_byte_length(const LmmeDocument *doc)
{
    GtkTextIter start;
    GtkTextIter end;

    gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(doc->buffer), &start, &end);
    return (gsize)gtk_text_iter_get_offset(&end);
}

static void
document_update_word_count_now(LmmeDocument *doc)
{
    g_autofree char *text = NULL;

    if (document_buffer_byte_length(doc) > LMME_DOCUMENT_WORD_COUNT_MAX_BYTES) {
        doc->word_count_valid = FALSE;
        doc->word_count_dirty = FALSE;
        return;
    }

    text = lmme_editor_dup_text(GTK_TEXT_BUFFER(doc->buffer));
    doc->word_count = lmme_word_count(text);
    doc->word_count_valid = TRUE;
    doc->word_count_dirty = FALSE;
}

static gboolean
stats_timeout_cb(gpointer user_data)
{
    LmmeDocument *doc = user_data;

    doc->stats_timeout_id = 0;
    if (!doc->word_count_dirty || !document_is_active_tab(doc) ||
        !doc->app->config.show_statusbar || doc->app->status_label == NULL ||
        !gtk_widget_get_visible(doc->app->status_label)) {
        return G_SOURCE_REMOVE;
    }

    document_update_word_count_now(doc);
    lmme_window_update_status(doc->app);
    return G_SOURCE_REMOVE;
}

void
lmme_document_mark_stats_dirty(LmmeDocument *doc)
{
    if (doc == NULL) {
        return;
    }
    doc->word_count_dirty = TRUE;
}

void
lmme_document_request_stats_update(LmmeDocument *doc)
{
    if (doc == NULL || !document_is_active_tab(doc)) {
        return;
    }
    if (!doc->word_count_dirty) {
        return;
    }
    if (doc->stats_timeout_id != 0) {
        g_source_remove(doc->stats_timeout_id);
        doc->stats_timeout_id = 0;
    }
    if (doc->app->config.show_statusbar && doc->app->status_label != NULL &&
        gtk_widget_get_visible(doc->app->status_label)) {
        doc->stats_timeout_id = g_timeout_add(LMME_DOCUMENT_STATS_DEBOUNCE_MS,
                                              stats_timeout_cb,
                                              doc);
    }
}

gboolean
lmme_document_word_count_is_valid(const LmmeDocument *doc)
{
    return doc != NULL && doc->word_count_valid;
}

void
lmme_document_refresh_title(LmmeDocument *doc)
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
    if (doc->disk_state == LMME_DISK_STATE_EXTERNAL_CHANGED) {
        return "Conflict";
    }
    if (doc->disk_state == LMME_DISK_STATE_EXTERNAL_DELETED) {
        return "Deleted";
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
    lmme_document_refresh_title(doc);
    lmme_window_update_status(doc->app);
}

LmmeDocument *
lmme_document_new(LmmeApp *app, const char *path, const char *contents, const char *relative_title)
{
    LmmeDocument *doc = g_new0(LmmeDocument, 1);

    doc->app = app;
    doc->id = app->next_document_id++;
    doc->path = g_canonicalize_filename(path, NULL);
    doc->relative_path = app->workspace != NULL ? lmme_path_relative_to(app->workspace->path, doc->path) : g_strdup(relative_title);
    doc->source_view = lmme_editor_create_view(&doc->buffer, &app->config);
    lmme_editor_setup_zoom_keys(doc->source_view, G_ACTION_GROUP(app->gtk_app));
    doc->scroller = gtk_scrolled_window_new();
    doc->save_state = LMME_SAVE_STATE_SAVED;
    doc->disk_state = LMME_DISK_STATE_NORMAL;
    doc->preview_dirty = TRUE;
    (void)lmme_file_fingerprint_read(doc->path, &doc->last_known_fingerprint, NULL);
    doc->base_fingerprint = doc->last_known_fingerprint;

    gtk_widget_set_hexpand(doc->scroller, TRUE);
    gtk_widget_set_vexpand(doc->scroller, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(doc->scroller), doc->source_view);
    lmme_image_insert_setup_for_document(doc);
    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(doc->buffer), contents != NULL ? contents : "", -1);
    gtk_text_buffer_set_modified(GTK_TEXT_BUFFER(doc->buffer), FALSE);
    if (contents != NULL && strlen(contents) <= LMME_DOCUMENT_WORD_COUNT_MAX_BYTES) {
        doc->word_count = lmme_word_count(contents);
        doc->word_count_valid = TRUE;
    } else if (contents == NULL) {
        doc->word_count = 0;
        doc->word_count_valid = TRUE;
    } else {
        doc->word_count = 0;
        doc->word_count_valid = FALSE;
    }

    doc->changed_handler_id = g_signal_connect(doc->buffer, "changed", G_CALLBACK(on_buffer_changed), doc);
    g_signal_connect(doc->buffer, "notify::cursor-position", G_CALLBACK(on_cursor_moved), doc);
    lmme_document_file_monitor_attach(doc);
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
            gtk_text_buffer_get_iter_at_mark(buffer, &iter, gtk_text_buffer_get_insert(buffer));
            doc->preview_active_line = (guint)gtk_text_iter_get_line(&iter);
            doc->preview_active_line_valid = TRUE;
            doc->preview_dirty = FALSE;
            doc->preview_full_parse_count++;
        }
    } else {
        lmme_preview_clear_editable_preview(buffer);
        gtk_widget_remove_css_class(doc->source_view, "preview-edit-mode");
        doc->preview_active_line_valid = FALSE;
        doc->preview_dirty = TRUE;
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
    lmme_document_refresh_title(doc);

    return result;
}

void
lmme_document_update_preview_active_line(LmmeDocument *doc)
{
    GtkTextIter cursor;
    guint new_line = 0;

    if (doc == NULL || !doc->app->preview_enabled || doc->preview_dirty) {
        return;
    }
    gtk_text_buffer_get_iter_at_mark(GTK_TEXT_BUFFER(doc->buffer),
                                     &cursor,
                                     gtk_text_buffer_get_insert(GTK_TEXT_BUFFER(doc->buffer)));
    new_line = (guint)gtk_text_iter_get_line(&cursor);
    if (doc->preview_active_line_valid && doc->preview_active_line == new_line) {
        return;
    }
    lmme_preview_update_active_line(GTK_TEXT_BUFFER(doc->buffer),
                                    doc->preview_active_line,
                                    doc->preview_active_line_valid,
                                    new_line);
    doc->preview_active_line = new_line;
    doc->preview_active_line_valid = TRUE;
}

gboolean
lmme_document_write_recovery_snapshot(LmmeDocument *doc,
                                      const char *snapshot,
                                      gsize length,
                                      guint64 revision,
                                      GError **error)
{
    const char *workspace_path = NULL;
    gboolean durable = FALSE;

    if (doc == NULL || snapshot == NULL) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Invalid recovery snapshot.");
        return FALSE;
    }
    workspace_path = doc->app->workspace != NULL ? doc->app->workspace->path : NULL;
    durable = lmme_recovery_write(doc->app->recovery_store,
                                  doc->path,
                                  workspace_path,
                                  &doc->base_fingerprint,
                                  snapshot,
                                  length,
                                  error);
    if (durable && revision != doc->content_revision) {
        durable = FALSE;
        g_set_error_literal(error,
                            G_FILE_ERROR,
                            G_FILE_ERROR_AGAIN,
                            "The buffer changed while recovery data was being written.");
    }
    doc->recovery_failed = !durable;
    lmme_window_update_status(doc->app);
    return durable;
}

gboolean
lmme_document_flush_recovery(LmmeDocument *doc, GError **error)
{
    g_autofree char *text = NULL;
    guint64 revision = 0;

    if (doc == NULL || (!doc->modified && !doc->restored_from_recovery &&
                        doc->disk_state == LMME_DISK_STATE_NORMAL)) {
        return TRUE;
    }
    revision = doc->content_revision;
    text = lmme_editor_dup_text(GTK_TEXT_BUFFER(doc->buffer));
    return lmme_document_write_recovery_snapshot(doc,
                                                 text,
                                                 strlen(text),
                                                 revision,
                                                 error);
}

void
lmme_document_mark_recovered(LmmeDocument *doc,
                             const char *recovery_source_path,
                             gboolean original_changed)
{
    if (doc == NULL) {
        return;
    }
    g_free(doc->recovery_source_path);
    doc->recovery_source_path = g_strdup(recovery_source_path);
    doc->restored_from_recovery = TRUE;
    doc->disk_state = original_changed ? LMME_DISK_STATE_EXTERNAL_CHANGED : LMME_DISK_STATE_NORMAL;
    lmme_document_set_save_state(doc, LMME_SAVE_STATE_MODIFIED);
}

LmmeDocumentSaveResult
lmme_document_save(LmmeDocument *doc, GError **error)
{
    return lmme_document_persist(doc,
                                 doc != NULL ? doc->path : NULL,
                                 FALSE,
                                 FALSE,
                                 error);
}

LmmeDocumentSaveResult
lmme_document_overwrite(LmmeDocument *doc, GError **error)
{
    return lmme_document_persist(doc,
                                 doc != NULL ? doc->path : NULL,
                                 TRUE,
                                 FALSE,
                                 error);
}

LmmeDocumentSaveResult
lmme_document_save_as(LmmeDocument *doc, const char *new_path, GError **error)
{
    return lmme_document_persist(doc, new_path, TRUE, TRUE, error);
}

void
lmme_document_free(LmmeDocument *doc)
{
    if (doc == NULL) {
        return;
    }

    lmme_document_cancel_autosave(doc);
    lmme_document_cancel_recovery(doc);
    lmme_external_conflict_cancel(doc);
    if (doc->stats_timeout_id != 0) {
        g_source_remove(doc->stats_timeout_id);
    }
    lmme_document_file_monitor_detach(doc);
    if (doc->image_insert_cancellable != NULL) {
        g_cancellable_cancel(doc->image_insert_cancellable);
    }
    g_clear_object(&doc->image_insert_cancellable);
    g_clear_object(&doc->buffer);
    g_free(doc->path);
    g_free(doc->relative_path);
    g_free(doc->recovery_source_path);
    g_free(doc);
}

static void
on_buffer_changed(GtkTextBuffer *buffer, gpointer user_data)
{
    LmmeDocument *doc = user_data;
    (void)buffer;

    doc->content_revision++;
    lmme_document_mark_stats_dirty(doc);
    lmme_document_request_stats_update(doc);
    doc->preview_dirty = TRUE;
    lmme_document_set_save_state(doc, LMME_SAVE_STATE_MODIFIED);
    if (!doc->app->scheduling_blocked) {
        lmme_document_schedule_recovery(doc);
        lmme_document_schedule_autosave(doc);
    }
    lmme_window_schedule_preview(doc->app);
}

guint
lmme_document_cached_word_count(const LmmeDocument *doc)
{
    return doc != NULL ? doc->word_count : 0;
}

static void
on_cursor_moved(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    LmmeDocument *doc = user_data;
    (void)object;
    (void)pspec;

    if (doc->app->preview_enabled) {
        lmme_document_update_preview_active_line(doc);
        lmme_window_update_status(doc->app);
    } else {
        lmme_window_update_status(doc->app);
    }
}
