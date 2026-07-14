#ifndef LMME_UI_TREE_CONTEXT_MENU_H
#define LMME_UI_TREE_CONTEXT_MENU_H

#include <glib.h>

typedef struct _LmmeApp LmmeApp;

void lmme_tree_context_menu_attach(LmmeApp *app);
char *lmme_tree_context_menu_dup_relative_path(const LmmeApp *app);

#ifdef LMME_TESTING
gboolean lmme_tree_context_menu_test_open(LmmeApp *app,
                                          gboolean *out_finalized);
#endif

#endif
