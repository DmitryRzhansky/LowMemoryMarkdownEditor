#ifndef LMME_UI_TREE_CONTEXT_MENU_H
#define LMME_UI_TREE_CONTEXT_MENU_H

typedef struct _LmmeApp LmmeApp;

void lmme_tree_context_menu_attach(LmmeApp *app);
char *lmme_tree_context_menu_dup_relative_path(const LmmeApp *app);

#endif
