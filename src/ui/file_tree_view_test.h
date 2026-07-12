#ifndef LMME_UI_FILE_TREE_VIEW_TEST_H
#define LMME_UI_FILE_TREE_VIEW_TEST_H

#include <gtk/gtk.h>

/* Test-only accessors for retaining the exact cached row/store across refresh. */
gboolean lmme_file_tree_test_expand_path(GtkWidget *tree_view, const char *path);
GObject *lmme_file_tree_test_ref_item(GtkWidget *tree_view, const char *path);
GListStore *lmme_file_tree_test_ref_child_store(GtkWidget *tree_view, const char *path);
GListModel *lmme_file_tree_test_create_child_model(GtkWidget *tree_view, GObject *item);
char *lmme_file_tree_test_dup_item_path(GObject *item);

#endif
