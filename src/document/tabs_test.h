#ifndef LMME_DOCUMENT_TABS_TEST_H
#define LMME_DOCUMENT_TABS_TEST_H

#include <glib.h>

typedef struct _LmmeDocument LmmeDocument;
typedef gboolean (*LmmeTabsTestPrepareFunc)(LmmeDocument *doc, gpointer user_data);

gboolean lmme_tabs_test_prepare_documents(GPtrArray *documents,
                                           LmmeTabsTestPrepareFunc prepare,
                                           gpointer user_data);
gboolean lmme_tabs_test_commit_close_disposition(LmmeDocument *doc, GError **error);

#endif
