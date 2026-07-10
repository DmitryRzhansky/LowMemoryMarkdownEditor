#ifndef LMME_DOCUMENT_FILE_MONITOR_H
#define LMME_DOCUMENT_FILE_MONITOR_H

#include <glib.h>

typedef struct _LmmeDocument LmmeDocument;

void lmme_document_file_monitor_attach(LmmeDocument *doc);
void lmme_document_file_monitor_detach(LmmeDocument *doc);
gboolean lmme_document_resolve_external_conflict(LmmeDocument *doc);

#endif
