#ifndef LMME_UI_EXTERNAL_CONFLICT_H
#define LMME_UI_EXTERNAL_CONFLICT_H

#include <glib.h>

#include "infra/dialogs.h"

typedef struct _LmmeDocument LmmeDocument;

typedef enum {
    LMME_EXTERNAL_CONFLICT_IDLE = 0,
    LMME_EXTERNAL_CONFLICT_SCHEDULED,
    LMME_EXTERNAL_CONFLICT_PRESENTING
} LmmeExternalConflictState;

/* Schedule at most one conflict UI workflow per document. */
void lmme_external_conflict_request(LmmeDocument *doc);
void lmme_external_conflict_cancel(LmmeDocument *doc);

/*
 * Apply a user choice without showing a dialog. Returns FALSE when validation
 * fails or the operation could not be completed. choice may be KEEP_LOCAL.
 */
gboolean lmme_document_apply_external_conflict_choice(LmmeDocument *doc,
                                                      LmmeExternalConflictChoice choice,
                                                      guint64 captured_generation,
                                                      const char *captured_document_path,
                                                      const char *captured_workspace_path,
                                                      GError **error);

#endif
