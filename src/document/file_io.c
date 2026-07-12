#include "document/file_io.h"

#include <errno.h>
#include <sys/stat.h>

#include <glib/gstdio.h>

gboolean
lmme_file_read_utf8(const char *path,
                    gsize maximum_bytes,
                    char **out_contents,
                    gsize *out_length,
                    GError **error)
{
    struct stat info;
    char *contents = NULL;
    gsize length = 0;

    if (path == NULL || path[0] == '\0' || out_contents == NULL) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Invalid file read request.");
        return FALSE;
    }
    *out_contents = NULL;
    if (out_length != NULL) {
        *out_length = 0;
    }

    if (g_stat(path, &info) != 0) {
        g_set_error(error,
                    G_FILE_ERROR,
                    (gint)g_file_error_from_errno(errno),
                    "Could not inspect file before reading.");
        return FALSE;
    }
    if (info.st_size < 0 || (guint64)info.st_size > (guint64)maximum_bytes) {
        g_set_error_literal(error,
                            G_FILE_ERROR,
                            G_FILE_ERROR_FAILED,
                            "File is too large to open safely.");
        return FALSE;
    }
    if (!g_file_get_contents(path, &contents, &length, error)) {
        return FALSE;
    }
    if (length > maximum_bytes) {
        g_free(contents);
        g_set_error_literal(error,
                            G_FILE_ERROR,
                            G_FILE_ERROR_FAILED,
                            "File is too large to open safely.");
        return FALSE;
    }
    if (!g_utf8_validate(contents, (gssize)length, NULL)) {
        g_free(contents);
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "This file is not valid UTF-8.");
        return FALSE;
    }

    *out_contents = contents;
    if (out_length != NULL) {
        *out_length = length;
    }
    return TRUE;
}
