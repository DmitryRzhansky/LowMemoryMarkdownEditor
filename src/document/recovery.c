#include "document/recovery.h"

#include "infra/safe_write.h"
#include "infra/util.h"

#include <errno.h>
#include <string.h>

#include <glib/gstdio.h>

#define LMME_RECOVERY_FORMAT_VERSION 1

struct _LmmeRecoveryStore {
    char *directory;
};

static char *metadata_path_for_original(const LmmeRecoveryStore *store,
                                        const char *original_path,
                                        const char *suffix);
static gboolean append_metadata_entries(LmmeRecoveryStore *store,
                                        GPtrArray *entries,
                                        GHashTable *seen_hashes,
                                        GError **error);
static void append_legacy_entries(LmmeRecoveryStore *store,
                                  GPtrArray *entries,
                                  GHashTable *seen_hashes);

LmmeRecoveryStore *
lmme_recovery_store_new(const char *directory)
{
    LmmeRecoveryStore *store = NULL;

    if (directory == NULL || directory[0] == '\0') {
        return NULL;
    }

    store = g_new0(LmmeRecoveryStore, 1);
    store->directory = g_canonicalize_filename(directory, NULL);
    return store;
}

LmmeRecoveryStore *
lmme_recovery_store_new_default(void)
{
    g_autofree char *directory = g_build_filename(g_get_user_cache_dir(), "lmme", "recovery", NULL);
    return lmme_recovery_store_new(directory);
}

void
lmme_recovery_store_free(LmmeRecoveryStore *store)
{
    if (store == NULL) {
        return;
    }
    g_free(store->directory);
    g_free(store);
}

const char *
lmme_recovery_store_get_directory(const LmmeRecoveryStore *store)
{
    return store != NULL ? store->directory : NULL;
}

char *
lmme_recovery_path_for_original(const LmmeRecoveryStore *store,
                                const char *original_path)
{
    return metadata_path_for_original(store, original_path, ".recover");
}

gboolean
lmme_recovery_exists(const LmmeRecoveryStore *store,
                     const char *original_path)
{
    if (store == NULL || original_path == NULL) {
        return FALSE;
    }

    g_autofree char *recovery_path = lmme_recovery_path_for_original(store, original_path);
    g_autofree char *metadata_path = metadata_path_for_original(store, original_path, ".meta");
    g_autofree char *legacy_path = metadata_path_for_original(store, original_path, ".path");

    return g_file_test(recovery_path, G_FILE_TEST_IS_REGULAR) ||
           g_file_test(metadata_path, G_FILE_TEST_IS_REGULAR) ||
           g_file_test(legacy_path, G_FILE_TEST_IS_REGULAR);
}

static char *
metadata_path_for_original(const LmmeRecoveryStore *store,
                           const char *original_path,
                           const char *suffix)
{
    g_autofree char *hash = NULL;
    g_autofree char *file = NULL;

    if (store == NULL || original_path == NULL) {
        return NULL;
    }
    hash = lmme_hash_path(original_path);
    file = g_strdup_printf("%s%s", hash, suffix);
    return g_build_filename(store->directory, file, NULL);
}

gboolean
lmme_recovery_write(LmmeRecoveryStore *store,
                    const char *original_path,
                    const char *workspace_path,
                    const LmmeFileFingerprint *original_fingerprint,
                    const char *contents,
                    gsize length,
                    GError **error)
{
    g_autofree char *recovery_path = NULL;
    g_autofree char *metadata_path = NULL;
    g_autoptr(GKeyFile) metadata = NULL;
    g_autofree char *metadata_text = NULL;
    gsize metadata_length = 0;
    LmmeFileFingerprint fingerprint = {0};

    if (store == NULL || original_path == NULL || (contents == NULL && length > 0)) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Invalid recovery write request.");
        return FALSE;
    }
    if (g_mkdir_with_parents(store->directory, 0700) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not create recovery directory.");
        return FALSE;
    }
    if (original_fingerprint != NULL) {
        fingerprint = *original_fingerprint;
    } else if (!lmme_file_fingerprint_read(original_path, &fingerprint, error)) {
        return FALSE;
    }

    recovery_path = lmme_recovery_path_for_original(store, original_path);
    metadata_path = metadata_path_for_original(store, original_path, ".meta");
    if (!lmme_safe_write_file(recovery_path, contents != NULL ? contents : "", length, error)) {
        return FALSE;
    }

    metadata = g_key_file_new();
    g_key_file_set_integer(metadata, "recovery", "version", LMME_RECOVERY_FORMAT_VERSION);
    g_key_file_set_string(metadata, "recovery", "original_path", original_path);
    g_key_file_set_string(metadata,
                          "recovery",
                          "workspace_path",
                          workspace_path != NULL ? workspace_path : "");
    g_key_file_set_int64(metadata, "recovery", "created_us", g_get_real_time());
    g_key_file_set_boolean(metadata, "original", "exists", fingerprint.exists);
    g_key_file_set_uint64(metadata, "original", "size", fingerprint.size);
    g_key_file_set_int64(metadata, "original", "mtime_ns", fingerprint.mtime_ns);
    g_key_file_set_uint64(metadata, "original", "inode", fingerprint.inode);
    g_key_file_set_uint64(metadata, "original", "device", fingerprint.device);
    metadata_text = g_key_file_to_data(metadata, &metadata_length, error);
    if (metadata_text == NULL ||
        !lmme_safe_write_file(metadata_path, metadata_text, metadata_length, error)) {
        g_unlink(recovery_path);
        return FALSE;
    }

    return TRUE;
}

static gboolean
remove_if_present(const char *path, GError **error)
{
    if (path == NULL || !g_file_test(path, G_FILE_TEST_EXISTS)) {
        return TRUE;
    }
    if (g_unlink(path) == 0) {
        return TRUE;
    }
    g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not remove recovery data.");
    return FALSE;
}

gboolean
lmme_recovery_remove(LmmeRecoveryStore *store,
                     const char *original_path,
                     GError **error)
{
    g_autofree char *recovery_path = lmme_recovery_path_for_original(store, original_path);
    g_autofree char *metadata_path = metadata_path_for_original(store, original_path, ".meta");
    g_autofree char *legacy_path = metadata_path_for_original(store, original_path, ".path");

    return remove_if_present(recovery_path, error) &&
           remove_if_present(metadata_path, error) &&
           remove_if_present(legacy_path, error);
}

static gboolean
load_metadata_entry(LmmeRecoveryStore *store,
                    const char *name,
                    LmmeRecoveryEntry **out_entry)
{
    g_autofree char *metadata_path = g_build_filename(store->directory, name, NULL);
    g_autoptr(GKeyFile) metadata = g_key_file_new();
    g_autofree char *hash = NULL;
    g_autofree char *expected_hash = NULL;
    g_autofree char *recovery_name = NULL;
    g_autofree char *recovery_path = NULL;
    g_autoptr(GError) error = NULL;
    LmmeRecoveryEntry *entry = NULL;

    if (!g_key_file_load_from_file(metadata, metadata_path, G_KEY_FILE_NONE, &error) ||
        g_key_file_get_integer(metadata, "recovery", "version", &error) != LMME_RECOVERY_FORMAT_VERSION) {
        return FALSE;
    }

    entry = g_new0(LmmeRecoveryEntry, 1);
    entry->original_path = g_key_file_get_string(metadata, "recovery", "original_path", &error);
    entry->workspace_path = g_key_file_get_string(metadata, "recovery", "workspace_path", &error);
    entry->created_us = g_key_file_get_int64(metadata, "recovery", "created_us", &error);
    entry->original_fingerprint.exists = g_key_file_get_boolean(metadata, "original", "exists", &error);
    entry->original_fingerprint.size = g_key_file_get_uint64(metadata, "original", "size", &error);
    entry->original_fingerprint.mtime_ns = g_key_file_get_int64(metadata, "original", "mtime_ns", &error);
    entry->original_fingerprint.inode = g_key_file_get_uint64(metadata, "original", "inode", &error);
    entry->original_fingerprint.device = g_key_file_get_uint64(metadata, "original", "device", &error);
    if (error != NULL || entry->original_path == NULL || entry->original_path[0] == '\0') {
        lmme_recovery_entry_free(entry);
        return FALSE;
    }

    hash = g_strndup(name, strlen(name) - strlen(".meta"));
    expected_hash = lmme_hash_path(entry->original_path);
    if (g_strcmp0(hash, expected_hash) != 0) {
        lmme_recovery_entry_free(entry);
        return FALSE;
    }
    recovery_name = g_strdup_printf("%s.recover", hash);
    recovery_path = g_build_filename(store->directory, recovery_name, NULL);
    if (!g_file_test(recovery_path, G_FILE_TEST_IS_REGULAR)) {
        lmme_recovery_entry_free(entry);
        return FALSE;
    }

    entry->recovery_path = g_steal_pointer(&recovery_path);
    *out_entry = entry;
    return TRUE;
}

static gboolean
append_metadata_entries(LmmeRecoveryStore *store,
                        GPtrArray *entries,
                        GHashTable *seen_hashes,
                        GError **error)
{
    g_autoptr(GDir) directory = g_dir_open(store->directory, 0, error);
    const char *name = NULL;

    if (directory == NULL) {
        return FALSE;
    }
    while ((name = g_dir_read_name(directory)) != NULL) {
        LmmeRecoveryEntry *entry = NULL;
        g_autofree char *hash = NULL;

        if (!g_str_has_suffix(name, ".meta") || !load_metadata_entry(store, name, &entry)) {
            continue;
        }
        hash = g_strndup(name, strlen(name) - strlen(".meta"));
        g_hash_table_add(seen_hashes, g_steal_pointer(&hash));
        g_ptr_array_add(entries, entry);
    }
    return TRUE;
}

static void
append_legacy_entries(LmmeRecoveryStore *store,
                      GPtrArray *entries,
                      GHashTable *seen_hashes)
{
    g_autoptr(GDir) directory = g_dir_open(store->directory, 0, NULL);
    const char *name = NULL;

    if (directory == NULL) {
        return;
    }
    while ((name = g_dir_read_name(directory)) != NULL) {
        g_autofree char *hash = NULL;
        g_autofree char *sidecar_path = NULL;
        g_autofree char *original_path = NULL;
        g_autofree char *recovery_name = NULL;
        g_autofree char *recovery_path = NULL;
        gsize length = 0;
        LmmeRecoveryEntry *entry = NULL;

        if (!g_str_has_suffix(name, ".path")) {
            continue;
        }
        hash = g_strndup(name, strlen(name) - strlen(".path"));
        if (g_hash_table_contains(seen_hashes, hash)) {
            continue;
        }
        sidecar_path = g_build_filename(store->directory, name, NULL);
        if (!g_file_get_contents(sidecar_path, &original_path, &length, NULL) || length == 0) {
            continue;
        }
        recovery_name = g_strdup_printf("%s.recover", hash);
        recovery_path = g_build_filename(store->directory, recovery_name, NULL);
        if (!g_file_test(recovery_path, G_FILE_TEST_IS_REGULAR)) {
            continue;
        }
        entry = g_new0(LmmeRecoveryEntry, 1);
        entry->original_path = g_strdup(original_path);
        entry->workspace_path = g_strdup("");
        entry->recovery_path = g_steal_pointer(&recovery_path);
        entry->legacy = TRUE;
        g_ptr_array_add(entries, entry);
    }
}

GPtrArray *
lmme_recovery_list(LmmeRecoveryStore *store, GError **error)
{
    GPtrArray *entries = g_ptr_array_new_with_free_func((GDestroyNotify)lmme_recovery_entry_free);
    g_autoptr(GHashTable) seen_hashes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    if (store == NULL) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Invalid recovery store.");
        g_ptr_array_unref(entries);
        return NULL;
    }
    if (!g_file_test(store->directory, G_FILE_TEST_IS_DIR)) {
        return entries;
    }
    if (!append_metadata_entries(store, entries, seen_hashes, error)) {
        g_ptr_array_unref(entries);
        return NULL;
    }
    append_legacy_entries(store, entries, seen_hashes);
    return entries;
}

gboolean
lmme_recovery_entry_original_changed(const LmmeRecoveryEntry *entry,
                                     GError **error)
{
    LmmeFileFingerprint current = {0};

    if (entry == NULL || entry->legacy) {
        return TRUE;
    }
    if (!lmme_file_fingerprint_read(entry->original_path, &current, error)) {
        return TRUE;
    }
    return !current.exists ||
           !lmme_file_fingerprint_equal(&entry->original_fingerprint, &current);
}

gboolean
lmme_recovery_entry_belongs_to_workspace(const LmmeRecoveryEntry *entry,
                                         const char *workspace_path)
{
    g_autofree char *entry_workspace = NULL;
    g_autofree char *requested_workspace = NULL;

    if (entry == NULL || entry->original_path == NULL || workspace_path == NULL ||
        !lmme_path_is_inside(workspace_path, entry->original_path)) {
        return FALSE;
    }
    if (entry->workspace_path == NULL || entry->workspace_path[0] == '\0') {
        return TRUE;
    }

    entry_workspace = g_canonicalize_filename(entry->workspace_path, NULL);
    requested_workspace = g_canonicalize_filename(workspace_path, NULL);
    return g_strcmp0(entry_workspace, requested_workspace) == 0;
}

void
lmme_recovery_entry_free(LmmeRecoveryEntry *entry)
{
    if (entry == NULL) {
        return;
    }
    g_free(entry->original_path);
    g_free(entry->workspace_path);
    g_free(entry->recovery_path);
    g_free(entry);
}
