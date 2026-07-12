#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>

#include "document/recovery.h"
#include "infra/safe_write_test.h"
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

static guint
count_generation_files(const char *directory)
{
    g_autoptr(GDir) dir = g_dir_open(directory, 0, NULL);
    const char *name = NULL;
    guint count = 0;

    while (dir != NULL && (name = g_dir_read_name(dir)) != NULL) {
        if (strlen(name) > 64 && name[64] == '-' && g_str_has_suffix(name, ".recover")) {
            count++;
        }
    }
    return count;
}

static void
write_version_1_metadata(const char *cache,
                         const char *original,
                         const char *workspace)
{
    g_autofree char *hash = lmme_hash_path(original);
    g_autofree char *metadata_name = g_strdup_printf("%s.meta", hash);
    g_autofree char *metadata_path = g_build_filename(cache, metadata_name, NULL);
    g_autoptr(GKeyFile) metadata = g_key_file_new();
    LmmeFileFingerprint fingerprint = {0};

    g_assert_true(lmme_file_fingerprint_read(original, &fingerprint, NULL));
    g_key_file_set_integer(metadata, "recovery", "version", 1);
    g_key_file_set_string(metadata, "recovery", "original_path", original);
    g_key_file_set_string(metadata, "recovery", "workspace_path", workspace);
    g_key_file_set_int64(metadata, "recovery", "created_us", 1);
    g_key_file_set_boolean(metadata, "original", "exists", fingerprint.exists);
    g_key_file_set_uint64(metadata, "original", "size", fingerprint.size);
    g_key_file_set_int64(metadata, "original", "mtime_ns", fingerprint.mtime_ns);
    g_key_file_set_uint64(metadata, "original", "inode", fingerprint.inode);
    g_key_file_set_uint64(metadata, "original", "device", fingerprint.device);
    g_assert_true(g_key_file_save_to_file(metadata, metadata_path, NULL));
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
    struct stat cache_info;

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
    g_assert_cmpuint(count_generation_files(cache), ==, 1);
    g_assert_cmpint(g_stat(cache, &cache_info), ==, 0);
    g_assert_cmpuint(cache_info.st_mode & 0777, ==, 0700);

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
test_version_1_metadata_is_loaded_and_migrated(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-test-recovery-v1-XXXXXX", NULL);
    g_autofree char *cache = g_build_filename(root, "cache", NULL);
    g_autofree char *original = g_build_filename(root, "note.md", NULL);
    g_autofree char *hash = lmme_hash_path(original);
    g_autofree char *recovery_name = g_strdup_printf("%s.recover", hash);
    g_autofree char *recovery_path = g_build_filename(cache, recovery_name, NULL);
    g_autoptr(GPtrArray) entries = NULL;
    LmmeRecoveryStore *store = NULL;

    g_assert_cmpint(g_mkdir(cache, 0700), ==, 0);
    g_assert_true(g_file_set_contents(original, "disk", -1, NULL));
    g_assert_true(g_file_set_contents(recovery_path, "version one", -1, NULL));
    write_version_1_metadata(cache, original, root);
    store = lmme_recovery_store_new(cache);

    entries = lmme_recovery_list(store, NULL);
    g_assert_cmpuint(entries->len, ==, 1);
    g_assert_cmpstr(((LmmeRecoveryEntry *)g_ptr_array_index(entries, 0))->recovery_path,
                    ==,
                    recovery_path);
    g_ptr_array_unref(g_steal_pointer(&entries));

    g_assert_true(lmme_recovery_write(store, original, root, NULL, "version two", 11, NULL));
    g_assert_false(g_file_test(recovery_path, G_FILE_TEST_EXISTS));
    entries = lmme_recovery_list(store, NULL);
    g_assert_cmpuint(entries->len, ==, 1);
    g_assert_cmpuint(count_generation_files(cache), ==, 1);

    lmme_recovery_store_free(store);
    remove_directory_contents(cache);
    g_rmdir(cache);
    g_unlink(original);
    g_rmdir(root);
}

static void
test_recovery_update_faults_preserve_usable_data(void)
{
    static const LmmeSafeWriteTestFault faults[] = {
        LMME_SAFE_WRITE_TEST_FAIL_TEMP_CREATE,
        LMME_SAFE_WRITE_TEST_FAIL_FCHMOD,
        LMME_SAFE_WRITE_TEST_FAIL_WRITE,
        LMME_SAFE_WRITE_TEST_FAIL_FILE_FSYNC,
        LMME_SAFE_WRITE_TEST_FAIL_CLOSE,
        LMME_SAFE_WRITE_TEST_FAIL_RENAME,
        LMME_SAFE_WRITE_TEST_FAIL_DIRECTORY_OPEN,
        LMME_SAFE_WRITE_TEST_FAIL_DIRECTORY_FSYNC,
    };

    for (guint write_invocation = 1; write_invocation <= 2; write_invocation++) {
        for (guint i = 0; i < G_N_ELEMENTS(faults); i++) {
            g_autofree char *root = g_dir_make_tmp("lmme-test-recovery-fault-XXXXXX", NULL);
            g_autofree char *cache = g_build_filename(root, "cache", NULL);
            g_autofree char *original = g_build_filename(root, "note.md", NULL);
            g_autofree char *previous_path = NULL;
            g_autofree char *active_contents = NULL;
            g_autoptr(GPtrArray) entries = NULL;
            g_autoptr(GError) error = NULL;
            LmmeRecoveryStore *store = lmme_recovery_store_new(cache);
            gboolean metadata_post_commit = write_invocation == 2 &&
                                                (faults[i] == LMME_SAFE_WRITE_TEST_FAIL_DIRECTORY_OPEN ||
                                                 faults[i] == LMME_SAFE_WRITE_TEST_FAIL_DIRECTORY_FSYNC);

            g_assert_true(g_file_set_contents(original, "disk", -1, NULL));
            g_assert_true(lmme_recovery_write(store, original, root, NULL, "old recovery", 12, NULL));
            previous_path = lmme_recovery_path_for_original(store, original);
            g_assert_true(g_file_test(previous_path, G_FILE_TEST_IS_REGULAR));

            lmme_safe_write_test_fail_at(faults[i], write_invocation);
            g_assert_false(lmme_recovery_write(store,
                                               original,
                                               root,
                                               NULL,
                                               "new recovery",
                                               12,
                                               &error));
            lmme_safe_write_test_reset();
            g_assert_nonnull(error);

            entries = lmme_recovery_list(store, NULL);
            g_assert_cmpuint(entries->len, ==, 1);
            LmmeRecoveryEntry *entry = g_ptr_array_index(entries, 0);
            g_assert_true(g_file_get_contents(entry->recovery_path,
                                              &active_contents,
                                              NULL,
                                              NULL));
            g_assert_cmpstr(active_contents,
                            ==,
                            metadata_post_commit ? "new recovery" : "old recovery");
            g_assert_true(g_file_test(previous_path, G_FILE_TEST_IS_REGULAR));
            g_assert_cmpuint(count_generation_files(cache),
                             ==,
                             metadata_post_commit ? 2 : 1);

            lmme_recovery_store_free(store);
            remove_directory_contents(cache);
            g_rmdir(cache);
            g_unlink(original);
            g_rmdir(root);
        }
    }
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
    g_autofree char *orphan_name = g_strdup_printf("%s-orphan.recover", hash);
    g_autofree char *orphan_path = g_build_filename(cache, orphan_name, NULL);
    g_autoptr(GPtrArray) entries = NULL;
    LmmeRecoveryStore *store = lmme_recovery_store_new(cache);

    g_assert_cmpint(g_mkdir(cache, 0700), ==, 0);
    g_assert_true(g_file_set_contents(legacy_recovery, "legacy", -1, NULL));
    g_assert_true(g_file_set_contents(legacy_path, original, -1, NULL));
    g_assert_true(g_file_set_contents(bad_meta, "not valid metadata", -1, NULL));
    g_assert_true(g_file_set_contents(orphan_path, "orphan", -1, NULL));

    entries = lmme_recovery_list(store, NULL);
    g_assert_cmpuint(entries->len, ==, 1);
    LmmeRecoveryEntry *entry = g_ptr_array_index(entries, 0);
    g_assert_true(entry->legacy);
    g_assert_cmpstr(entry->original_path, ==, original);
    g_assert_false(g_file_test(orphan_path, G_FILE_TEST_EXISTS));

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
    g_test_add_func("/recovery/version-1-migration", test_version_1_metadata_is_loaded_and_migrated);
    g_test_add_func("/recovery/update-faults", test_recovery_update_faults_preserve_usable_data);
    g_test_add_func("/recovery/original-change", test_original_change_is_detected);
    g_test_add_func("/recovery/missing-workspace-filter", test_missing_original_and_workspace_filter);
    g_test_add_func("/recovery/legacy-corrupt", test_legacy_and_corrupt_metadata);
    return g_test_run();
}
