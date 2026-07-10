#include <glib.h>
#include <glib/gstdio.h>
#include <gtksourceview/gtksource.h>

#include "app/app.h"
#include "document/document.h"
#include "document/document_autosave.h"
#include "document/recovery.h"
#include "editor/preview.h"
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

    g_assert_true(lmme_document_save(&doc, NULL));
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

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/document/recovery-save-original", test_recovered_document_saves_to_original);
    g_test_add_func("/document/conflict-blocks-autosave", test_conflict_blocks_autosave);
    g_test_add_func("/document/preview-cursor-incremental", test_cursor_preview_update_does_not_full_parse);
    return g_test_run();
}
