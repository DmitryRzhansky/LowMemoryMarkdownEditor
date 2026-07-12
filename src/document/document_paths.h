#ifndef LMME_DOCUMENT_DOCUMENT_PATHS_H
#define LMME_DOCUMENT_DOCUMENT_PATHS_H

#include <glib.h>

typedef struct _LmmeApp LmmeApp;
typedef struct _LmmeDocumentPathRemapPlan LmmeDocumentPathRemapPlan;

/*
 * Returns an owned plan with borrowed references to app/documents. The app and
 * affected documents must outlive commit/free. Freeing an uncommitted plan
 * discards its staged recovery changes. Commit does not consume the plan.
 */
LmmeDocumentPathRemapPlan *lmme_document_path_remap_plan_new(LmmeApp *app,
                                                              const char *old_root,
                                                              const char *new_root,
                                                              GError **error);
void lmme_document_path_remap_plan_commit(LmmeDocumentPathRemapPlan *plan);
void lmme_document_path_remap_plan_free(LmmeDocumentPathRemapPlan *plan);

#endif
