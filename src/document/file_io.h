#ifndef LMME_DOCUMENT_FILE_IO_H
#define LMME_DOCUMENT_FILE_IO_H

#include <glib.h>

gboolean lmme_file_read_utf8(const char *path,
                             gsize maximum_bytes,
                             char **out_contents,
                             gsize *out_length,
                             GError **error);

#endif
