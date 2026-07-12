#include "ui/file_tree_view.h"

#include "app/app.h"
#include "document/tabs.h"
#include "infra/dialogs.h"
#include "ui/window.h"

#define LMME_TYPE_TREE_ITEM (lmme_tree_item_get_type())
G_DECLARE_FINAL_TYPE(LmmeTreeItem, lmme_tree_item, LMME, TREE_ITEM, GObject)

struct _LmmeTreeItem {
    GObject parent_instance;
    LmmeFileNode *node;
    char *path;
    char *name;
    LmmeFileKind kind;
};

G_DEFINE_TYPE(LmmeTreeItem, lmme_tree_item, G_TYPE_OBJECT)

typedef struct {
    LmmeApp *app;
    LmmeWorkspace *workspace;
    gboolean show_hidden_files;
    gboolean show_images;
    GListStore *roots;
    GtkTreeListModel *tree_model;
    GtkSingleSelection *selection;
    gulong activate_handler_id;
    GHashTable *directory_monitors;
    GHashTable *dirty_directories;
    guint monitor_timeout_id;
} LmmeFileTreeState;

static const char *state_key = "lmme-file-tree-state";
static const char *row_path_key = "lmme-tree-path";
static const char *row_kind_key = "lmme-tree-kind";
static const char *row_position_key = "lmme-tree-position";

static void ensure_directory_monitor(LmmeFileTreeState *state, const char *path);

static gboolean
monitor_refresh_timeout(gpointer user_data)
{
    LmmeFileTreeState *state = user_data;
    GHashTableIter iter;
    gpointer key = NULL;

    state->monitor_timeout_id = 0;
    g_hash_table_iter_init(&iter, state->dirty_directories);
    while (g_hash_table_iter_next(&iter, &key, NULL)) {
        (void)lmme_workspace_refresh_directory(state->workspace,
                                               key,
                                               state->show_hidden_files,
                                               state->show_images,
                                               NULL);
    }
    g_hash_table_remove_all(state->dirty_directories);
    lmme_window_refresh_tree(state->app);
    return G_SOURCE_REMOVE;
}

static void
on_directory_monitor_changed(GFileMonitor *monitor,
                             GFile *file,
                             GFile *other_file,
                             GFileMonitorEvent event_type,
                             gpointer user_data)
{
    LmmeFileTreeState *state = user_data;
    const char *directory = g_object_get_data(G_OBJECT(monitor), "lmme-directory-path");
    (void)file;
    (void)other_file;

    if (event_type != G_FILE_MONITOR_EVENT_CREATED &&
        event_type != G_FILE_MONITOR_EVENT_DELETED &&
        event_type != G_FILE_MONITOR_EVENT_MOVED_IN &&
        event_type != G_FILE_MONITOR_EVENT_MOVED_OUT &&
        event_type != G_FILE_MONITOR_EVENT_RENAMED &&
        event_type != G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT) {
        return;
    }
    if (directory != NULL) {
        g_hash_table_add(state->dirty_directories, g_strdup(directory));
    }
    if (state->monitor_timeout_id != 0) {
        g_source_remove(state->monitor_timeout_id);
    }
    state->monitor_timeout_id = g_timeout_add(200, monitor_refresh_timeout, state);
}

static void
ensure_directory_monitor(LmmeFileTreeState *state, const char *path)
{
    g_autoptr(GFile) directory = NULL;
    g_autoptr(GError) error = NULL;
    GFileMonitor *monitor = NULL;

    if (state == NULL || path == NULL || g_hash_table_contains(state->directory_monitors, path)) {
        return;
    }
    directory = g_file_new_for_path(path);
    monitor = g_file_monitor_directory(directory, G_FILE_MONITOR_NONE, NULL, &error);
    if (monitor == NULL) {
        return;
    }
    g_object_set_data_full(G_OBJECT(monitor), "lmme-directory-path", g_strdup(path), g_free);
    g_signal_connect(monitor, "changed", G_CALLBACK(on_directory_monitor_changed), state);
    g_hash_table_insert(state->directory_monitors, g_strdup(path), monitor);
}

static void
lmme_tree_item_finalize(GObject *object)
{
    LmmeTreeItem *item = LMME_TREE_ITEM(object);
    g_free(item->path);
    g_free(item->name);
    G_OBJECT_CLASS(lmme_tree_item_parent_class)->finalize(object);
}

static void
lmme_tree_item_class_init(LmmeTreeItemClass *klass)
{
    G_OBJECT_CLASS(klass)->finalize = lmme_tree_item_finalize;
}

static void
lmme_tree_item_init(LmmeTreeItem *item)
{
    item->node = NULL;
}

static LmmeTreeItem *
tree_item_new(LmmeFileNode *node)
{
    LmmeTreeItem *item = g_object_new(LMME_TYPE_TREE_ITEM, NULL);
    item->node = node;
    item->path = g_strdup(node->path);
    item->name = g_strdup(node->name);
    item->kind = node->kind;
    return item;
}

static const char *
icon_for_item(const LmmeTreeItem *item)
{
    if (item->kind == LMME_FILE_KIND_DIRECTORY) {
        return g_strcmp0(item->name, "img") == 0 ? "folder-pictures-symbolic" : "folder-symbolic";
    }
    if (item->kind == LMME_FILE_KIND_IMAGE) {
        return "image-x-generic-symbolic";
    }
    if (item->kind == LMME_FILE_KIND_MARKDOWN) {
        return "text-x-markdown-symbolic";
    }
    return "text-x-generic-symbolic";
}

static void
file_tree_state_free(LmmeFileTreeState *state)
{
    if (state == NULL) {
        return;
    }
    g_clear_object(&state->selection);
    g_clear_object(&state->tree_model);
    g_clear_object(&state->roots);
    if (state->monitor_timeout_id != 0) {
        g_source_remove(state->monitor_timeout_id);
    }
    g_clear_pointer(&state->directory_monitors, g_hash_table_unref);
    g_clear_pointer(&state->dirty_directories, g_hash_table_unref);
    g_free(state);
}

static GListModel *
create_child_model(gpointer object, gpointer user_data)
{
    LmmeTreeItem *item = LMME_TREE_ITEM(object);
    LmmeFileTreeState *state = user_data;
    GListStore *children = NULL;
    g_autoptr(GError) error = NULL;

    if (item->node == NULL || item->kind != LMME_FILE_KIND_DIRECTORY) {
        return NULL;
    }
    ensure_directory_monitor(state, item->node->path);
    if (!lmme_workspace_load_directory(state->workspace,
                                       item->node,
                                       state->show_hidden_files,
                                       state->show_images,
                                       &error)) {
        g_warning("Could not load workspace directory: %s",
                  error != NULL ? error->message : "unknown error");
    }

    children = g_list_store_new(LMME_TYPE_TREE_ITEM);
    for (guint i = 0; i < item->node->children->len; i++) {
        g_autoptr(LmmeTreeItem) child = tree_item_new(g_ptr_array_index(item->node->children, i));
        g_list_store_append(children, child);
    }
    return G_LIST_MODEL(children);
}

static GtkWidget *
list_item_get_tree_expander(GtkListItem *list_item)
{
    GtkWidget *row_widget = gtk_list_item_get_child(list_item);
    GtkWidget *child = NULL;

    if (row_widget == NULL) {
        return NULL;
    }
    if (GTK_IS_TREE_EXPANDER(row_widget)) {
        return row_widget;
    }
    for (child = gtk_widget_get_first_child(row_widget); child != NULL;
         child = gtk_widget_get_next_sibling(child)) {
        if (GTK_IS_TREE_EXPANDER(child)) {
            return child;
        }
    }
    return NULL;
}

static void
factory_setup(GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *leading = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *expander = gtk_tree_expander_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *icon = gtk_image_new();
    GtkWidget *label = gtk_label_new("");
    (void)factory;
    (void)user_data;

    gtk_widget_set_size_request(leading, 8, -1);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_box_append(GTK_BOX(box), icon);
    gtk_box_append(GTK_BOX(box), label);
    gtk_tree_expander_set_child(GTK_TREE_EXPANDER(expander), box);
    gtk_box_append(GTK_BOX(row), leading);
    gtk_box_append(GTK_BOX(row), expander);
    gtk_widget_set_hexpand(row, TRUE);
    gtk_widget_set_hexpand(expander, TRUE);
    g_object_set_data(G_OBJECT(expander), "lmme-tree-icon", icon);
    g_object_set_data(G_OBJECT(expander), "lmme-tree-label", label);
    gtk_list_item_set_child(list_item, row);
}

static void
factory_bind(GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
    GtkTreeListRow *row = gtk_list_item_get_item(list_item);
    LmmeTreeItem *item = row != NULL ? gtk_tree_list_row_get_item(row) : NULL;
    GtkWidget *expander = list_item_get_tree_expander(list_item);
    GtkWidget *icon = expander != NULL ? g_object_get_data(G_OBJECT(expander), "lmme-tree-icon") : NULL;
    GtkWidget *label = expander != NULL ? g_object_get_data(G_OBJECT(expander), "lmme-tree-label") : NULL;
    (void)factory;
    (void)user_data;

    if (item == NULL || item->node == NULL || expander == NULL || icon == NULL || label == NULL) {
        return;
    }
    gtk_tree_expander_set_list_row(GTK_TREE_EXPANDER(expander), row);
    gtk_image_set_from_icon_name(GTK_IMAGE(icon), icon_for_item(item));
    gtk_label_set_text(GTK_LABEL(label), item->name);
    g_object_set_data_full(G_OBJECT(expander), row_path_key, g_strdup(item->path), g_free);
    g_object_set_data(G_OBJECT(expander), row_kind_key, GINT_TO_POINTER((int)item->kind));
    g_object_set_data(G_OBJECT(expander),
                      row_position_key,
                      GUINT_TO_POINTER(gtk_tree_list_row_get_position(row) + 1));
}

static void
factory_unbind(GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
    GtkWidget *expander = list_item_get_tree_expander(list_item);
    (void)factory;
    (void)user_data;

    if (expander == NULL) {
        return;
    }
    gtk_tree_expander_set_list_row(GTK_TREE_EXPANDER(expander), NULL);
    g_object_set_data_full(G_OBJECT(expander), row_path_key, NULL, NULL);
    g_object_set_data(G_OBJECT(expander), row_kind_key, NULL);
    g_object_set_data(G_OBJECT(expander), row_position_key, NULL);
}

static void
sync_app_selection(LmmeFileTreeState *state)
{
    GtkTreeListRow *row = gtk_single_selection_get_selected_item(state->selection);
    LmmeTreeItem *item = row != NULL ? gtk_tree_list_row_get_item(row) : NULL;

    g_clear_pointer(&state->app->selection.path, g_free);
    state->app->selection.kind = LMME_FILE_KIND_OTHER;
    state->app->selection.empty_area = FALSE;
    if (item == NULL || item->node == NULL) {
        return;
    }
    state->app->selection.path = g_strdup(item->path);
    state->app->selection.kind = item->kind;
}

static void
on_selection_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    (void)object;
    (void)pspec;
    sync_app_selection(user_data);
}

static void
on_activate(GtkListView *view, guint position, gpointer user_data)
{
    LmmeFileTreeState *state = user_data;
    g_autoptr(GtkTreeListRow) row = gtk_tree_list_model_get_row(state->tree_model, position);
    LmmeTreeItem *item = row != NULL ? gtk_tree_list_row_get_item(row) : NULL;
    (void)view;

    if (item == NULL || item->node == NULL) {
        return;
    }
    gtk_single_selection_set_selected(state->selection, position);
    if (item->kind == LMME_FILE_KIND_DIRECTORY) {
        gtk_tree_list_row_set_expanded(row, !gtk_tree_list_row_get_expanded(row));
    } else if (item->kind == LMME_FILE_KIND_MARKDOWN) {
        g_autoptr(GError) error = NULL;
        if (!lmme_tabs_open_file(state->app, item->path, &error)) {
            lmme_dialog_error(GTK_WINDOW(state->app->window),
                              "Could not read file.",
                              error != NULL ? error->message : NULL);
        }
    }
}

GtkWidget *
lmme_file_tree_create(LmmeApp *app)
{
    GtkListItemFactory *factory = GTK_LIST_ITEM_FACTORY(gtk_signal_list_item_factory_new());
    GtkWidget *view = gtk_list_view_new(NULL, factory);

    gtk_list_view_set_single_click_activate(GTK_LIST_VIEW(view), TRUE);
    g_signal_connect(factory, "setup", G_CALLBACK(factory_setup), NULL);
    g_signal_connect(factory, "bind", G_CALLBACK(factory_bind), NULL);
    g_signal_connect(factory, "unbind", G_CALLBACK(factory_unbind), NULL);
    g_object_set_data(G_OBJECT(view), "lmme-app", app);
    gtk_widget_set_hexpand(view, TRUE);
    gtk_widget_set_vexpand(view, TRUE);
    return view;
}

static GHashTable *
expanded_paths(GtkWidget *tree_view)
{
    LmmeFileTreeState *state = g_object_get_data(G_OBJECT(tree_view), state_key);
    GHashTable *paths = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    if (state == NULL) {
        return paths;
    }
    guint count = g_list_model_get_n_items(G_LIST_MODEL(state->tree_model));
    for (guint i = 0; i < count; i++) {
        g_autoptr(GtkTreeListRow) row = gtk_tree_list_model_get_row(state->tree_model, i);
        LmmeTreeItem *item = row != NULL ? gtk_tree_list_row_get_item(row) : NULL;
        if (row != NULL && item != NULL && gtk_tree_list_row_get_expanded(row)) {
            g_hash_table_add(paths, g_strdup(item->path));
        }
    }
    return paths;
}

void
lmme_file_tree_populate(GtkWidget *tree_view,
                        LmmeWorkspace *workspace,
                        gboolean show_hidden_files,
                        gboolean show_images)
{
    g_autoptr(GHashTable) expanded = expanded_paths(tree_view);
    g_autofree char *selected_path = NULL;
    LmmeFileKind selected_kind = LMME_FILE_KIND_OTHER;
    LmmeFileTreeState *old_state = g_object_get_data(G_OBJECT(tree_view), state_key);
    LmmeFileTreeState *state = NULL;

    (void)lmme_file_tree_get_selected(tree_view, &selected_path, &selected_kind);
    (void)selected_kind;
    gtk_list_view_set_model(GTK_LIST_VIEW(tree_view), NULL);
    if (old_state != NULL) {
        if (old_state->activate_handler_id != 0) {
            g_signal_handler_disconnect(tree_view, old_state->activate_handler_id);
        }
        g_object_set_data(G_OBJECT(tree_view), state_key, NULL);
    }
    if (workspace == NULL || workspace->root == NULL) {
        return;
    }

    state = g_new0(LmmeFileTreeState, 1);
    state->app = g_object_get_data(G_OBJECT(tree_view), "lmme-app");
    state->workspace = workspace;
    state->show_hidden_files = show_hidden_files;
    state->show_images = show_images;
    state->directory_monitors = g_hash_table_new_full(g_str_hash,
                                                       g_str_equal,
                                                       g_free,
                                                       g_object_unref);
    state->dirty_directories = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    state->roots = g_list_store_new(LMME_TYPE_TREE_ITEM);
    g_autoptr(LmmeTreeItem) root_item = tree_item_new(workspace->root);
    g_list_store_append(state->roots, root_item);
    state->tree_model = gtk_tree_list_model_new(G_LIST_MODEL(g_object_ref(state->roots)),
                                                FALSE,
                                                FALSE,
                                                create_child_model,
                                                state,
                                                NULL);
    state->selection = gtk_single_selection_new(G_LIST_MODEL(g_object_ref(state->tree_model)));
    gtk_single_selection_set_autoselect(state->selection, FALSE);
    gtk_single_selection_set_can_unselect(state->selection, TRUE);
    g_signal_connect(state->selection, "notify::selected-item", G_CALLBACK(on_selection_changed), state);
    gtk_list_view_set_model(GTK_LIST_VIEW(tree_view), GTK_SELECTION_MODEL(state->selection));
    state->activate_handler_id = g_signal_connect(tree_view, "activate", G_CALLBACK(on_activate), state);
    g_object_set_data_full(G_OBJECT(tree_view), state_key, state, (GDestroyNotify)file_tree_state_free);
    ensure_directory_monitor(state, workspace->root->path);

    g_hash_table_add(expanded, g_strdup(workspace->root->path));
    for (guint i = 0; i < g_list_model_get_n_items(G_LIST_MODEL(state->tree_model)); i++) {
        g_autoptr(GtkTreeListRow) row = gtk_tree_list_model_get_row(state->tree_model, i);
        LmmeTreeItem *item = row != NULL ? gtk_tree_list_row_get_item(row) : NULL;
        if (item != NULL && g_hash_table_contains(expanded, item->path)) {
            gtk_tree_list_row_set_expanded(row, TRUE);
        }
    }
    if (selected_path != NULL) {
        guint count = g_list_model_get_n_items(G_LIST_MODEL(state->tree_model));
        for (guint i = 0; i < count; i++) {
            g_autoptr(GtkTreeListRow) row = gtk_tree_list_model_get_row(state->tree_model, i);
            LmmeTreeItem *item = row != NULL ? gtk_tree_list_row_get_item(row) : NULL;
            if (item != NULL && g_strcmp0(item->path, selected_path) == 0) {
                gtk_single_selection_set_selected(state->selection, i);
                break;
            }
        }
    }
}

gboolean
lmme_file_tree_get_selected(GtkWidget *tree_view, char **out_path, LmmeFileKind *out_kind)
{
    LmmeFileTreeState *state = g_object_get_data(G_OBJECT(tree_view), state_key);
    GtkTreeListRow *row = state != NULL ? gtk_single_selection_get_selected_item(state->selection) : NULL;
    LmmeTreeItem *item = row != NULL ? gtk_tree_list_row_get_item(row) : NULL;

    if (item == NULL || item->node == NULL) {
        return FALSE;
    }
    if (out_path != NULL) {
        *out_path = g_strdup(item->path);
    }
    if (out_kind != NULL) {
        *out_kind = item->kind;
    }
    return TRUE;
}

gboolean
lmme_file_tree_select_at(GtkWidget *tree_view,
                         double x,
                         double y,
                         char **out_path,
                         LmmeFileKind *out_kind)
{
    LmmeFileTreeState *state = g_object_get_data(G_OBJECT(tree_view), state_key);
    GtkWidget *picked = gtk_widget_pick(tree_view, x, y, GTK_PICK_DEFAULT);

    while (picked != NULL && picked != tree_view &&
           g_object_get_data(G_OBJECT(picked), row_position_key) == NULL) {
        picked = gtk_widget_get_parent(picked);
    }
    if (state == NULL || picked == NULL || picked == tree_view) {
        if (state != NULL) {
            gtk_single_selection_set_selected(state->selection, GTK_INVALID_LIST_POSITION);
        }
        return FALSE;
    }
    guint position = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(picked), row_position_key)) - 1;
    gtk_single_selection_set_selected(state->selection, position);
    return lmme_file_tree_get_selected(tree_view, out_path, out_kind);
}
