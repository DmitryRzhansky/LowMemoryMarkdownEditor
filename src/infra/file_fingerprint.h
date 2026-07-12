#ifndef LMME_INFRA_FILE_FINGERPRINT_H
#define LMME_INFRA_FILE_FINGERPRINT_H

#include <glib.h>

typedef struct {
    gboolean exists;
    guint64 size;
    gint64 mtime_ns;
    guint64 inode;
    guint64 device;
} LmmeFileFingerprint;

/* out_fingerprint is required and caller-owned; error may be NULL. */
gboolean lmme_file_fingerprint_read(const char *path,
                                    LmmeFileFingerprint *out_fingerprint,
                                    GError **error);
gboolean lmme_file_fingerprint_read_fd(int fd,
                                       LmmeFileFingerprint *out_fingerprint,
                                       GError **error);
/* Both fingerprints are borrowed; NULL never compares equal. */
gboolean lmme_file_fingerprint_equal(const LmmeFileFingerprint *left,
                                     const LmmeFileFingerprint *right);

#endif
