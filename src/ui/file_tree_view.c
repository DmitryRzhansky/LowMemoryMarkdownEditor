#include "ui/file_tree_view.h"
#include "ui/file_tree_view_test.h"

#include "app/app.h"
#include "document/tabs.h"
#include "infra/dialogs.h"
#include "ui/window.h"

#define LMME_TYPE_TREE_ITEM (lmme_tree_item_get_type())
G_DECLARE_FINAL_TYPE(LmmeTreeItem, lmme_tree_item, LMME, TREE_ITEM, GObject)

struct _LmmeTreeItem {
    GObject parent_instance;
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
    GHashTable *child_stores;
    GHashTable *directory_monitors;
    GHashTable *dirty_directories;
    GHashTable *refresh_counts;
    guint monitor_timeout_id;
    gboolean use_test_monitors;
} LmmeFileTreeState;

struct _LmmeFileTreeTestModel {
    LmmeFileTreeState *state;
};

static const char *state_key = "lmme-file-tree-state";
static const char *row_path_key = "lmme-tree-path";
static const char *row_kind_key = "lmme-tree-kind";
static const char *row_position_key = "lmme-tree-position";

static void ensure_directory_monitor(LmmeFileTreeState *state, const char *path);
static void refresh_dirty_directories(LmmeFileTreeState *state);

static gboolean
monitor_refresh_timeout(gpointer user_data)
{
    LmmeFileTreeState *state = user_data;
    state->monitor_timeout_id = 0;
    refresh_dirty_directories(state);
    g_hash_table_remove_all(state->dirty_directories);
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
    if (state->use_test_monitors) {
        g_hash_table_insert(state->directory_monitors,
                            g_strdup(path),
                            g_object_new(G_TYPE_OBJECT, NULL));
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
    (void)item;
}

static LmmeTreeItem *
tree_item_new(LmmeFileNode *node)
{
    LmmeTreeItem *item = g_object_new(LMME_TYPE_TREE_ITEM, NULL);
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
    GHashTableIter iter;
    gpointer value = NULL;

    if (state == NULL) {
        return;
    }
    g_clear_object(&state->selection);
    g_clear_object(&state->tree_model);
    if (state->roots != NULL) {
        g_list_store_remove_all(state->roots);
    }
    if (state->child_stores != NULL) {
        g_hash_table_iter_init(&iter, state->child_stores);
        while (g_hash_table_iter_next(&iter, NULL, &value)) {
            g_list_store_remove_all(G_LIST_STORE(value));
        }
    }
    g_clear_object(&state->roots);
    if (state->monitor_timeout_id != 0) {
        g_source_remove(state->monitor_timeout_id);
    }
    g_clear_pointer(&state->child_stores, g_hash_table_unref);
    g_clear_pointer(&state->directory_monitors, g_hash_table_unref);
    g_clear_pointer(&state->dirty_directories, g_hash_table_unref);
    g_clear_pointer(&state->refresh_counts, g_hash_table_unref);
    g_free(state);
}

static GListModel *
create_child_model(gpointer object, gpointer user_data)
{
    LmmeTreeItem *item = LMME_TREE_ITEM(object);
    LmmeFileTreeState *state = user_data;
    LmmeFileNode *node = NULL;
    GListStore *children = NULL;
    g_autoptr(GError) error = NULL;

    if (state == NULL || item->kind != LMME_FILE_KIND_DIRECTORY) {
        return NULL;
    }
    children = g_hash_table_lookup(state->child_stores, item->path);
    if (children != NULL) {
        return G_LIST_MODEL(g_object_ref(children));
    }
    ensure_directory_monitor(state, item->path);
    if (!lmme_workspace_load_directory_path(state->workspace,
                                            item->path,
                                            state->show_hidden_files,
                                            state->show_images,
                                            &error)) {
        g_warning("Could not load workspace directory: %s",
                  error != NULL ? error->message : "unknown error");
        return NULL;
    }
    node = lmme_workspace_find_node(state->workspace, item->path);
    if (node == NULL || node->children == NULL) {
        return NULL;
    }

    children = g_list_store_new(LMME_TYPE_TREE_ITEM);
    for (guint i = 0; i < node->children->len; i++) {
        g_autoptr(LmmeTreeItem) child = tree_item_new(g_ptr_array_index(node->children, i));
        g_list_store_append(children, child);
    }
    g_hash_table_insert(state->child_stores,
                        g_strdup(item->path),
                        g_object_ref(children));
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
    g_autoptr(LmmeTreeItem) item = row != NULL ? gtk_tree_list_row_get_item(row) : NULL;
    GtkWidget *expander = list_item_get_tree_expander(list_item);
    GtkWidget *icon = expander != NULL ? g_object_get_data(G_OBJECT(expander), "lmme-tree-icon") : NULL;
    GtkWidget *label = expander != NULL ? g_object_get_data(G_OBJECT(expander), "lmme-tree-label") : NULL;
    (void)factory;
    (void)user_data;

    if (item == NULL || expander == NULL || icon == NULL || label == NULL) {
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
    g_autoptr(LmmeTreeItem) item = row != NULL ? gtk_tree_list_row_get_item(row) : NULL;

    lmme_path_context_clear(&state->app->selection);
    if (item == NULL) {
        return;
    }
    lmme_path_context_set(&state->app->selection, item->path, item->kind, FALSE);
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
    g_autoptr(LmmeTreeItem) item = row != NULL ? gtk_tree_list_row_get_item(row) : NULL;
    (void)view;

    if (item == NULL) {
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
expanded_paths_for_state(LmmeFileTreeState *state)
{
    GHashTable *paths = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    if (state == NULL) {
        return paths;
    }
    guint count = g_list_model_get_n_items(G_LIST_MODEL(state->tree_model));
    for (guint i = 0; i < count; i++) {
        g_autoptr(GtkTreeListRow) row = gtk_tree_list_model_get_row(state->tree_model, i);
        g_autoptr(LmmeTreeItem) item = row != NULL ? gtk_tree_list_row_get_item(row) : NULL;
        if (row != NULL && item != NULL && gtk_tree_list_row_get_expanded(row)) {
            g_hash_table_add(paths, g_strdup(item->path));
        }
    }
    return paths;
}

static GHashTable *
expanded_paths(GtkWidget *tree_view)
{
    return expanded_paths_for_state(g_object_get_data(G_OBJECT(tree_view), state_key));
}

static void
restore_expanded_paths(LmmeFileTreeState *state, GHashTable *paths)
{
    gboolean expanded_any = FALSE;

    do {
        guint count = g_list_model_get_n_items(G_LIST_MODEL(state->tree_model));
        expanded_any = FALSE;
        for (guint i = 0; i < count; i++) {
            g_autoptr(GtkTreeListRow) row = gtk_tree_list_model_get_row(state->tree_model, i);
            g_autoptr(LmmeTreeItem) item = row != NULL ? gtk_tree_list_row_get_item(row) : NULL;
            if (item != NULL && item->kind == LMME_FILE_KIND_DIRECTORY &&
                g_hash_table_contains(paths, item->path) &&
                !gtk_tree_list_row_get_expanded(row)) {
                gtk_tree_list_row_set_expanded(row, TRUE);
                expanded_any = gtk_tree_list_row_get_expanded(row) || expanded_any;
            }
        }
    } while (expanded_any);
}

static void
replace_child_store_items(GListStore *store, LmmeFileNode *directory)
{
    g_list_store_remove_all(store);
    for (guint i = 0; i < directory->children->len; i++) {
        g_autoptr(LmmeTreeItem) child = tree_item_new(g_ptr_array_index(directory->children, i));
        g_list_store_append(store, child);
    }
}

static void
prune_removed_directories(LmmeFileTreeState *state)
{
    GHashTableIter iter;
    gpointer key = NULL;

    g_hash_table_iter_init(&iter, state->child_stores);
    while (g_hash_table_iter_next(&iter, &key, NULL)) {
        LmmeFileNode *node = lmme_workspace_find_node(state->workspace, key);
        if (node == NULL || node->kind != LMME_FILE_KIND_DIRECTORY) {
            g_hash_table_iter_remove(&iter);
        }
    }
    g_hash_table_iter_init(&iter, state->directory_monitors);
    while (g_hash_table_iter_next(&iter, &key, NULL)) {
        LmmeFileNode *node = lmme_workspace_find_node(state->workspace, key);
        if (node == NULL || node->kind != LMME_FILE_KIND_DIRECTORY) {
            g_hash_table_iter_remove(&iter);
        }
    }
}

static gboolean
refresh_directory_state(LmmeFileTreeState *state,
                        const char *directory_path,
                        GError **error)
{
    LmmeFileNode *node = NULL;
    GListStore *store = NULL;

    if (!lmme_workspace_refresh_directory(state->workspace,
                                          directory_path,
                                          state->show_hidden_files,
                                          state->show_images,
                                          error)) {
        return FALSE;
    }
    if (state->refresh_counts != NULL) {
        guint *count = g_hash_table_lookup(state->refresh_counts, directory_path);

        if (count == NULL) {
            guint *initial = g_new(guint, 1);
            *initial = 1;
            g_hash_table_insert(state->refresh_counts, g_strdup(directory_path), initial);
        } else {
            (*count)++;
        }
    }
    node = lmme_workspace_find_node(state->workspace, directory_path);
    store = g_hash_table_lookup(state->child_stores, directory_path);
    if (node != NULL && store != NULL) {
        replace_child_store_items(store, node);
    }
    return TRUE;
}

static void
refresh_dirty_directories(LmmeFileTreeState *state)
{
    g_autoptr(GHashTable) expanded = expanded_paths(state->app->tree_view);
    g_autofree char *selected_path = g_strdup(state->app->selection.path);
    GHashTableIter iter;
    gpointer key = NULL;

    g_hash_table_iter_init(&iter, state->dirty_directories);
    while (g_hash_table_iter_next(&iter, &key, NULL)) {
        (void)refresh_directory_state(state, key, NULL);
    }
    prune_removed_directories(state);
    restore_expanded_paths(state, expanded);
    if (selected_path == NULL || !lmme_file_tree_select_path(state->app->tree_view, selected_path)) {
        lmme_path_context_clear(&state->app->selection);
    }
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
    LmmeApp *app = g_object_get_data(G_OBJECT(tree_view), "lmme-app");

    (void)lmme_file_tree_get_selected(tree_view, &selected_path, &selected_kind);
    (void)selected_kind;
    if (app != NULL) {
        lmme_path_context_clear(&app->selection);
        lmme_path_context_clear(&app->tree_context);
    }
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
    state->child_stores = g_hash_table_new_full(g_str_hash,
                                                 g_str_equal,
                                                 g_free,
                                                 g_object_unref);
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
    restore_expanded_paths(state, expanded);
    if (selected_path != NULL) {
        guint count = g_list_model_get_n_items(G_LIST_MODEL(state->tree_model));
        for (guint i = 0; i < count; i++) {
            g_autoptr(GtkTreeListRow) row = gtk_tree_list_model_get_row(state->tree_model, i);
            g_autoptr(LmmeTreeItem) item = row != NULL ? gtk_tree_list_row_get_item(row) : NULL;
            if (item != NULL && g_strcmp0(item->path, selected_path) == 0) {
                gtk_single_selection_set_selected(state->selection, i);
                break;
            }
        }
    }
}

gboolean
lmme_file_tree_refresh_directory(GtkWidget *tree_view,
                                 const char *directory_path,
                                 GError **error)
{
    LmmeFileTreeState *state = g_object_get_data(G_OBJECT(tree_view), state_key);
    g_autoptr(GHashTable) expanded = NULL;
    g_autofree char *selected_path = NULL;

    if (state == NULL || directory_path == NULL) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "File tree is not ready.");
        return FALSE;
    }
    expanded = expanded_paths(tree_view);
    selected_path = g_strdup(state->app->selection.path);
    if (!refresh_directory_state(state, directory_path, error)) {
        return FALSE;
    }
    prune_removed_directories(state);
    restore_expanded_paths(state, expanded);
    if (selected_path == NULL || !lmme_file_tree_select_path(tree_view, selected_path)) {
        lmme_path_context_clear(&state->app->selection);
    }
    return TRUE;
}

gpointer
lmme_file_tree_model_identity(GtkWidget *tree_view)
{
    LmmeFileTreeState *state = g_object_get_data(G_OBJECT(tree_view), state_key);
    return state != NULL ? state->tree_model : NULL;
}

guint
lmme_file_tree_monitor_count(GtkWidget *tree_view)
{
    LmmeFileTreeState *state = g_object_get_data(G_OBJECT(tree_view), state_key);
    return state != NULL ? g_hash_table_size(state->directory_monitors) : 0;
}

gboolean
lmme_file_tree_get_selected(GtkWidget *tree_view, char **out_path, LmmeFileKind *out_kind)
{
    LmmeFileTreeState *state = g_object_get_data(G_OBJECT(tree_view), state_key);
    GtkTreeListRow *row = state != NULL ? gtk_single_selection_get_selected_item(state->selection) : NULL;
    g_autoptr(LmmeTreeItem) item = row != NULL ? gtk_tree_list_row_get_item(row) : NULL;

    if (item == NULL) {
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

gboolean
lmme_file_tree_select_path(GtkWidget *tree_view, const char *path)
{
    LmmeFileTreeState *state = g_object_get_data(G_OBJECT(tree_view), state_key);

    if (state == NULL || path == NULL) {
        return FALSE;
    }
    for (guint i = 0; i < g_list_model_get_n_items(G_LIST_MODEL(state->tree_model)); i++) {
        g_autoptr(GtkTreeListRow) row = gtk_tree_list_model_get_row(state->tree_model, i);
        g_autoptr(LmmeTreeItem) item = row != NULL ? gtk_tree_list_row_get_item(row) : NULL;
        if (item != NULL && g_strcmp0(item->path, path) == 0) {
            gtk_single_selection_set_selected(state->selection, i);
            return TRUE;
        }
    }
    gtk_single_selection_set_selected(state->selection, GTK_INVALID_LIST_POSITION);
    return FALSE;
}

LmmeFileTreeTestModel *
lmme_file_tree_test_model_new(LmmeApp *app,
                              LmmeWorkspace *workspace,
                              gboolean show_hidden_files,
                              gboolean show_images)
{
    LmmeFileTreeTestModel *model = NULL;
    LmmeFileTreeState *state = NULL;

    if (app == NULL || workspace == NULL || workspace->root == NULL) {
        return NULL;
    }
    model = g_new0(LmmeFileTreeTestModel, 1);
    state = g_new0(LmmeFileTreeState, 1);
    model->state = state;
    state->app = app;
    state->workspace = workspace;
    state->show_hidden_files = show_hidden_files;
    state->show_images = show_images;
    state->use_test_monitors = TRUE;
    state->child_stores = g_hash_table_new_full(g_str_hash,
                                                g_str_equal,
                                                g_free,
                                                g_object_unref);
    state->directory_monitors = g_hash_table_new_full(g_str_hash,
                                                      g_str_equal,
                                                      g_free,
                                                      g_object_unref);
    state->dirty_directories = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    state->refresh_counts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    state->roots = g_list_store_new(LMME_TYPE_TREE_ITEM);
    g_autoptr(LmmeTreeItem) root_item = tree_item_new(workspace->root);
    g_list_store_append(state->roots, root_item);
    state->tree_model = gtk_tree_list_model_new(G_LIST_MODEL(g_object_ref(state->roots)),
                                                FALSE,
                                                FALSE,
                                                create_child_model,
                                                state,
                                                NULL);
    ensure_directory_monitor(state, workspace->root->path);
    return model;
}

static gboolean
file_tree_test_has_monitor(LmmeFileTreeState *state, const char *path)
{
    return state != NULL && path != NULL && g_hash_table_contains(state->directory_monitors, path);
}

static guint
file_tree_test_refresh_count(LmmeFileTreeState *state, const char *path)
{
    guint *count = NULL;

    if (state == NULL || path == NULL || state->refresh_counts == NULL) {
        return 0;
    }
    count = g_hash_table_lookup(state->refresh_counts, path);
    return count != NULL ? *count : 0;
}

void
lmme_file_tree_test_model_free(LmmeFileTreeTestModel *model)
{
    g_autoptr(GtkTreeListRow) root_row = NULL;

    if (model == NULL) {
        return;
    }
    root_row = gtk_tree_list_model_get_row(model->state->tree_model, 0);
    if (root_row != NULL) {
        gtk_tree_list_row_set_expanded(root_row, FALSE);
    }
    g_clear_object(&root_row);
    file_tree_state_free(model->state);
    g_free(model);
}

gboolean
lmme_file_tree_test_expand_path(LmmeFileTreeTestModel *model, const char *path)
{
    LmmeFileTreeState *state = model != NULL ? model->state : NULL;

    if (state == NULL || path == NULL) {
        return FALSE;
    }
    for (guint i = 0; i < g_list_model_get_n_items(G_LIST_MODEL(state->tree_model)); i++) {
        g_autoptr(GtkTreeListRow) row = gtk_tree_list_model_get_row(state->tree_model, i);
        g_autoptr(LmmeTreeItem) item = row != NULL ? gtk_tree_list_row_get_item(row) : NULL;
        if (item != NULL && g_strcmp0(item->path, path) == 0) {
            gtk_tree_list_row_set_expanded(row, TRUE);
            return gtk_tree_list_row_get_expanded(row);
        }
    }
    return FALSE;
}

gboolean
lmme_file_tree_test_refresh_directory(LmmeFileTreeTestModel *model,
                                      const char *path,
                                      GError **error)
{
    LmmeFileTreeState *state = model != NULL ? model->state : NULL;
    g_autoptr(GHashTable) expanded = NULL;

    if (state == NULL || path == NULL) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Invalid file-tree test model.");
        return FALSE;
    }
    expanded = expanded_paths_for_state(state);
    if (!refresh_directory_state(state, path, error)) {
        return FALSE;
    }
    prune_removed_directories(state);
    restore_expanded_paths(state, expanded);
    return TRUE;
}

gboolean
lmme_file_tree_test_contains_path(LmmeFileTreeTestModel *model, const char *path)
{
    LmmeFileTreeState *state = model != NULL ? model->state : NULL;

    if (state == NULL || path == NULL) {
        return FALSE;
    }
    for (guint i = 0; i < g_list_model_get_n_items(G_LIST_MODEL(state->tree_model)); i++) {
        g_autoptr(GtkTreeListRow) row = gtk_tree_list_model_get_row(state->tree_model, i);
        g_autoptr(LmmeTreeItem) item = row != NULL ? gtk_tree_list_row_get_item(row) : NULL;
        if (item != NULL && g_strcmp0(item->path, path) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

gpointer
lmme_file_tree_test_model_identity(LmmeFileTreeTestModel *model)
{
    return model != NULL ? model->state->tree_model : NULL;
}

guint
lmme_file_tree_test_monitor_count(LmmeFileTreeTestModel *model)
{
    return model != NULL ? g_hash_table_size(model->state->directory_monitors) : 0;
}

GObject *
lmme_file_tree_test_ref_item(LmmeFileTreeTestModel *model, const char *path)
{
    LmmeFileTreeState *state = model != NULL ? model->state : NULL;

    if (state == NULL || path == NULL) {
        return NULL;
    }
    for (guint i = 0; i < g_list_model_get_n_items(G_LIST_MODEL(state->tree_model)); i++) {
        g_autoptr(GtkTreeListRow) row = gtk_tree_list_model_get_row(state->tree_model, i);
        g_autoptr(LmmeTreeItem) item = row != NULL ? gtk_tree_list_row_get_item(row) : NULL;
        if (item != NULL && g_strcmp0(item->path, path) == 0) {
            return g_object_ref(G_OBJECT(item));
        }
    }
    return NULL;
}

GListStore *
lmme_file_tree_test_ref_child_store(LmmeFileTreeTestModel *model, const char *path)
{
    LmmeFileTreeState *state = model != NULL ? model->state : NULL;
    GListStore *store = state != NULL && path != NULL
                          ? g_hash_table_lookup(state->child_stores, path)
                          : NULL;

    return store != NULL ? g_object_ref(store) : NULL;
}

GListModel *
lmme_file_tree_test_create_child_model(LmmeFileTreeTestModel *model, GObject *item)
{
    LmmeFileTreeState *state = model != NULL ? model->state : NULL;

    if (state == NULL || !LMME_IS_TREE_ITEM(item)) {
        return NULL;
    }
    return create_child_model(item, state);
}

char *
lmme_file_tree_test_dup_item_path(GObject *item)
{
    return LMME_IS_TREE_ITEM(item) ? g_strdup(LMME_TREE_ITEM(item)->path) : NULL;
}

gboolean
lmme_file_tree_test_has_monitor(LmmeFileTreeTestModel *model, const char *path)
{
    return file_tree_test_has_monitor(model != NULL ? model->state : NULL, path);
}

guint
lmme_file_tree_test_refresh_count(LmmeFileTreeTestModel *model, const char *path)
{
    return file_tree_test_refresh_count(model != NULL ? model->state : NULL, path);
}
