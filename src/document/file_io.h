#ifndef LMME_DOCUMENT_FILE_IO_H
#define LMME_DOCUMENT_FILE_IO_H

#include <glib.h>

#define LMME_DOCUMENT_MAX_OPEN_BYTES (10U * 1024U * 1024U)

/*
 * On success, out_contents receives an owned NUL-terminated UTF-8 buffer and
 * out_length, when non-NULL, receives its byte length. out_contents is
 * required; error and out_length may be NULL.
 */
gboolean lmme_file_read_utf8(const char *path,
                             gsize maximum_bytes,
                             char **out_contents,
                             gsize *out_length,
                             GError **error);

#endif
