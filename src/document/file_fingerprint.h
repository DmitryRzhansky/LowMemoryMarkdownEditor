#ifndef LMME_DOCUMENT_FILE_FINGERPRINT_H
#define LMME_DOCUMENT_FILE_FINGERPRINT_H

#include <glib.h>

typedef struct {
    gboolean exists;
    guint64 size;
    gint64 mtime_ns;
    guint64 inode;
    guint64 device;
} LmmeFileFingerprint;

gboolean lmme_file_fingerprint_read(const char *path,
                                    LmmeFileFingerprint *out_fingerprint,
                                    GError **error);
gboolean lmme_file_fingerprint_equal(const LmmeFileFingerprint *left,
                                     const LmmeFileFingerprint *right);

#endif
