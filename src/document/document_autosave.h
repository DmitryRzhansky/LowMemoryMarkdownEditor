#ifndef LMME_DOCUMENT_DOCUMENT_AUTOSAVE_H
#define LMME_DOCUMENT_DOCUMENT_AUTOSAVE_H

typedef struct _LmmeDocument LmmeDocument;

void lmme_document_schedule_autosave(LmmeDocument *doc);
void lmme_document_cancel_autosave(LmmeDocument *doc);
void lmme_document_schedule_recovery(LmmeDocument *doc);
void lmme_document_cancel_recovery(LmmeDocument *doc);

#endif
