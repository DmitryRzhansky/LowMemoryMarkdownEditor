#ifndef LMME_FILE_TREE_H
#define LMME_FILE_TREE_H

#include <gtk/gtk.h>

#include "workspace.h"

enum {
    LMME_TREE_COL_ICON = 0,
    LMME_TREE_COL_NAME,
    LMME_TREE_COL_PATH,
    LMME_TREE_COL_KIND,
    LMME_TREE_N_COLS
};

GtkWidget *lmme_file_tree_create(void);
void lmme_file_tree_populate(GtkWidget *tree_view, LmmeFileNode *root);
gboolean lmme_file_tree_get_selected(GtkWidget *tree_view,
                                     char **out_path,
                                     LmmeFileKind *out_kind);

#endif
