#include <glib.h>
#include <glib/gstdio.h>
#include <gtksourceview/gtksource.h>

#include "app/app.h"
#include "document/document.h"
#include "document/document_autosave.h"
#include "document/recovery.h"
#include "document/tabs_test.h"
#include "editor/preview.h"
#include "infra/safe_write_test.h"
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
    (void)doc;

    state->calls++;
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

    state = (PrepareCloseState){0};
    g_assert_true(lmme_tabs_test_prepare_documents(documents,
                                                   prepare_close_for_test,
                                                   &state));
    g_assert_cmpuint(state.calls, ==, 3);
    g_assert_cmpuint(documents->len, ==, 3);
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
    g_test_add_func("/document/save-as/transactional", test_save_as_is_transactional);
    g_test_add_func("/document/save-as/uncertain", test_save_as_uncertain_commit_switches_path_with_recovery);
    g_test_add_func("/document/overwrite/explicit-policy", test_overwrite_uses_explicit_conflict_policy);
    g_test_add_func("/document/conflict-blocks-autosave", test_conflict_blocks_autosave);
    g_test_add_func("/document/bulk-close/two-phase", test_bulk_close_prepare_is_all_or_nothing);
    g_test_add_func("/document/preview-cursor-incremental", test_cursor_preview_update_does_not_full_parse);
    return g_test_run();
}
