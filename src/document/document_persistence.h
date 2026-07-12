#ifndef LMME_DOCUMENT_DOCUMENT_PERSISTENCE_H
#define LMME_DOCUMENT_DOCUMENT_PERSISTENCE_H

#include "document/document.h"

LmmeDocumentSaveResult lmme_document_persist(LmmeDocument *doc,
                                              const char *target_path,
                                              gboolean allow_external_overwrite,
                                              gboolean change_document_path,
                                              GError **error);

#endif
