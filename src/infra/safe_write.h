#ifndef LMME_INFRA_SAFE_WRITE_H
#define LMME_INFRA_SAFE_WRITE_H

#include <glib.h>

gboolean lmme_safe_write_file(const char *path, const char *contents, gsize length, GError **error);

#endif
