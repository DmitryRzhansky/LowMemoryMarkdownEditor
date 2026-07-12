#ifndef LMME_DOCUMENT_DOCUMENT_PATHS_H
#define LMME_DOCUMENT_DOCUMENT_PATHS_H

#include <glib.h>

typedef struct _LmmeApp LmmeApp;
typedef struct _LmmeDocumentPathRemapPlan LmmeDocumentPathRemapPlan;

LmmeDocumentPathRemapPlan *lmme_document_path_remap_plan_new(LmmeApp *app,
                                                              const char *old_root,
                                                              const char *new_root,
                                                              GError **error);
void lmme_document_path_remap_plan_commit(LmmeDocumentPathRemapPlan *plan);
void lmme_document_path_remap_plan_free(LmmeDocumentPathRemapPlan *plan);

#endif
