#ifndef LMME_DOCUMENT_FILE_MONITOR_H
#define LMME_DOCUMENT_FILE_MONITOR_H

#include <glib.h>

#include "document/document.h"

typedef struct _LmmeDocument LmmeDocument;

typedef enum {
    LMME_FILE_CHANGE_IGNORE,
    LMME_FILE_CHANGE_INTERNAL_WRITE,
    LMME_FILE_CHANGE_RELOAD,
    LMME_FILE_CHANGE_EXTERNAL_CHANGED,
    LMME_FILE_CHANGE_EXTERNAL_DELETED
} LmmeFileChangeAction;

/*
 * current and last_known are borrowed and required. expected_internal is
 * borrowed and may be NULL when has_expected_internal is FALSE. This function
 * is pure.
 */
LmmeFileChangeAction lmme_file_change_decide(const LmmeFileFingerprint *current,
                                              const LmmeFileFingerprint *last_known,
                                              const LmmeFileFingerprint *expected_internal,
                                              gboolean has_expected_internal,
                                              gboolean modified,
                                              gboolean restored,
                                              LmmeDiskState disk_state);

/* doc is borrowed; attach/detach update only the monitor owned by doc. */
void lmme_document_file_monitor_attach(LmmeDocument *doc);
void lmme_document_file_monitor_detach(LmmeDocument *doc);
gboolean lmme_document_resolve_external_conflict(LmmeDocument *doc);

#endif
