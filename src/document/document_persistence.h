#ifndef LMME_DOCUMENT_DOCUMENT_PERSISTENCE_H
#define LMME_DOCUMENT_DOCUMENT_PERSISTENCE_H

#include "document/document.h"

/*
 * doc and target_path are borrowed and required; error may be NULL. A
 * COMMITTED result means callers must treat the target as replaced even when
 * durability was not confirmed.
 */
LmmeDocumentSaveResult lmme_document_persist(LmmeDocument *doc,
                                              const char *target_path,
                                              gboolean allow_external_overwrite,
                                              gboolean change_document_path,
                                              GError **error);

#endif
