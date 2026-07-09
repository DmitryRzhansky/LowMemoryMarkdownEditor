#ifndef LMME_DOCUMENT_FILE_MONITOR_H
#define LMME_DOCUMENT_FILE_MONITOR_H

typedef struct _LmmeDocument LmmeDocument;

void lmme_document_file_monitor_attach(LmmeDocument *doc);
void lmme_document_file_monitor_detach(LmmeDocument *doc);

#endif
