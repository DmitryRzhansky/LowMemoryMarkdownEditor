#include "autosave.h"

#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <glib/gstdio.h>

static gboolean
write_all(int fd, const char *contents, gsize length, GError **error)
{
    gsize written = 0;

    while (written < length) {
        ssize_t result = write(fd, contents + written, length - written);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not write file.");
            return FALSE;
        }
        written += (gsize)result;
    }

    return TRUE;
}

gboolean
lmme_safe_write_file(const char *path, const char *contents, gsize length, GError **error)
{
    g_autofree char *dir = g_path_get_dirname(path);
    g_autofree char *base = g_path_get_basename(path);
    g_autofree char *tmp_base = g_strdup_printf(".%s.lmme.tmp", base);
    g_autofree char *tmp_path = g_build_filename(dir, tmp_base, NULL);
    int fd = -1;

    fd = g_open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not create temporary save file.");
        return FALSE;
    }

    if (!write_all(fd, contents, length, error)) {
        close(fd);
        g_unlink(tmp_path);
        return FALSE;
    }

    if (fsync(fd) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not flush temporary save file.");
        close(fd);
        g_unlink(tmp_path);
        return FALSE;
    }

    if (close(fd) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not close temporary save file.");
        g_unlink(tmp_path);
        return FALSE;
    }

    if (g_rename(tmp_path, path) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not replace file.");
        g_unlink(tmp_path);
        return FALSE;
    }

    return TRUE;
}

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
        if (!g_str_has_suffix(name, ".path")) {
            continue;
        }

        g_autofree char *sidecar_path = g_build_filename(dir_path, name, NULL);
        g_autofree char *original_path = NULL;
        gsize length = 0;

        if (!g_file_get_contents(sidecar_path, &original_path, &length, NULL) || length == 0) {
            continue;
        }

        g_autofree char *hash = g_strndup(name, strlen(name) - strlen(".path"));
        g_autofree char *recovery_file = g_strdup_printf("%s.recover", hash);
        g_autofree char *recovery_path = g_build_filename(dir_path, recovery_file, NULL);

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
