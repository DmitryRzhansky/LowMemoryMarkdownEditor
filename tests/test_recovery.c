#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include "document/recovery.h"
#include "infra/util.h"

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
test_recovery_roundtrip(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-recovery-XXXXXX", NULL);
    g_autofree char *cache = g_build_filename(root, "cache", NULL);
    g_autofree char *workspace = g_build_filename(root, "workspace", NULL);
    g_autofree char *original = g_build_filename(workspace, "note.md", NULL);
    g_autoptr(GPtrArray) entries = NULL;
    LmmeRecoveryStore *store = NULL;

    g_assert_cmpint(g_mkdir(workspace, 0700), ==, 0);
    g_assert_true(g_file_set_contents(original, "disk", -1, NULL));
    store = lmme_recovery_store_new(cache);
    g_assert_nonnull(store);
    g_assert_true(lmme_recovery_write(store, original, workspace, NULL, "unsaved", 7, NULL));

    entries = lmme_recovery_list(store, NULL);
    g_assert_nonnull(entries);
    g_assert_cmpuint(entries->len, ==, 1);
    LmmeRecoveryEntry *entry = g_ptr_array_index(entries, 0);
    g_assert_cmpstr(entry->original_path, ==, original);
    g_assert_cmpstr(entry->workspace_path, ==, workspace);
    g_assert_false(entry->legacy);
    g_assert_false(lmme_recovery_entry_original_changed(entry, NULL));

    g_assert_true(lmme_recovery_remove(store, original, NULL));
    g_ptr_array_unref(g_steal_pointer(&entries));
    entries = lmme_recovery_list(store, NULL);
    g_assert_cmpuint(entries->len, ==, 0);

    lmme_recovery_store_free(store);
    remove_directory_contents(cache);
    g_rmdir(cache);
    g_remove(original);
    g_rmdir(workspace);
    g_rmdir(root);
}

static void
test_original_change_is_detected(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-recovery-XXXXXX", NULL);
    g_autofree char *cache = g_build_filename(root, "cache", NULL);
    g_autofree char *original = g_build_filename(root, "note.md", NULL);
    g_autoptr(GPtrArray) entries = NULL;
    LmmeRecoveryStore *store = lmme_recovery_store_new(cache);

    g_assert_true(g_file_set_contents(original, "disk", -1, NULL));
    g_assert_true(lmme_recovery_write(store, original, root, NULL, "unsaved", 7, NULL));
    g_assert_true(g_file_set_contents(original, "changed and larger", -1, NULL));
    entries = lmme_recovery_list(store, NULL);
    g_assert_cmpuint(entries->len, ==, 1);
    g_assert_true(lmme_recovery_entry_original_changed(g_ptr_array_index(entries, 0), NULL));

    lmme_recovery_store_free(store);
    remove_directory_contents(cache);
    g_rmdir(cache);
    g_remove(original);
    g_rmdir(root);
}

static void
test_missing_original_and_workspace_filter(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-recovery-XXXXXX", NULL);
    g_autofree char *cache = g_build_filename(root, "cache", NULL);
    g_autofree char *workspace = g_build_filename(root, "workspace", NULL);
    g_autofree char *other_workspace = g_build_filename(root, "other", NULL);
    g_autofree char *original = g_build_filename(workspace, "missing.md", NULL);
    g_autoptr(GPtrArray) entries = NULL;
    LmmeRecoveryStore *store = NULL;

    g_assert_cmpint(g_mkdir(workspace, 0700), ==, 0);
    g_assert_cmpint(g_mkdir(other_workspace, 0700), ==, 0);
    store = lmme_recovery_store_new(cache);
    g_assert_true(lmme_recovery_write(store, original, workspace, NULL, "unsaved", 7, NULL));
    entries = lmme_recovery_list(store, NULL);
    g_assert_cmpuint(entries->len, ==, 1);
    LmmeRecoveryEntry *entry = g_ptr_array_index(entries, 0);
    g_assert_true(lmme_recovery_entry_original_changed(entry, NULL));
    g_assert_true(lmme_recovery_entry_belongs_to_workspace(entry, workspace));
    g_assert_false(lmme_recovery_entry_belongs_to_workspace(entry, other_workspace));

    lmme_recovery_remove(store, original, NULL);
    lmme_recovery_store_free(store);
    g_rmdir(cache);
    g_rmdir(workspace);
    g_rmdir(other_workspace);
    g_rmdir(root);
}

static void
test_legacy_and_corrupt_metadata(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-recovery-XXXXXX", NULL);
    g_autofree char *cache = g_build_filename(root, "cache", NULL);
    g_autofree char *original = g_build_filename(root, "legacy.md", NULL);
    g_autofree char *hash = lmme_hash_path(original);
    g_autofree char *legacy_recovery_name = g_strdup_printf("%s.recover", hash);
    g_autofree char *legacy_path_name = g_strdup_printf("%s.path", hash);
    g_autofree char *legacy_recovery = g_build_filename(cache, legacy_recovery_name, NULL);
    g_autofree char *legacy_path = g_build_filename(cache, legacy_path_name, NULL);
    g_autofree char *bad_meta = g_build_filename(cache, "broken.meta", NULL);
    g_autoptr(GPtrArray) entries = NULL;
    LmmeRecoveryStore *store = lmme_recovery_store_new(cache);

    g_assert_cmpint(g_mkdir(cache, 0700), ==, 0);
    g_assert_true(g_file_set_contents(legacy_recovery, "legacy", -1, NULL));
    g_assert_true(g_file_set_contents(legacy_path, original, -1, NULL));
    g_assert_true(g_file_set_contents(bad_meta, "not valid metadata", -1, NULL));

    entries = lmme_recovery_list(store, NULL);
    g_assert_cmpuint(entries->len, ==, 1);
    LmmeRecoveryEntry *entry = g_ptr_array_index(entries, 0);
    g_assert_true(entry->legacy);
    g_assert_cmpstr(entry->original_path, ==, original);

    lmme_recovery_store_free(store);
    remove_directory_contents(cache);
    g_rmdir(cache);
    g_rmdir(root);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/recovery/roundtrip", test_recovery_roundtrip);
    g_test_add_func("/recovery/original-change", test_original_change_is_detected);
    g_test_add_func("/recovery/missing-workspace-filter", test_missing_original_and_workspace_filter);
    g_test_add_func("/recovery/legacy-corrupt", test_legacy_and_corrupt_metadata);
    return g_test_run();
}
