#include "infra/safe_write.h"

#include <errno.h>
#include <fcntl.h>
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
    int fd = g_open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);

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
