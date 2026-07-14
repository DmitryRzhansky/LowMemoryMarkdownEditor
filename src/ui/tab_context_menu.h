#ifndef LMME_UI_TAB_CONTEXT_MENU_H
#define LMME_UI_TAB_CONTEXT_MENU_H

#include <gtk/gtk.h>

typedef struct _LmmeDocument LmmeDocument;
typedef struct _LmmeApp LmmeApp;

void lmme_tab_context_menu_attach(LmmeDocument *doc, GtkWidget *tab_box);

#ifdef LMME_TESTING
gboolean lmme_tab_context_menu_test_open(LmmeApp *app,
                                         gboolean *out_finalized);
#endif

#endif
