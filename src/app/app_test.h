#ifndef LMME_APP_APP_TEST_H
#define LMME_APP_APP_TEST_H

#include <glib.h>

typedef struct _LmmeApp LmmeApp;

#ifdef LMME_TESTING

void lmme_app_test_save_session(LmmeApp *app);
void lmme_app_test_cancel_pending_work(LmmeApp *app);
void lmme_app_test_destroy_runtime_ui(LmmeApp *app);
gboolean lmme_app_test_drain_main_context(guint max_iterations);
void lmme_app_test_attach_lifetime(LmmeApp *app);

#endif

#endif
