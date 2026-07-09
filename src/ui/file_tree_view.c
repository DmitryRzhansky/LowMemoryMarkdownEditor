#include "ui/file_tree_view.h"

static const char *
icon_for_node(const LmmeFileNode *node)
{
    if (node->kind == LMME_FILE_KIND_DIRECTORY) {
        return g_strcmp0(node->name, "img") == 0 ? "folder-pictures-symbolic" : "folder-symbolic";
    }
    if (node->kind == LMME_FILE_KIND_IMAGE) {
        return "image-x-generic-symbolic";
    }
    if (node->kind == LMME_FILE_KIND_MARKDOWN) {
        return "text-x-markdown-symbolic";
    }
    return "text-x-generic-symbolic";
}

static void
append_node(GtkTreeStore *store, GtkTreeIter *parent, const LmmeFileNode *node)
{
    GtkTreeIter iter;

    gtk_tree_store_append(store, &iter, parent);
    gtk_tree_store_set(store,
                       &iter,
                       LMME_TREE_COL_ICON,
                       icon_for_node(node),
                       LMME_TREE_COL_NAME,
                       node->name,
                       LMME_TREE_COL_PATH,
                       node->path,
                       LMME_TREE_COL_KIND,
                       (int)node->kind,
                       -1);

    if (node->children != NULL) {
        for (guint i = 0; i < node->children->len; i++) {
            append_node(store, &iter, g_ptr_array_index(node->children, i));
        }
    }
}

GtkWidget *
lmme_file_tree_create(void)
{
    GtkTreeStore *store = gtk_tree_store_new(LMME_TREE_N_COLS,
                                             G_TYPE_STRING,
                                             G_TYPE_STRING,
                                             G_TYPE_STRING,
                                             G_TYPE_INT);
    GtkWidget *tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    GtkCellRenderer *icon_renderer = gtk_cell_renderer_pixbuf_new();
    GtkCellRenderer *text_renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *column = gtk_tree_view_column_new();

    gtk_tree_view_column_pack_start(column, icon_renderer, FALSE);
    gtk_tree_view_column_add_attribute(column, icon_renderer, "icon-name", LMME_TREE_COL_ICON);
    gtk_tree_view_column_pack_start(column, text_renderer, TRUE);
    gtk_tree_view_column_add_attribute(column, text_renderer, "text", LMME_TREE_COL_NAME);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree), FALSE);
    gtk_tree_view_set_activate_on_single_click(GTK_TREE_VIEW(tree), TRUE);
    gtk_widget_set_hexpand(tree, TRUE);
    gtk_widget_set_vexpand(tree, TRUE);

    g_object_unref(store);
    return tree;
}

void
lmme_file_tree_populate(GtkWidget *tree_view, LmmeFileNode *root)
{
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(tree_view));
    GtkTreeStore *store = GTK_TREE_STORE(model);

    gtk_tree_store_clear(store);
    if (root != NULL) {
        GtkTreePath *first = gtk_tree_path_new_first();
        append_node(store, NULL, root);
        gtk_tree_view_expand_row(GTK_TREE_VIEW(tree_view), first, FALSE);
        gtk_tree_path_free(first);
    }
}

gboolean
lmme_file_tree_get_selected(GtkWidget *tree_view, char **out_path, LmmeFileKind *out_kind)
{
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
    GtkTreeModel *model = NULL;
    GtkTreeIter iter;
    int kind = 0;
    char *path = NULL;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
        return FALSE;
    }

    gtk_tree_model_get(model,
                       &iter,
                       LMME_TREE_COL_PATH,
                       &path,
                       LMME_TREE_COL_KIND,
                       &kind,
                       -1);

    if (out_path != NULL) {
        *out_path = path;
    } else {
        g_free(path);
    }

    if (out_kind != NULL) {
        *out_kind = (LmmeFileKind)kind;
    }

    return TRUE;
}
