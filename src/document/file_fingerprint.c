#define _GNU_SOURCE

#include "document/file_fingerprint.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

gboolean
lmme_file_fingerprint_read(const char *path,
                           LmmeFileFingerprint *out_fingerprint,
                           GError **error)
{
    struct stat info;

    if (path == NULL || out_fingerprint == NULL) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Invalid file fingerprint request.");
        return FALSE;
    }

    memset(out_fingerprint, 0, sizeof(*out_fingerprint));
    if (stat(path, &info) != 0) {
        if (errno == ENOENT) {
            return TRUE;
        }
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not inspect file state.");
        return FALSE;
    }

    out_fingerprint->exists = TRUE;
    out_fingerprint->size = info.st_size >= 0 ? (guint64)info.st_size : 0;
    out_fingerprint->mtime_ns = ((gint64)info.st_mtim.tv_sec * G_GINT64_CONSTANT(1000000000)) +
                                (gint64)info.st_mtim.tv_nsec;
    out_fingerprint->inode = (guint64)info.st_ino;
    out_fingerprint->device = (guint64)info.st_dev;
    return TRUE;
}

gboolean
lmme_file_fingerprint_equal(const LmmeFileFingerprint *left,
                            const LmmeFileFingerprint *right)
{
    if (left == NULL || right == NULL || left->exists != right->exists) {
        return FALSE;
    }
    if (!left->exists) {
        return TRUE;
    }
    return left->size == right->size &&
           left->mtime_ns == right->mtime_ns &&
           left->inode == right->inode &&
           left->device == right->device;
}
