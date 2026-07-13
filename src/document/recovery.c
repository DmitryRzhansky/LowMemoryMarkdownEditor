#define _GNU_SOURCE

#include "document/recovery.h"
#include "document/recovery_test.h"

#include "infra/safe_write.h"
#include "infra/util.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#include <glib/gstdio.h>

#define LMME_RECOVERY_FORMAT_VERSION 2
#define LMME_RECOVERY_FORMAT_VERSION_1 1

struct _LmmeRecoveryStore {
    char *directory;
};

struct _LmmeRecoveryStage {
    LmmeRecoveryStore *store;
    char *generation_name;
    char *generation_path;
    char *metadata_path;
    char *previous_recovery_path;
    char *legacy_path;
    char *metadata_text;
    gsize metadata_length;
    gboolean activated;
};

static char *path_for_original_with_suffix(const LmmeRecoveryStore *store,
                                           const char *original_path,
                                           const char *suffix);
static char *fixed_recovery_path_for_original(const LmmeRecoveryStore *store,
                                              const char *original_path);
static gboolean append_metadata_entries(LmmeRecoveryStore *store,
                                        GPtrArray *entries,
                                        GHashTable *seen_hashes,
                                        GError **error);
static void append_legacy_entries(LmmeRecoveryStore *store,
                                  GPtrArray *entries,
                                  GHashTable *seen_hashes);

typedef struct {
    gboolean enabled;
    LmmeRecoveryTestFault fault;
    guint target_invocation;
    guint current_invocation;
    const char *active_payload_path;
} LmmeRecoveryTestState;

static LmmeRecoveryTestState recovery_test_state;

void
lmme_recovery_test_fail_at(LmmeRecoveryTestFault fault, guint invocation)
{
    recovery_test_state.enabled = TRUE;
    recovery_test_state.fault = fault;
    recovery_test_state.target_invocation = invocation > 0 ? invocation : 1;
    recovery_test_state.current_invocation = 0;
    recovery_test_state.active_payload_path = NULL;
}

void
lmme_recovery_test_reset(void)
{
    recovery_test_state = (LmmeRecoveryTestState){0};
}

static LmmeRecoveryTestFault
recovery_test_fault_for_path(const char *path)
{
    g_autofree char *basename = NULL;
    const char *dash = NULL;

    if (path == NULL) {
        return (LmmeRecoveryTestFault)-1;
    }
    basename = g_path_get_basename(path);
    if (g_str_has_suffix(basename, ".meta")) {
        return LMME_RECOVERY_TEST_FAIL_METADATA_UNLINK;
    }
    if (g_str_has_suffix(basename, ".path")) {
        return LMME_RECOVERY_TEST_FAIL_LEGACY_LOCATOR_UNLINK;
    }
    if (!g_str_has_suffix(basename, ".recover")) {
        return (LmmeRecoveryTestFault)-1;
    }
    if (recovery_test_state.active_payload_path != NULL &&
        g_strcmp0(path, recovery_test_state.active_payload_path) == 0) {
        return LMME_RECOVERY_TEST_FAIL_ACTIVE_PAYLOAD_UNLINK;
    }
    dash = strchr(basename, '-');
    if (dash != NULL && g_str_has_suffix(dash, ".recover")) {
        return LMME_RECOVERY_TEST_FAIL_INACTIVE_GENERATION_UNLINK;
    }
    return LMME_RECOVERY_TEST_FAIL_ACTIVE_PAYLOAD_UNLINK;
}

static gboolean
recovery_test_should_fail_unlink(const char *path)
{
    LmmeRecoveryTestFault path_fault;

    if (!recovery_test_state.enabled) {
        return FALSE;
    }
    path_fault = recovery_test_fault_for_path(path);
    if (path_fault != recovery_test_state.fault) {
        return FALSE;
    }
    recovery_test_state.current_invocation++;
    if (recovery_test_state.current_invocation != recovery_test_state.target_invocation) {
        return FALSE;
    }
    recovery_test_state.enabled = FALSE;
    errno = EIO;
    return TRUE;
}

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

static char *
path_for_original_with_suffix(const LmmeRecoveryStore *store,
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

static char *
fixed_recovery_path_for_original(const LmmeRecoveryStore *store,
                                 const char *original_path)
{
    return path_for_original_with_suffix(store, original_path, ".recover");
}

static gboolean
generation_name_is_valid(const char *hash, const char *name)
{
    g_autofree char *prefix = NULL;

    if (hash == NULL || name == NULL || g_path_is_absolute(name) ||
        strchr(name, G_DIR_SEPARATOR) != NULL) {
        return FALSE;
    }
    prefix = g_strdup_printf("%s-", hash);
    return g_str_has_prefix(name, prefix) &&
           strlen(name) > strlen(prefix) + strlen(".recover") &&
           g_str_has_suffix(name, ".recover");
}

static char *
metadata_recovery_path(const LmmeRecoveryStore *store,
                       const char *original_path)
{
    g_autofree char *metadata_path = path_for_original_with_suffix(store, original_path, ".meta");
    g_autoptr(GKeyFile) metadata = g_key_file_new();
    g_autoptr(GError) error = NULL;
    g_autofree char *stored_original = NULL;
    g_autofree char *hash = NULL;
    g_autofree char *generation_name = NULL;
    gint version = 0;

    if (!g_key_file_load_from_file(metadata, metadata_path, G_KEY_FILE_NONE, &error)) {
        return NULL;
    }
    version = g_key_file_get_integer(metadata, "recovery", "version", &error);
    stored_original = g_key_file_get_string(metadata, "recovery", "original_path", &error);
    if (error != NULL || g_strcmp0(stored_original, original_path) != 0) {
        return NULL;
    }
    if (version == LMME_RECOVERY_FORMAT_VERSION_1) {
        return fixed_recovery_path_for_original(store, original_path);
    }
    if (version != LMME_RECOVERY_FORMAT_VERSION) {
        return NULL;
    }

    hash = lmme_hash_path(original_path);
    generation_name = g_key_file_get_string(metadata, "recovery", "recovery_file", &error);
    if (error != NULL || !generation_name_is_valid(hash, generation_name)) {
        return NULL;
    }
    return g_build_filename(store->directory, generation_name, NULL);
}

char *
lmme_recovery_path_for_original(const LmmeRecoveryStore *store,
                                const char *original_path)
{
    char *metadata_path = metadata_recovery_path(store, original_path);

    return metadata_path != NULL
               ? metadata_path
               : fixed_recovery_path_for_original(store, original_path);
}

gboolean
lmme_recovery_exists(const LmmeRecoveryStore *store,
                     const char *original_path)
{
    g_autofree char *recovery_path = NULL;
    g_autofree char *legacy_path = NULL;

    if (store == NULL || original_path == NULL) {
        return FALSE;
    }
    recovery_path = lmme_recovery_path_for_original(store, original_path);
    legacy_path = path_for_original_with_suffix(store, original_path, ".path");
    return g_file_test(recovery_path, G_FILE_TEST_IS_REGULAR) ||
           g_file_test(legacy_path, G_FILE_TEST_IS_REGULAR);
}

static gboolean
ensure_recovery_directory(LmmeRecoveryStore *store, GError **error)
{
    if (g_mkdir_with_parents(store->directory, 0700) == 0) {
        return TRUE;
    }
    g_set_error(error,
                G_FILE_ERROR,
                (gint)g_file_error_from_errno(errno),
                "Could not create recovery directory.");
    return FALSE;
}

LmmeRecoveryStage *
lmme_recovery_stage_new(LmmeRecoveryStore *store,
                        const char *original_path,
                        const char *workspace_path,
                        const LmmeFileFingerprint *original_fingerprint,
                        const char *contents,
                        gsize length,
                        GError **error)
{
    LmmeRecoveryStage *stage = NULL;
    LmmeFileFingerprint fingerprint = {0};
    g_autofree char *hash = NULL;
    g_autofree char *generation = NULL;
    g_autoptr(GKeyFile) metadata = NULL;
    LmmeSafeWriteOutcome content_outcome;

    if (store == NULL || original_path == NULL || (contents == NULL && length > 0)) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Invalid recovery write request.");
        return NULL;
    }
    if (!ensure_recovery_directory(store, error)) {
        return NULL;
    }
    if (original_fingerprint != NULL) {
        fingerprint = *original_fingerprint;
    } else if (!lmme_file_fingerprint_read(original_path, &fingerprint, error)) {
        return NULL;
    }

    stage = g_new0(LmmeRecoveryStage, 1);
    stage->store = store;
    hash = lmme_hash_path(original_path);
    generation = g_uuid_string_random();
    stage->generation_name = g_strdup_printf("%s-%s.recover", hash, generation);
    stage->generation_path = g_build_filename(store->directory, stage->generation_name, NULL);
    stage->metadata_path = path_for_original_with_suffix(store, original_path, ".meta");
    stage->previous_recovery_path = lmme_recovery_path_for_original(store, original_path);
    stage->legacy_path = path_for_original_with_suffix(store, original_path, ".path");

    content_outcome = lmme_safe_write_file(stage->generation_path,
                                           contents != NULL ? contents : "",
                                           length,
                                           error);
    if (content_outcome.result != LMME_SAFE_WRITE_COMMITTED_DURABLE) {
        if (content_outcome.result == LMME_SAFE_WRITE_COMMITTED_NOT_DURABLE) {
            (void)g_unlink(stage->generation_path);
        }
        lmme_recovery_stage_free(stage);
        return NULL;
    }

    metadata = g_key_file_new();
    g_key_file_set_integer(metadata, "recovery", "version", LMME_RECOVERY_FORMAT_VERSION);
    g_key_file_set_string(metadata, "recovery", "original_path", original_path);
    g_key_file_set_string(metadata,
                          "recovery",
                          "workspace_path",
                          workspace_path != NULL ? workspace_path : "");
    g_key_file_set_string(metadata, "recovery", "recovery_file", stage->generation_name);
    if (stage->previous_recovery_path != NULL) {
        g_autofree char *previous_name = g_path_get_basename(stage->previous_recovery_path);
        g_key_file_set_string(metadata, "recovery", "previous_recovery_file", previous_name);
    }
    g_key_file_set_int64(metadata, "recovery", "created_us", g_get_real_time());
    g_key_file_set_boolean(metadata, "original", "exists", fingerprint.exists);
    g_key_file_set_uint64(metadata, "original", "size", fingerprint.size);
    g_key_file_set_int64(metadata, "original", "mtime_ns", fingerprint.mtime_ns);
    g_key_file_set_uint64(metadata, "original", "inode", fingerprint.inode);
    g_key_file_set_uint64(metadata, "original", "device", fingerprint.device);
    stage->metadata_text = g_key_file_to_data(metadata, &stage->metadata_length, error);
    if (stage->metadata_text == NULL) {
        lmme_recovery_stage_free(stage);
        return NULL;
    }
    return stage;
}

gboolean
lmme_recovery_stage_commit(LmmeRecoveryStage *stage, GError **error)
{
    LmmeSafeWriteOutcome metadata_outcome;

    if (stage == NULL || stage->activated) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Invalid recovery commit request.");
        return FALSE;
    }

    metadata_outcome = lmme_safe_write_file(stage->metadata_path,
                                            stage->metadata_text,
                                            stage->metadata_length,
                                            error);
    if (metadata_outcome.result == LMME_SAFE_WRITE_NOT_COMMITTED) {
        return FALSE;
    }

    stage->activated = TRUE;
    if (metadata_outcome.result == LMME_SAFE_WRITE_COMMITTED_NOT_DURABLE) {
        return FALSE;
    }

    if (g_strcmp0(stage->previous_recovery_path, stage->generation_path) != 0) {
        (void)g_unlink(stage->previous_recovery_path);
    }
    (void)g_unlink(stage->legacy_path);
    return TRUE;
}

void
lmme_recovery_stage_free(LmmeRecoveryStage *stage)
{
    if (stage == NULL) {
        return;
    }
    if (!stage->activated && stage->generation_path != NULL) {
        (void)g_unlink(stage->generation_path);
    }
    g_free(stage->generation_name);
    g_free(stage->generation_path);
    g_free(stage->metadata_path);
    g_free(stage->previous_recovery_path);
    g_free(stage->legacy_path);
    g_free(stage->metadata_text);
    g_free(stage);
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
    LmmeRecoveryStage *stage = lmme_recovery_stage_new(store,
                                                       original_path,
                                                       workspace_path,
                                                       original_fingerprint,
                                                       contents,
                                                       length,
                                                       error);
    gboolean success = FALSE;

    if (stage == NULL) {
        return FALSE;
    }
    success = lmme_recovery_stage_commit(stage, error);
    lmme_recovery_stage_free(stage);
    return success;
}

static gboolean
regular_file_without_following_symlink(const char *path)
{
    struct stat info;

    return g_lstat(path, &info) == 0 && S_ISREG(info.st_mode);
}

static gboolean
remove_if_present(const char *path, GError **error)
{
    if (path == NULL || !g_file_test(path, G_FILE_TEST_EXISTS)) {
        return TRUE;
    }
    if (recovery_test_should_fail_unlink(path)) {
        g_set_error(error,
                    G_FILE_ERROR,
                    (gint)g_file_error_from_errno(errno),
                    "Could not remove recovery data.");
        return FALSE;
    }
    if (g_unlink(path) == 0) {
        return TRUE;
    }
    g_set_error(error,
                G_FILE_ERROR,
                (gint)g_file_error_from_errno(errno),
                "Could not remove recovery data.");
    return FALSE;
}

static char *
active_payload_path_for_original(LmmeRecoveryStore *store, const char *original_path)
{
    return metadata_recovery_path(store, original_path);
}

static gboolean
payload_path_exists(const char *path)
{
    return path != NULL && regular_file_without_following_symlink(path);
}

static gboolean
remove_payload_paths_except(const GPtrArray *payload_paths,
                            const char *keep_path,
                            GError **error)
{
    for (guint i = 0; i < payload_paths->len; i++) {
        const char *path = g_ptr_array_index(payload_paths, i);

        if (keep_path != NULL && g_strcmp0(path, keep_path) == 0) {
            continue;
        }
        if (!remove_if_present(path, error)) {
            return FALSE;
        }
    }
    return TRUE;
}

static gboolean
collect_payload_paths(LmmeRecoveryStore *store,
                      const char *hash,
                      const char *generation_prefix,
                      GPtrArray *payload_paths)
{
    g_autofree char *fixed_path = NULL;
    g_autoptr(GDir) directory = NULL;
    const char *name = NULL;

    fixed_path = g_strdup_printf("%s.recover", hash);
    g_ptr_array_add(payload_paths, g_build_filename(store->directory, fixed_path, NULL));

    directory = g_dir_open(store->directory, 0, NULL);
    while (directory != NULL && (name = g_dir_read_name(directory)) != NULL) {
        if (g_str_has_prefix(name, generation_prefix) && g_str_has_suffix(name, ".recover")) {
            g_ptr_array_add(payload_paths, g_build_filename(store->directory, name, NULL));
        }
    }
    return TRUE;
}

static void
deduplicate_payload_paths(GPtrArray *payload_paths)
{
    g_autoptr(GHashTable) seen = g_hash_table_new(g_str_hash, g_str_equal);

    for (gint i = (gint)payload_paths->len - 1; i >= 0; i--) {
        char *path = g_ptr_array_index(payload_paths, i);

        if (g_hash_table_contains(seen, path)) {
            g_ptr_array_remove_index(payload_paths, (guint)i);
            g_free(path);
            continue;
        }
        g_hash_table_add(seen, path);
    }
}

gboolean
lmme_recovery_remove(LmmeRecoveryStore *store,
                     const char *original_path,
                     GError **error)
{
    g_autofree char *hash = NULL;
    g_autofree char *generation_prefix = NULL;
    g_autofree char *active_payload_path = NULL;
    g_autofree char *metadata_path = NULL;
    g_autofree char *legacy_locator_path = NULL;
    g_autoptr(GPtrArray) payload_paths = NULL;
    const char *active_keep_path = NULL;

    if (store == NULL || original_path == NULL) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Invalid recovery removal request.");
        return FALSE;
    }

    hash = lmme_hash_path(original_path);
    generation_prefix = g_strdup_printf("%s-", hash);
    metadata_path = path_for_original_with_suffix(store, original_path, ".meta");
    legacy_locator_path = path_for_original_with_suffix(store, original_path, ".path");

    payload_paths = g_ptr_array_new_with_free_func(g_free);
    collect_payload_paths(store, hash, generation_prefix, payload_paths);
    deduplicate_payload_paths(payload_paths);

    active_payload_path = active_payload_path_for_original(store, original_path);
    if (payload_path_exists(active_payload_path)) {
        active_keep_path = active_payload_path;
    }
    recovery_test_state.active_payload_path = active_keep_path;

    if (!remove_payload_paths_except(payload_paths, active_keep_path, error)) {
        recovery_test_state.active_payload_path = NULL;
        return FALSE;
    }

    if (active_keep_path != NULL && !remove_if_present(active_keep_path, error)) {
        recovery_test_state.active_payload_path = NULL;
        return FALSE;
    }

    if (!remove_if_present(metadata_path, error) ||
        !remove_if_present(legacy_locator_path, error)) {
        recovery_test_state.active_payload_path = NULL;
        return FALSE;
    }

    recovery_test_state.active_payload_path = NULL;
    return TRUE;
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
    g_autofree char *previous_name = NULL;
    g_autofree char *fixed_name = NULL;
    g_autofree char *recovery_path = NULL;
    g_autoptr(GError) error = NULL;
    LmmeRecoveryEntry *entry = NULL;
    gint version = 0;

    if (!g_key_file_load_from_file(metadata, metadata_path, G_KEY_FILE_NONE, &error)) {
        return FALSE;
    }
    version = g_key_file_get_integer(metadata, "recovery", "version", &error);
    if (error != NULL || (version != LMME_RECOVERY_FORMAT_VERSION &&
                          version != LMME_RECOVERY_FORMAT_VERSION_1)) {
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
    fixed_name = g_strdup_printf("%s.recover", hash);

    if (version == LMME_RECOVERY_FORMAT_VERSION_1) {
        recovery_name = g_strdup(fixed_name);
    } else {
        recovery_name = g_key_file_get_string(metadata, "recovery", "recovery_file", &error);
        if (error != NULL || !generation_name_is_valid(hash, recovery_name)) {
            lmme_recovery_entry_free(entry);
            return FALSE;
        }
        previous_name = g_key_file_get_string(metadata,
                                              "recovery",
                                              "previous_recovery_file",
                                              NULL);
        if (previous_name != NULL &&
            g_strcmp0(previous_name, recovery_name) != 0 &&
            g_strcmp0(previous_name, fixed_name) != 0 &&
            !generation_name_is_valid(hash, previous_name)) {
            g_clear_pointer(&previous_name, g_free);
        }
    }
    recovery_path = g_build_filename(store->directory, recovery_name, NULL);
    if (!regular_file_without_following_symlink(recovery_path)) {
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

        if (!g_str_has_suffix(name, ".meta") ||
            !load_metadata_entry(store, name, &entry)) {
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
        if (!regular_file_without_following_symlink(recovery_path)) {
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
