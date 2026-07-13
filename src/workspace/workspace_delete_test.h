#ifndef LMME_WORKSPACE_WORKSPACE_DELETE_TEST_H
#define LMME_WORKSPACE_WORKSPACE_DELETE_TEST_H

#include <glib.h>

/* Private test seam. invocation is one-based and applies to the next deletes. */
void lmme_workspace_delete_test_fail_at(guint after_successful_unlinks, guint invocation);
void lmme_workspace_delete_test_reset(void);

#endif
