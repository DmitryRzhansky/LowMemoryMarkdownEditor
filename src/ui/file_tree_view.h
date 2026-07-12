#ifndef LMME_UI_FILE_TREE_VIEW_H
#define LMME_UI_FILE_TREE_VIEW_H

#include <gtk/gtk.h>

#include "workspace/workspace.h"

typedef struct _LmmeApp LmmeApp;

GtkWidget *lmme_file_tree_create(LmmeApp *app);
void lmme_file_tree_populate(GtkWidget *tree_view,
                             LmmeWorkspace *workspace,
                             gboolean show_hidden_files,
                             gboolean show_images);
gboolean lmme_file_tree_get_selected(GtkWidget *tree_view,
                                     char **out_path,
                                     LmmeFileKind *out_kind);
gboolean lmme_file_tree_select_at(GtkWidget *tree_view,
                                  double x,
                                  double y,
                                  char **out_path,
                                  LmmeFileKind *out_kind);
gboolean lmme_file_tree_select_path(GtkWidget *tree_view, const char *path);

#endif
