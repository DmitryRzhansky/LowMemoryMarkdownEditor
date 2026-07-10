#define _GNU_SOURCE

#include "infra/safe_write.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
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
        if (result == 0) {
            g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "Could not make progress while writing file.");
            return FALSE;
        }
        written += (gsize)result;
    }

    return TRUE;
}

gboolean
lmme_safe_write_file(const char *path, const char *contents, gsize length, GError **error)
{
    g_autofree char *resolved_path = NULL;
    const char *target_path = path;
    g_autofree char *dir = NULL;
    g_autofree char *base = NULL;
    g_autofree char *tmp_base = NULL;
    g_autofree char *tmp_path = NULL;
    struct stat link_stat;
    struct stat target_stat;
    mode_t mode = 0600;
    int fd = -1;
    int dir_fd = -1;
    gboolean renamed = FALSE;
    gboolean success = FALSE;

    if (path == NULL || path[0] == '\0' || (contents == NULL && length > 0)) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Invalid file save request.");
        return FALSE;
    }

    if (lstat(path, &link_stat) == 0 && S_ISLNK(link_stat.st_mode)) {
        resolved_path = realpath(path, NULL);
        if (resolved_path == NULL) {
            g_set_error(error,
                        G_FILE_ERROR,
                        g_file_error_from_errno(errno),
                        "Could not resolve symbolic link before saving.");
            return FALSE;
        }
        target_path = resolved_path;
    }

    if (stat(target_path, &target_stat) == 0) {
        mode = target_stat.st_mode & 07777;
    } else if (errno != ENOENT) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not inspect file before saving.");
        return FALSE;
    }

    dir = g_path_get_dirname(target_path);
    base = g_path_get_basename(target_path);
    tmp_base = g_strdup_printf(".%s.lmme.XXXXXX", base);
    tmp_path = g_build_filename(dir, tmp_base, NULL);
    fd = g_mkstemp_full(tmp_path, O_RDWR | O_CLOEXEC, (gint)mode);

    if (fd < 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not create temporary save file.");
        return FALSE;
    }

    if (fchmod(fd, mode) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not preserve file permissions.");
        goto out;
    }

    if (!write_all(fd, contents != NULL ? contents : "", length, error)) {
        goto out;
    }

    if (fsync(fd) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not flush temporary save file.");
        goto out;
    }

    if (close(fd) != 0) {
        fd = -1;
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not close temporary save file.");
        goto out;
    }
    fd = -1;

    if (g_rename(tmp_path, target_path) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not replace file.");
        goto out;
    }
    renamed = TRUE;

    dir_fd = g_open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0);
    if (dir_fd < 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not open parent directory after saving.");
        goto out;
    }
    if (fsync(dir_fd) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not flush parent directory after saving.");
        goto out;
    }
    success = TRUE;

out:
    if (fd >= 0) {
        close(fd);
    }
    if (dir_fd >= 0) {
        close(dir_fd);
    }
    if (!renamed) {
        g_unlink(tmp_path);
    }
    return success;
}
