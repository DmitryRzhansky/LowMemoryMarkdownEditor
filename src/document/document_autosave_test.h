#ifndef LMME_DOCUMENT_DOCUMENT_AUTOSAVE_TEST_H
#define LMME_DOCUMENT_DOCUMENT_AUTOSAVE_TEST_H

#include <glib.h>

typedef struct _LmmeDocument LmmeDocument;

/* Runs the delayed recovery callback immediately without waiting for wall time. */
gboolean lmme_document_test_run_recovery_timeout(LmmeDocument *doc);

#endif
