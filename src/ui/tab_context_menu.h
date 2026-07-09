#ifndef LMME_UI_TAB_CONTEXT_MENU_H
#define LMME_UI_TAB_CONTEXT_MENU_H

#include <gtk/gtk.h>

typedef struct _LmmeDocument LmmeDocument;

void lmme_tab_context_menu_attach(LmmeDocument *doc, GtkWidget *tab_box);

#endif
