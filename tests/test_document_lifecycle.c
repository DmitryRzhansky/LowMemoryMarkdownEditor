#include <glib.h>
#include <glib/gstdio.h>
#include <gtksourceview/gtksource.h>
#include <string.h>

#include "app/app.h"
#include "document/document.h"
#include "document/document_autosave.h"
#include "document/document_autosave_test.h"
#include "document/file_monitor.h"
#include "document/recovery.h"
#include "document/recovery_test.h"
#include "document/tabs.h"
#include "document/tabs_test.h"
#include "editor/preview.h"
#include "infra/safe_write_test.h"
#include "ui/statusbar.h"
#include "workspace/workspace.h"

static void
remove_directory_contents(const char *directory)
{
    g_autoptr(GDir) dir = g_dir_open(directory, 0, NULL);
    const char *name = NULL;
    if (dir == NULL) {
        return;
    }
    while ((name = g_dir_read_name(dir)) != NULL) {
        g_autofree char *path = g_build_filename(directory, name, NULL);
        g_remove(path);
    }
}

static LmmeDocument *
test_document_new(LmmeApp *app, const char *path, const char *text)
{
    LmmeDocument *doc = g_new0(LmmeDocument, 1);

    doc->app = app;
    doc->path = g_strdup(path);
    doc->relative_path = g_path_get_basename(path);
    doc->buffer = gtk_source_buffer_new(NULL);
    doc->modified = TRUE;
    doc->save_state = LMME_SAVE_STATE_MODIFIED;
    doc->disk_state = LMME_DISK_STATE_NORMAL;
    g_assert_true(lmme_file_fingerprint_read(path, &doc->base_fingerprint, NULL));
    doc->last_known_fingerprint = doc->base_fingerprint;
    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(doc->buffer), text, -1);
    gtk_text_buffer_set_modified(GTK_TEXT_BUFFER(doc->buffer), TRUE);
    return doc;
}

static void
test_document_environment_clear(LmmeApp *app,
                                LmmeDocument *doc,
                                const char *root,
                                const char *cache)
{
    lmme_document_free(doc);
    lmme_recovery_store_free(app->recovery_store);
    lmme_workspace_free(app->workspace);
    remove_directory_contents(cache);
    g_rmdir(cache);

    g_autoptr(GDir) directory = g_dir_open(root, 0, NULL);
    const char *name = NULL;
    while (directory != NULL && (name = g_dir_read_name(directory)) != NULL) {
        g_autofree char *path = g_build_filename(root, name, NULL);
        g_unlink(path);
    }
    g_rmdir(root);
}

static void
test_conflict_blocks_autosave(void)
{
    LmmeApp app = {0};
    LmmeDocument doc = {0};

    app.config.autosave = TRUE;
    app.config.autosave_delay_ms = 1;
    doc.app = &app;
    doc.disk_state = LMME_DISK_STATE_EXTERNAL_CHANGED;
    lmme_document_schedule_autosave(&doc);
    g_assert_cmpuint(doc.autosave_id, ==, 0);
}

typedef struct {
    guint calls;
    guint cancel_on_call;
} PrepareCloseState;

static gboolean
prepare_close_for_test(LmmeDocument *doc, gpointer user_data)
{
    PrepareCloseState *state = user_data;

    state->calls++;
    doc->pending_close = LMME_PENDING_CLOSE_DISCARD_LOCAL;
    return state->calls != state->cancel_on_call;
}

static void
test_bulk_close_prepare_is_all_or_nothing(void)
{
    LmmeDocument first = {0};
    LmmeDocument second = {0};
    LmmeDocument third = {0};
    g_autoptr(GPtrArray) documents = g_ptr_array_new();
    PrepareCloseState state = {0};

    g_ptr_array_add(documents, &first);
    g_ptr_array_add(documents, &second);
    g_ptr_array_add(documents, &third);
    state.cancel_on_call = 2;
    g_assert_false(lmme_tabs_test_prepare_documents(documents,
                                                    prepare_close_for_test,
                                                    &state));
    g_assert_cmpuint(state.calls, ==, 2);
    g_assert_cmpuint(documents->len, ==, 3);
    g_assert_cmpint(first.pending_close, ==, LMME_PENDING_CLOSE_NONE);
    g_assert_cmpint(second.pending_close, ==, LMME_PENDING_CLOSE_NONE);
    g_assert_cmpint(third.pending_close, ==, LMME_PENDING_CLOSE_NONE);

    state = (PrepareCloseState){0};
    g_assert_true(lmme_tabs_test_prepare_documents(documents,
                                                   prepare_close_for_test,
                                                   &state));
    g_assert_cmpuint(state.calls, ==, 3);
    g_assert_cmpuint(documents->len, ==, 3);
    g_assert_cmpint(first.pending_close, ==, LMME_PENDING_CLOSE_DISCARD_LOCAL);
    g_assert_cmpint(second.pending_close, ==, LMME_PENDING_CLOSE_DISCARD_LOCAL);
    g_assert_cmpint(third.pending_close, ==, LMME_PENDING_CLOSE_DISCARD_LOCAL);
}

static void
test_recovery_failure_state_and_retry(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-document-recovery-state-XXXXXX", NULL);
    g_autofree char *cache = g_build_filename(root, "recovery", NULL);
    g_autofree char *original = g_build_filename(root, "note.md", NULL);
    LmmeApp app = {0};
    LmmeDocument *doc = NULL;

    g_assert_true(g_file_set_contents(original, "disk", -1, NULL));
    app.workspace = lmme_workspace_new(root);
    app.recovery_store = lmme_recovery_store_new(cache);
    app.config.autosave = FALSE;
    doc = test_document_new(&app, original, "latest local text");

    lmme_document_schedule_recovery(doc);
    g_assert_cmpuint(doc->recovery_id, !=, 0);
    lmme_safe_write_test_fail_at(LMME_SAFE_WRITE_TEST_FAIL_TEMP_CREATE, 1);
    g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*Could not write recovery file*");
    g_assert_false(lmme_document_test_run_recovery_timeout(doc));
    g_test_assert_expected_messages();
    lmme_safe_write_test_reset();
    g_assert_cmpuint(doc->recovery_id, ==, 0);
    g_assert_true(doc->recovery_failed);
    g_assert_true(doc->modified);

    g_assert_true(lmme_document_flush_recovery(doc, NULL));
    g_assert_false(doc->recovery_failed);

    doc->recovery_failed = TRUE;
    g_assert_cmpint(lmme_document_save(doc, NULL),
                    ==,
                    LMME_DOCUMENT_SAVE_COMMITTED_DURABLE);
    g_assert_false(doc->recovery_failed);
    g_assert_false(doc->modified);

    test_document_environment_clear(&app, doc, root, cache);
}

static void
test_stale_recovery_snapshot_does_not_clear_failure(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-document-stale-recovery-XXXXXX", NULL);
    g_autofree char *cache = g_build_filename(root, "recovery", NULL);
    g_autofree char *original = g_build_filename(root, "note.md", NULL);
    g_autoptr(GError) error = NULL;
    LmmeApp app = {0};
    LmmeDocument *doc = NULL;
    guint64 stale_revision = 0;

    g_assert_true(g_file_set_contents(original, "disk", -1, NULL));
    app.workspace = lmme_workspace_new(root);
    app.recovery_store = lmme_recovery_store_new(cache);
    doc = test_document_new(&app, original, "stale text");
    doc->recovery_failed = TRUE;
    stale_revision = doc->content_revision;
    doc->content_revision++;

    g_assert_false(lmme_document_write_recovery_snapshot(doc,
                                                         "stale text",
                                                         strlen("stale text"),
                                                         stale_revision,
                                                         &error));
    g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_AGAIN);
    g_assert_true(doc->recovery_failed);
    g_clear_error(&error);
    g_assert_true(lmme_document_flush_recovery(doc, &error));
    g_assert_no_error(error);
    g_assert_false(doc->recovery_failed);

    test_document_environment_clear(&app, doc, root, cache);
}

static void
test_recovery_failure_is_independent_of_document_state(void)
{
    LmmeDocument doc = {0};
    g_autofree char *relative_path = g_strdup("note.md");
    g_autofree char *status = NULL;

    doc.relative_path = relative_path;
    doc.recovery_failed = TRUE;
    doc.disk_state = LMME_DISK_STATE_EXTERNAL_CHANGED;
    g_assert_cmpstr(lmme_document_save_state_label(&doc), ==, "Conflict");
    status = lmme_statusbar_format_document(&doc, 12, 4, 350, FALSE);
    g_assert_cmpstr(status,
                    ==,
                    "note.md | Ln 12, Col 4 | 350 words | Conflict | Recovery failed | Source");
    g_clear_pointer(&status, g_free);
    doc.disk_state = LMME_DISK_STATE_EXTERNAL_DELETED;
    g_assert_cmpstr(lmme_document_save_state_label(&doc), ==, "Deleted");
    status = lmme_statusbar_format_document(&doc, 12, 4, 350, TRUE);
    g_assert_nonnull(strstr(status, "Deleted | Recovery failed | Editable Preview"));
    g_clear_pointer(&status, g_free);
    doc.disk_state = LMME_DISK_STATE_NORMAL;
    doc.save_state = LMME_SAVE_STATE_MODIFIED;
    g_assert_cmpstr(lmme_document_save_state_label(&doc), ==, "Modified");
    doc.save_state = LMME_SAVE_STATE_ERROR;
    g_assert_cmpstr(lmme_document_save_state_label(&doc), ==, "Error");
    g_assert_true(doc.recovery_failed);
}

static void
test_discard_close_disposition_is_committed_explicitly(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-document-close-discard-XXXXXX", NULL);
    g_autofree char *cache = g_build_filename(root, "recovery", NULL);
    g_autofree char *original = g_build_filename(root, "note.md", NULL);
    LmmeApp app = {0};
    LmmeDocument doc = {0};

    g_assert_true(g_file_set_contents(original, "disk", -1, NULL));
    app.recovery_store = lmme_recovery_store_new(cache);
    doc.app = &app;
    doc.path = original;
    g_assert_true(lmme_recovery_write(app.recovery_store,
                                      original,
                                      root,
                                      NULL,
                                      "local",
                                      5,
                                      NULL));

    doc.pending_close = LMME_PENDING_CLOSE_KEEP_RECOVERY;
    g_assert_true(lmme_tabs_test_commit_close_disposition(&doc, NULL));
    g_assert_cmpint(doc.pending_close, ==, LMME_PENDING_CLOSE_NONE);
    g_assert_true(lmme_recovery_exists(app.recovery_store, original));

    doc.pending_close = LMME_PENDING_CLOSE_DISCARD_LOCAL;
    g_assert_true(lmme_tabs_test_commit_close_disposition(&doc, NULL));
    g_assert_cmpint(doc.pending_close, ==, LMME_PENDING_CLOSE_NONE);
    g_assert_false(lmme_recovery_exists(app.recovery_store, original));

    lmme_recovery_store_free(app.recovery_store);
    remove_directory_contents(cache);
    g_rmdir(cache);
    g_unlink(original);
    g_rmdir(root);
}

static void
test_discard_close_failure_keeps_pending_close(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-document-discard-fail-XXXXXX", NULL);
    g_autofree char *cache = g_build_filename(root, "recovery", NULL);
    g_autofree char *original = g_build_filename(root, "note.md", NULL);
    g_autoptr(GError) error = NULL;
    LmmeApp app = {0};
    LmmeDocument doc = {0};

    g_assert_cmpint(g_mkdir(cache, 0700), ==, 0);
    g_assert_true(g_file_set_contents(original, "disk", -1, NULL));
    app.recovery_store = lmme_recovery_store_new(cache);
    doc.app = &app;
    doc.path = original;
    doc.relative_path = (char *)"note.md";
    g_assert_true(lmme_recovery_write(app.recovery_store, original, root, NULL, "local", 5, NULL));

    doc.pending_close = LMME_PENDING_CLOSE_DISCARD_LOCAL;
    lmme_recovery_test_fail_at(LMME_RECOVERY_TEST_FAIL_ACTIVE_PAYLOAD_UNLINK, 1);
    g_assert_false(lmme_tabs_test_commit_close_disposition(&doc, &error));
    g_assert_nonnull(error);
    g_assert_cmpint(doc.pending_close, ==, LMME_PENDING_CLOSE_DISCARD_LOCAL);
    lmme_recovery_test_reset();

    g_assert_true(lmme_tabs_test_commit_close_disposition(&doc, NULL));
    g_assert_cmpint(doc.pending_close, ==, LMME_PENDING_CLOSE_NONE);
    g_assert_false(lmme_recovery_exists(app.recovery_store, original));

    lmme_recovery_store_free(app.recovery_store);
    remove_directory_contents(cache);
    g_rmdir(cache);
    g_unlink(original);
    g_rmdir(root);
}

static void
test_bulk_close_commit_failure_keeps_all_documents(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-document-bulk-commit-XXXXXX", NULL);
    g_autofree char *cache = g_build_filename(root, "recovery", NULL);
    g_autofree char *first_path = g_build_filename(root, "first.md", NULL);
    g_autofree char *second_path = g_build_filename(root, "second.md", NULL);
    g_autoptr(GPtrArray) documents = NULL;
    g_autoptr(GError) error = NULL;
    LmmeApp app = {0};
    LmmeDocument *first = NULL;
    LmmeDocument *second = NULL;

    g_assert_cmpint(g_mkdir(cache, 0700), ==, 0);
    g_assert_true(g_file_set_contents(first_path, "one", -1, NULL));
    g_assert_true(g_file_set_contents(second_path, "two", -1, NULL));
    app.workspace = lmme_workspace_new(root);
    app.recovery_store = lmme_recovery_store_new(cache);
    app.documents = g_ptr_array_new();
    first = test_document_new(&app, first_path, "local one");
    second = test_document_new(&app, second_path, "local two");
    g_ptr_array_add(app.documents, first);
    g_ptr_array_add(app.documents, second);
    g_assert_true(lmme_document_flush_recovery(first, NULL));
    g_assert_true(lmme_document_flush_recovery(second, NULL));

    first->pending_close = LMME_PENDING_CLOSE_DISCARD_LOCAL;
    second->pending_close = LMME_PENDING_CLOSE_DISCARD_LOCAL;
    documents = g_ptr_array_new();
    g_ptr_array_add(documents, first);
    g_ptr_array_add(documents, second);

    lmme_recovery_test_fail_at(LMME_RECOVERY_TEST_FAIL_ACTIVE_PAYLOAD_UNLINK, 2);
    g_assert_false(lmme_tabs_commit_pending_dispositions(&app, &error));
    g_assert_nonnull(error);
    lmme_recovery_test_reset();

    g_assert_cmpuint(app.documents->len, ==, 2);
    g_assert_cmpint(first->pending_close, ==, LMME_PENDING_CLOSE_NONE);
    g_assert_cmpint(second->pending_close, ==, LMME_PENDING_CLOSE_DISCARD_LOCAL);
    g_assert_false(lmme_recovery_exists(app.recovery_store, first_path));
    g_assert_true(lmme_recovery_exists(app.recovery_store, second_path));

    lmme_document_free(second);
    g_ptr_array_remove_index(app.documents, 1);
    lmme_document_free(first);
    g_ptr_array_unref(app.documents);
    lmme_recovery_store_free(app.recovery_store);
    lmme_workspace_free(app.workspace);
    remove_directory_contents(cache);
    g_rmdir(cache);
    g_unlink(first_path);
    g_unlink(second_path);
    g_rmdir(root);
}

static void
test_external_states_consume_recovery_failure(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-document-external-recovery-XXXXXX", NULL);
    g_autofree char *cache = g_build_filename(root, "recovery", NULL);
    g_autofree char *original = g_build_filename(root, "note.md", NULL);
    g_autoptr(GError) error = NULL;
    LmmeApp app = {0};
    LmmeDocument *doc = NULL;

    g_assert_true(g_file_set_contents(original, "disk", -1, NULL));
    app.workspace = lmme_workspace_new(root);
    app.recovery_store = lmme_recovery_store_new(cache);
    doc = test_document_new(&app, original, "local");

    doc->disk_state = LMME_DISK_STATE_EXTERNAL_CHANGED;
    lmme_safe_write_test_fail_at(LMME_SAFE_WRITE_TEST_FAIL_TEMP_CREATE, 1);
    g_assert_false(lmme_document_flush_recovery(doc, &error));
    lmme_safe_write_test_reset();
    g_assert_nonnull(error);
    g_assert_true(doc->recovery_failed);
    g_assert_false(lmme_external_conflict_reload_allowed(TRUE, !doc->recovery_failed));
    g_clear_error(&error);

    g_assert_true(lmme_document_flush_recovery(doc, &error));
    g_assert_no_error(error);
    g_assert_false(doc->recovery_failed);
    g_assert_true(lmme_external_conflict_reload_allowed(TRUE, !doc->recovery_failed));

    doc->disk_state = LMME_DISK_STATE_EXTERNAL_DELETED;
    lmme_safe_write_test_fail_at(LMME_SAFE_WRITE_TEST_FAIL_TEMP_CREATE, 1);
    g_assert_false(lmme_document_flush_recovery(doc, &error));
    lmme_safe_write_test_reset();
    g_assert_nonnull(error);
    g_assert_true(doc->recovery_failed);
    g_assert_false(lmme_external_conflict_reload_allowed(FALSE, !doc->recovery_failed));
    g_clear_error(&error);

    test_document_environment_clear(&app, doc, root, cache);
}

static void
test_cursor_preview_update_does_not_full_parse(void)
{
    LmmeApp app = {0};
    LmmeDocument doc = {0};
    GtkTextIter second_line;

    app.preview_enabled = TRUE;
    doc.app = &app;
    doc.buffer = gtk_source_buffer_new(NULL);
    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(doc.buffer), "# First\n# Second\n", -1);
    g_assert_cmpint(lmme_preview_apply_editable_preview(GTK_TEXT_BUFFER(doc.buffer), TRUE, TRUE),
                    ==,
                    LMME_PREVIEW_APPLY_OK);
    doc.preview_dirty = FALSE;
    doc.preview_active_line_valid = TRUE;
    doc.preview_active_line = 0;
    doc.preview_full_parse_count = 1;
    gtk_text_buffer_get_iter_at_line(GTK_TEXT_BUFFER(doc.buffer), &second_line, 1);
    gtk_text_buffer_place_cursor(GTK_TEXT_BUFFER(doc.buffer), &second_line);
    lmme_document_update_preview_active_line(&doc);
    g_assert_cmpuint(doc.preview_full_parse_count, ==, 1);
    g_assert_cmpuint(doc.preview_active_line, ==, 1);

    g_object_unref(doc.buffer);
}

static char *
document_buffer_text_include_hidden(GtkTextBuffer *buffer)
{
    GtkTextIter start;
    GtkTextIter end;

    gtk_text_buffer_get_bounds(buffer, &start, &end);
    return gtk_text_buffer_get_text(buffer, &start, &end, TRUE);
}

static void
test_preview_table_lifecycle(void)
{
    gtk_init();
    g_autofree char *root = g_dir_make_tmp("lmme-test-preview-table-XXXXXX", NULL);
    g_autofree char *path = g_build_filename(root, "table.md", NULL);
    const char *input = "| A | B |\n|---|---|\n| 1 | 2 |\n";
    LmmeApp app = {0};
    LmmeDocument *doc = NULL;
    GtkWidget *window = NULL;
    GtkTextTagTable *tag_table = NULL;
    GtkTextTag *header_tag = NULL;
    g_autofree char *before = NULL;
    g_autofree char *after = NULL;
    GtkTextIter cursor;
    GtkTextIter sel_start;
    GtkTextIter sel_end;
    int cursor_offset = 0;
    int sel_start_offset = 0;
    int sel_end_offset = 0;

    g_assert_true(g_file_set_contents(path, input, -1, NULL));
    app.workspace = lmme_workspace_new(root);
    app.config.preview_hide_frontmatter = TRUE;
    doc = lmme_document_new(&app, path, input, "table.md");
    window = gtk_window_new();
    gtk_window_set_child(GTK_WINDOW(window), doc->scroller);
    gtk_widget_realize(window);

    {
        const char *cursor_ptr = strstr(input, "1");
        const char *sel_start_ptr = strstr(input, "| 1");
        const char *sel_end_ptr = strstr(input, "2 |");
        int cursor_char = 0;
        int sel_start_char = 0;
        int sel_end_char = 0;

        g_assert_nonnull(cursor_ptr);
        g_assert_nonnull(sel_start_ptr);
        g_assert_nonnull(sel_end_ptr);
        cursor_char = (int)g_utf8_strlen(input, (gssize)(cursor_ptr - input));
        sel_start_char = (int)g_utf8_strlen(input, (gssize)(sel_start_ptr - input));
        sel_end_char = (int)g_utf8_strlen(input, (gssize)(sel_end_ptr - input));

        gtk_text_buffer_get_iter_at_offset(GTK_TEXT_BUFFER(doc->buffer), &cursor, cursor_char);
        gtk_text_buffer_place_cursor(GTK_TEXT_BUFFER(doc->buffer), &cursor);
        gtk_text_buffer_get_iter_at_offset(GTK_TEXT_BUFFER(doc->buffer), &sel_start, sel_start_char);
        gtk_text_buffer_get_iter_at_offset(GTK_TEXT_BUFFER(doc->buffer), &sel_end, sel_end_char);
        gtk_text_buffer_select_range(GTK_TEXT_BUFFER(doc->buffer), &sel_start, &sel_end);
    }

    doc->modified = TRUE;
    gtk_text_buffer_set_modified(GTK_TEXT_BUFFER(doc->buffer), TRUE);
    doc->save_state = LMME_SAVE_STATE_MODIFIED;

    gtk_text_buffer_get_iter_at_mark(GTK_TEXT_BUFFER(doc->buffer),
                                     &cursor,
                                     gtk_text_buffer_get_insert(GTK_TEXT_BUFFER(doc->buffer)));
    cursor_offset = gtk_text_iter_get_offset(&cursor);
    gtk_text_buffer_get_selection_bounds(GTK_TEXT_BUFFER(doc->buffer), &sel_start, &sel_end);
    sel_start_offset = gtk_text_iter_get_offset(&sel_start);
    sel_end_offset = gtk_text_iter_get_offset(&sel_end);

    before = document_buffer_text_include_hidden(GTK_TEXT_BUFFER(doc->buffer));
    g_assert_cmpint(lmme_document_set_preview_visible(doc, TRUE), ==, LMME_PREVIEW_APPLY_OK);

    after = document_buffer_text_include_hidden(GTK_TEXT_BUFFER(doc->buffer));
    g_assert_cmpstr(after, ==, before);

    gtk_text_buffer_get_iter_at_mark(GTK_TEXT_BUFFER(doc->buffer),
                                     &cursor,
                                     gtk_text_buffer_get_insert(GTK_TEXT_BUFFER(doc->buffer)));
    g_assert_cmpint(gtk_text_iter_get_offset(&cursor), ==, cursor_offset);
    g_assert_true(gtk_text_buffer_get_selection_bounds(GTK_TEXT_BUFFER(doc->buffer), &sel_start, &sel_end));
    g_assert_cmpint(gtk_text_iter_get_offset(&sel_start), ==, sel_start_offset);
    g_assert_cmpint(gtk_text_iter_get_offset(&sel_end), ==, sel_end_offset);
    g_assert_true(gtk_text_buffer_get_modified(GTK_TEXT_BUFFER(doc->buffer)));
    g_assert_true(doc->modified);
    g_assert_cmpint(doc->save_state, ==, LMME_SAVE_STATE_MODIFIED);

    tag_table = gtk_text_buffer_get_tag_table(GTK_TEXT_BUFFER(doc->buffer));
    header_tag = gtk_text_tag_table_lookup(tag_table, "lmme-preview-table-header-row");
    g_assert_nonnull(header_tag);
    g_assert_cmpint(lmme_document_set_preview_visible(doc, TRUE), ==, LMME_PREVIEW_APPLY_OK);
    g_assert_true(header_tag == gtk_text_tag_table_lookup(tag_table, "lmme-preview-table-header-row"));

    g_assert_cmpint(lmme_document_set_preview_visible(doc, FALSE), ==, LMME_PREVIEW_APPLY_OK);
    gtk_text_buffer_get_iter_at_line(GTK_TEXT_BUFFER(doc->buffer), &cursor, 2);
    g_assert_false(gtk_text_iter_has_tag(&cursor,
                                         gtk_text_tag_table_lookup(tag_table, "lmme-preview-table-body-row")));
    g_free(after);
    after = document_buffer_text_include_hidden(GTK_TEXT_BUFFER(doc->buffer));
    g_assert_cmpstr(after, ==, before);

    gtk_text_buffer_insert_at_cursor(GTK_TEXT_BUFFER(doc->buffer), "x", 1);
    g_assert_cmpint(lmme_document_set_preview_visible(doc, TRUE), ==, LMME_PREVIEW_APPLY_OK);
    gtk_text_buffer_undo(GTK_TEXT_BUFFER(doc->buffer));
    g_free(after);
    after = document_buffer_text_include_hidden(GTK_TEXT_BUFFER(doc->buffer));
    g_assert_cmpstr(after, ==, before);

    lmme_document_free(doc);
    gtk_window_destroy(GTK_WINDOW(window));
    lmme_workspace_free(app.workspace);
    g_unlink(path);
    g_rmdir(root);
}

static void
test_recovered_document_saves_to_original(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-document-XXXXXX", NULL);
    g_autofree char *cache = g_build_filename(root, "recovery", NULL);
    g_autofree char *original = g_build_filename(root, "note.md", NULL);
    g_autofree char *contents = NULL;
    g_autoptr(GPtrArray) entries = NULL;
    LmmeApp app = {0};
    LmmeDocument doc = {0};

    g_assert_true(g_file_set_contents(original, "disk", -1, NULL));
    app.workspace = lmme_workspace_new(root);
    app.recovery_store = lmme_recovery_store_new(cache);
    doc.app = &app;
    doc.path = g_strdup(original);
    doc.relative_path = g_strdup("note.md");
    doc.buffer = gtk_source_buffer_new(NULL);
    doc.modified = TRUE;
    doc.restored_from_recovery = TRUE;
    doc.save_state = LMME_SAVE_STATE_MODIFIED;
    doc.disk_state = LMME_DISK_STATE_NORMAL;
    g_assert_true(lmme_file_fingerprint_read(original, &doc.base_fingerprint, NULL));
    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(doc.buffer), "recovered text", -1);
    g_assert_true(lmme_recovery_write(app.recovery_store,
                                      original,
                                      root,
                                      &doc.base_fingerprint,
                                      "recovered text",
                                      14,
                                      NULL));

    g_assert_cmpint(lmme_document_save(&doc, NULL),
                    ==,
                    LMME_DOCUMENT_SAVE_COMMITTED_DURABLE);
    g_assert_true(g_file_get_contents(original, &contents, NULL, NULL));
    g_assert_cmpstr(contents, ==, "recovered text");
    entries = lmme_recovery_list(app.recovery_store, NULL);
    g_assert_cmpuint(entries->len, ==, 0);

    g_object_unref(doc.buffer);
    g_free(doc.path);
    g_free(doc.relative_path);
    lmme_recovery_store_free(app.recovery_store);
    lmme_workspace_free(app.workspace);
    remove_directory_contents(cache);
    g_rmdir(cache);
    g_remove(original);
    g_rmdir(root);
}

static void
test_save_precommit_failure_keeps_document_state(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-document-save-XXXXXX", NULL);
    g_autofree char *cache = g_build_filename(root, "recovery", NULL);
    g_autofree char *original = g_build_filename(root, "note.md", NULL);
    g_autofree char *contents = NULL;
    g_autoptr(GError) error = NULL;
    LmmeApp app = {0};
    LmmeDocument *doc = NULL;
    LmmeFileFingerprint old_fingerprint;

    g_assert_true(g_file_set_contents(original, "old", -1, NULL));
    app.workspace = lmme_workspace_new(root);
    app.recovery_store = lmme_recovery_store_new(cache);
    doc = test_document_new(&app, original, "new text");
    old_fingerprint = doc->base_fingerprint;
    g_assert_true(lmme_document_flush_recovery(doc, NULL));

    lmme_safe_write_test_fail_at(LMME_SAFE_WRITE_TEST_FAIL_RENAME, 1);
    g_assert_cmpint(lmme_document_save(doc, &error),
                    ==,
                    LMME_DOCUMENT_SAVE_NOT_COMMITTED);
    lmme_safe_write_test_reset();

    g_assert_nonnull(error);
    g_assert_cmpstr(doc->path, ==, original);
    g_assert_true(doc->modified);
    g_assert_true(lmme_file_fingerprint_equal(&doc->base_fingerprint, &old_fingerprint));
    g_assert_true(g_file_get_contents(original, &contents, NULL, NULL));
    g_assert_cmpstr(contents, ==, "old");
    g_assert_true(lmme_recovery_exists(app.recovery_store, original));

    test_document_environment_clear(&app, doc, root, cache);
}

static void
test_save_uncertain_commit_keeps_recovery(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-document-uncertain-XXXXXX", NULL);
    g_autofree char *cache = g_build_filename(root, "recovery", NULL);
    g_autofree char *original = g_build_filename(root, "note.md", NULL);
    g_autofree char *contents = NULL;
    g_autofree char *recovery_contents = NULL;
    g_autoptr(GPtrArray) entries = NULL;
    g_autoptr(GError) error = NULL;
    LmmeApp app = {0};
    LmmeDocument *doc = NULL;

    g_assert_true(g_file_set_contents(original, "old", -1, NULL));
    app.workspace = lmme_workspace_new(root);
    app.recovery_store = lmme_recovery_store_new(cache);
    doc = test_document_new(&app, original, "new text");

    lmme_safe_write_test_fail_at(LMME_SAFE_WRITE_TEST_FAIL_DIRECTORY_FSYNC, 1);
    g_assert_cmpint(lmme_document_save(doc, &error),
                    ==,
                    LMME_DOCUMENT_SAVE_COMMITTED_NOT_DURABLE);
    lmme_safe_write_test_reset();

    g_assert_nonnull(error);
    g_assert_true(g_file_get_contents(original, &contents, NULL, NULL));
    g_assert_cmpstr(contents, ==, "new text");
    g_assert_true(doc->has_expected_internal_fingerprint);
    g_assert_true(doc->modified);
    g_assert_cmpint(doc->save_state, ==, LMME_SAVE_STATE_ERROR);
    g_assert_false(doc->recovery_failed);
    entries = lmme_recovery_list(app.recovery_store, NULL);
    g_assert_cmpuint(entries->len, ==, 1);
    g_assert_true(g_file_get_contents(((LmmeRecoveryEntry *)g_ptr_array_index(entries, 0))->recovery_path,
                                      &recovery_contents,
                                      NULL,
                                      NULL));
    g_assert_cmpstr(recovery_contents, ==, "new text");

    g_ptr_array_unref(g_steal_pointer(&entries));
    test_document_environment_clear(&app, doc, root, cache);
}

static void
test_durable_save_cleanup_failure_class_a_keeps_recoverable_payload(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-document-cleanup-a-XXXXXX", NULL);
    g_autofree char *cache = g_build_filename(root, "recovery", NULL);
    g_autofree char *original = g_build_filename(root, "note.md", NULL);
    g_autofree char *hash = lmme_hash_path(original);
    g_autofree char *previous_name = g_strdup_printf("%s-previous.recover", hash);
    g_autofree char *previous_path = g_build_filename(cache, previous_name, NULL);
    g_autoptr(GPtrArray) entries = NULL;
    LmmeApp app = {0};
    LmmeDocument *doc = NULL;

    g_assert_true(g_file_set_contents(original, "disk", -1, NULL));
    g_assert_cmpint(g_mkdir(cache, 0700), ==, 0);
    g_assert_true(g_file_set_contents(previous_path, "old", -1, NULL));
    app.workspace = lmme_workspace_new(root);
    app.recovery_store = lmme_recovery_store_new(cache);
    doc = test_document_new(&app, original, "new text");
    g_assert_true(lmme_document_flush_recovery(doc, NULL));

    lmme_recovery_test_fail_at(LMME_RECOVERY_TEST_FAIL_INACTIVE_GENERATION_UNLINK, 1);
    g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*Could not remove recovery data after save*");
    g_assert_cmpint(lmme_document_save(doc, NULL),
                    ==,
                    LMME_DOCUMENT_SAVE_COMMITTED_DURABLE);
    g_test_assert_expected_messages();
    lmme_recovery_test_reset();

    g_assert_true(doc->recovery_cleanup_failed);
    g_assert_false(doc->recovery_failed);
    g_assert_false(doc->restored_from_recovery);
    g_assert_cmpint(doc->save_state, ==, LMME_SAVE_STATE_SAVED);
    entries = lmme_recovery_list(app.recovery_store, NULL);
    g_assert_cmpuint(entries->len, ==, 1);
    g_assert_true(g_file_test(previous_path, G_FILE_TEST_IS_REGULAR));

    test_document_environment_clear(&app, doc, root, cache);
}

static void
test_durable_save_cleanup_failure_class_b_leaves_stale_metadata_only(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-document-cleanup-b-XXXXXX", NULL);
    g_autofree char *cache = g_build_filename(root, "recovery", NULL);
    g_autofree char *original = g_build_filename(root, "note.md", NULL);
    g_autoptr(GPtrArray) entries = NULL;
    LmmeApp app = {0};
    LmmeDocument *doc = NULL;

    g_assert_true(g_file_set_contents(original, "disk", -1, NULL));
    app.workspace = lmme_workspace_new(root);
    app.recovery_store = lmme_recovery_store_new(cache);
    doc = test_document_new(&app, original, "new text");
    g_assert_true(lmme_document_flush_recovery(doc, NULL));

    lmme_recovery_test_fail_at(LMME_RECOVERY_TEST_FAIL_METADATA_UNLINK, 1);
    g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*Could not remove recovery data after save*");
    g_assert_cmpint(lmme_document_save(doc, NULL),
                    ==,
                    LMME_DOCUMENT_SAVE_COMMITTED_DURABLE);
    g_test_assert_expected_messages();
    lmme_recovery_test_reset();

    g_assert_true(doc->recovery_cleanup_failed);
    g_assert_false(doc->recovery_failed);
    g_assert_false(doc->restored_from_recovery);
    g_assert_cmpint(doc->save_state, ==, LMME_SAVE_STATE_SAVED);
    entries = lmme_recovery_list(app.recovery_store, NULL);
    g_assert_cmpuint(entries->len, ==, 0);

    test_document_environment_clear(&app, doc, root, cache);
}

static void
test_recovery_cleanup_failure_status_label(void)
{
    LmmeDocument doc = {0};
    g_autofree char *relative_path = g_strdup("note.md");
    g_autofree char *status = NULL;

    doc.relative_path = relative_path;
    doc.save_state = LMME_SAVE_STATE_SAVED;
    doc.recovery_cleanup_failed = TRUE;
    status = lmme_statusbar_format_document(&doc, 3, 1, 10, FALSE);
    g_assert_nonnull(strstr(status, "Saved | Recovery cleanup failed | Source"));
}

static void
test_save_as_is_transactional(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-document-save-as-XXXXXX", NULL);
    g_autofree char *cache = g_build_filename(root, "recovery", NULL);
    g_autofree char *original = g_build_filename(root, "old.md", NULL);
    g_autofree char *target = g_build_filename(root, "new.md", NULL);
    g_autofree char *contents = NULL;
    g_autoptr(GError) error = NULL;
    LmmeApp app = {0};
    LmmeDocument *doc = NULL;

    g_assert_true(g_file_set_contents(original, "old", -1, NULL));
    app.workspace = lmme_workspace_new(root);
    app.recovery_store = lmme_recovery_store_new(cache);
    doc = test_document_new(&app, original, "save as text");
    g_assert_true(lmme_document_flush_recovery(doc, NULL));

    lmme_safe_write_test_fail_at(LMME_SAFE_WRITE_TEST_FAIL_RENAME, 1);
    g_assert_cmpint(lmme_document_save_as(doc, target, &error),
                    ==,
                    LMME_DOCUMENT_SAVE_NOT_COMMITTED);
    lmme_safe_write_test_reset();
    g_assert_cmpstr(doc->path, ==, original);
    g_assert_false(g_file_test(target, G_FILE_TEST_EXISTS));
    g_clear_error(&error);

    g_assert_cmpint(lmme_document_save_as(doc, target, &error),
                    ==,
                    LMME_DOCUMENT_SAVE_COMMITTED_DURABLE);
    g_assert_no_error(error);
    g_assert_cmpstr(doc->path, ==, target);
    g_assert_cmpstr(doc->relative_path, ==, "new.md");
    g_assert_false(doc->modified);
    g_assert_nonnull(doc->monitor);
    g_assert_true(g_file_get_contents(target, &contents, NULL, NULL));
    g_assert_cmpstr(contents, ==, "save as text");
    g_assert_false(lmme_recovery_exists(app.recovery_store, original));

    test_document_environment_clear(&app, doc, root, cache);
}

static void
test_overwrite_uses_explicit_conflict_policy(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-document-overwrite-XXXXXX", NULL);
    g_autofree char *cache = g_build_filename(root, "recovery", NULL);
    g_autofree char *original = g_build_filename(root, "note.md", NULL);
    g_autofree char *contents = NULL;
    g_autoptr(GError) error = NULL;
    LmmeApp app = {0};
    LmmeDocument *doc = NULL;

    g_assert_true(g_file_set_contents(original, "external", -1, NULL));
    app.workspace = lmme_workspace_new(root);
    app.recovery_store = lmme_recovery_store_new(cache);
    doc = test_document_new(&app, original, "local");
    doc->disk_state = LMME_DISK_STATE_EXTERNAL_CHANGED;

    g_assert_cmpint(lmme_document_save(doc, &error),
                    ==,
                    LMME_DOCUMENT_SAVE_NOT_COMMITTED);
    g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_AGAIN);
    g_assert_cmpint(doc->disk_state, ==, LMME_DISK_STATE_EXTERNAL_CHANGED);
    g_clear_error(&error);

    g_assert_cmpint(lmme_document_overwrite(doc, &error),
                    ==,
                    LMME_DOCUMENT_SAVE_COMMITTED_DURABLE);
    g_assert_no_error(error);
    g_assert_cmpint(doc->disk_state, ==, LMME_DISK_STATE_NORMAL);
    g_assert_true(g_file_get_contents(original, &contents, NULL, NULL));
    g_assert_cmpstr(contents, ==, "local");

    test_document_environment_clear(&app, doc, root, cache);
}

static void
test_save_as_uncertain_commit_switches_path_with_recovery(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-document-save-as-uncertain-XXXXXX", NULL);
    g_autofree char *cache = g_build_filename(root, "recovery", NULL);
    g_autofree char *original = g_build_filename(root, "old.md", NULL);
    g_autofree char *target = g_build_filename(root, "new.md", NULL);
    g_autofree char *contents = NULL;
    g_autoptr(GError) error = NULL;
    LmmeApp app = {0};
    LmmeDocument *doc = NULL;

    g_assert_true(g_file_set_contents(original, "old", -1, NULL));
    app.workspace = lmme_workspace_new(root);
    app.recovery_store = lmme_recovery_store_new(cache);
    doc = test_document_new(&app, original, "uncertain save as");
    g_assert_true(lmme_document_flush_recovery(doc, NULL));

    lmme_safe_write_test_fail_at(LMME_SAFE_WRITE_TEST_FAIL_DIRECTORY_OPEN, 1);
    g_assert_cmpint(lmme_document_save_as(doc, target, &error),
                    ==,
                    LMME_DOCUMENT_SAVE_COMMITTED_NOT_DURABLE);
    lmme_safe_write_test_reset();

    g_assert_nonnull(error);
    g_assert_cmpstr(doc->path, ==, target);
    g_assert_cmpstr(doc->relative_path, ==, "new.md");
    g_assert_true(doc->modified);
    g_assert_nonnull(doc->monitor);
    g_assert_true(g_file_get_contents(target, &contents, NULL, NULL));
    g_assert_cmpstr(contents, ==, "uncertain save as");
    g_assert_true(lmme_recovery_exists(app.recovery_store, target));
    g_assert_false(lmme_recovery_exists(app.recovery_store, original));

    test_document_environment_clear(&app, doc, root, cache);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/document/recovery-save-original", test_recovered_document_saves_to_original);
    g_test_add_func("/document/save/precommit-failure", test_save_precommit_failure_keeps_document_state);
    g_test_add_func("/document/save/uncertain", test_save_uncertain_commit_keeps_recovery);
    g_test_add_func("/document/save/cleanup-failure-class-a",
                    test_durable_save_cleanup_failure_class_a_keeps_recoverable_payload);
    g_test_add_func("/document/save/cleanup-failure-class-b",
                    test_durable_save_cleanup_failure_class_b_leaves_stale_metadata_only);
    g_test_add_func("/document/recovery/cleanup-failure-status",
                    test_recovery_cleanup_failure_status_label);
    g_test_add_func("/document/save-as/transactional", test_save_as_is_transactional);
    g_test_add_func("/document/save-as/uncertain", test_save_as_uncertain_commit_switches_path_with_recovery);
    g_test_add_func("/document/overwrite/explicit-policy", test_overwrite_uses_explicit_conflict_policy);
    g_test_add_func("/document/conflict-blocks-autosave", test_conflict_blocks_autosave);
    g_test_add_func("/document/bulk-close/two-phase", test_bulk_close_prepare_is_all_or_nothing);
    g_test_add_func("/document/recovery/failure-retry", test_recovery_failure_state_and_retry);
    g_test_add_func("/document/recovery/stale-snapshot",
                    test_stale_recovery_snapshot_does_not_clear_failure);
    g_test_add_func("/document/recovery/composite-state",
                    test_recovery_failure_is_independent_of_document_state);
    g_test_add_func("/document/close/discard-commit",
                    test_discard_close_disposition_is_committed_explicitly);
    g_test_add_func("/document/close/discard-failure",
                    test_discard_close_failure_keeps_pending_close);
    g_test_add_func("/document/close/bulk-commit-failure",
                    test_bulk_close_commit_failure_keeps_all_documents);
    g_test_add_func("/document/recovery/external-states",
                    test_external_states_consume_recovery_failure);
    g_test_add_func("/document/preview-cursor-incremental", test_cursor_preview_update_does_not_full_parse);
    g_test_add_func("/document/preview-table-lifecycle", test_preview_table_lifecycle);
    return g_test_run();
}
