#ifndef LMME_UI_FILE_TREE_VIEW_TEST_H
#define LMME_UI_FILE_TREE_VIEW_TEST_H

#include <gtk/gtk.h>

typedef struct _LmmeApp LmmeApp;
typedef struct _LmmeWorkspace LmmeWorkspace;
typedef struct _LmmeFileTreeTestModel LmmeFileTreeTestModel;

/* Test-only accessors for retaining the exact cached row/store across refresh. */
LmmeFileTreeTestModel *lmme_file_tree_test_model_new(LmmeApp *app,
                                                     LmmeWorkspace *workspace,
                                                     gboolean show_hidden_files,
                                                     gboolean show_images);
void lmme_file_tree_test_model_free(LmmeFileTreeTestModel *model);
gboolean lmme_file_tree_test_expand_path(LmmeFileTreeTestModel *model, const char *path);
gboolean lmme_file_tree_test_refresh_directory(LmmeFileTreeTestModel *model,
                                               const char *path,
                                               GError **error);
gboolean lmme_file_tree_test_contains_path(LmmeFileTreeTestModel *model, const char *path);
gpointer lmme_file_tree_test_model_identity(LmmeFileTreeTestModel *model);
guint lmme_file_tree_test_monitor_count(LmmeFileTreeTestModel *model);
GObject *lmme_file_tree_test_ref_item(LmmeFileTreeTestModel *model, const char *path);
GListStore *lmme_file_tree_test_ref_child_store(LmmeFileTreeTestModel *model, const char *path);
GListModel *lmme_file_tree_test_create_child_model(LmmeFileTreeTestModel *model, GObject *item);
char *lmme_file_tree_test_dup_item_path(GObject *item);

#endif
