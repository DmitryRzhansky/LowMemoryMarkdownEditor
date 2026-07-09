#include "document/recovery.h"

#include "infra/safe_write.h"
#include "infra/util.h"

#include <errno.h>
#include <string.h>

#include <glib/gstdio.h>

char *
lmme_recovery_directory(void)
{
    return g_build_filename(g_get_user_cache_dir(), "lmme", "recovery", NULL);
}

char *
lmme_recovery_path_for_original(const char *original_path)
{
    g_autofree char *dir = lmme_recovery_directory();
    g_autofree char *hash = lmme_hash_path(original_path);
    g_autofree char *file = g_strdup_printf("%s.recover", hash);

    return g_build_filename(dir, file, NULL);
}

static char *
recovery_sidecar_path(const char *original_path)
{
    g_autofree char *dir = lmme_recovery_directory();
    g_autofree char *hash = lmme_hash_path(original_path);
    g_autofree char *file = g_strdup_printf("%s.path", hash);

    return g_build_filename(dir, file, NULL);
}

gboolean
lmme_recovery_write(const char *original_path, const char *contents, gsize length, GError **error)
{
    g_autofree char *dir = lmme_recovery_directory();
    g_autofree char *recovery_path = lmme_recovery_path_for_original(original_path);
    g_autofree char *sidecar_path = recovery_sidecar_path(original_path);

    if (g_mkdir_with_parents(dir, 0700) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not create recovery directory.");
        return FALSE;
    }

    if (!lmme_safe_write_file(recovery_path, contents, length, error)) {
        return FALSE;
    }

    return lmme_safe_write_file(sidecar_path, original_path, strlen(original_path), error);
}

gboolean
lmme_recovery_remove(const char *original_path, GError **error)
{
    g_autofree char *recovery_path = lmme_recovery_path_for_original(original_path);
    g_autofree char *sidecar_path = recovery_sidecar_path(original_path);

    if (g_file_test(recovery_path, G_FILE_TEST_EXISTS) && g_unlink(recovery_path) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not remove recovery file.");
        return FALSE;
    }

    if (g_file_test(sidecar_path, G_FILE_TEST_EXISTS) && g_unlink(sidecar_path) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not remove recovery metadata.");
        return FALSE;
    }

    return TRUE;
}

GPtrArray *
lmme_recovery_list(GError **error)
{
    g_autofree char *dir_path = lmme_recovery_directory();
    g_autoptr(GDir) dir = NULL;
    GPtrArray *entries = g_ptr_array_new_with_free_func((GDestroyNotify)lmme_recovery_entry_free);
    const char *name = NULL;

    if (!g_file_test(dir_path, G_FILE_TEST_IS_DIR)) {
        return entries;
    }

    dir = g_dir_open(dir_path, 0, error);
    if (dir == NULL) {
        g_ptr_array_unref(entries);
        return NULL;
    }

    while ((name = g_dir_read_name(dir)) != NULL) {
        g_autofree char *sidecar_path = NULL;
        g_autofree char *original_path = NULL;
        g_autofree char *hash = NULL;
        g_autofree char *recovery_file = NULL;
        g_autofree char *recovery_path = NULL;
        gsize length = 0;

        if (!g_str_has_suffix(name, ".path")) {
            continue;
        }

        sidecar_path = g_build_filename(dir_path, name, NULL);
        if (!g_file_get_contents(sidecar_path, &original_path, &length, NULL) || length == 0) {
            continue;
        }

        hash = g_strndup(name, strlen(name) - strlen(".path"));
        recovery_file = g_strdup_printf("%s.recover", hash);
        recovery_path = g_build_filename(dir_path, recovery_file, NULL);
        if (!g_file_test(recovery_path, G_FILE_TEST_EXISTS)) {
            continue;
        }

        LmmeRecoveryEntry *entry = g_new0(LmmeRecoveryEntry, 1);
        entry->original_path = g_strdup(original_path);
        entry->recovery_path = g_strdup(recovery_path);
        g_ptr_array_add(entries, entry);
    }

    return entries;
}

void
lmme_recovery_entry_free(LmmeRecoveryEntry *entry)
{
    if (entry == NULL) {
        return;
    }

    g_free(entry->original_path);
    g_free(entry->recovery_path);
    g_free(entry);
}
