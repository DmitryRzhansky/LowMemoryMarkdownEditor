#include "workspace.h"

#include <errno.h>
#include <string.h>

#include <gio/gio.h>
#include <glib/gstdio.h>

static LmmeFileNode *scan_dir(const char *path,
                              gboolean is_root,
                              gboolean show_hidden_files,
                              gboolean show_images,
                              GHashTable *visited,
                              GError **error);

static int
kind_sort_rank(LmmeFileKind kind)
{
    switch (kind) {
    case LMME_FILE_KIND_DIRECTORY:
        return 0;
    case LMME_FILE_KIND_MARKDOWN:
        return 1;
    case LMME_FILE_KIND_IMAGE:
        return 2;
    case LMME_FILE_KIND_OTHER:
    default:
        return 3;
    }
}

static gint
node_compare(gconstpointer a, gconstpointer b)
{
    const LmmeFileNode *left = *(const LmmeFileNode * const *)a;
    const LmmeFileNode *right = *(const LmmeFileNode * const *)b;
    const int left_rank = kind_sort_rank(left->kind);
    const int right_rank = kind_sort_rank(right->kind);

    if (left_rank != right_rank) {
        return left_rank - right_rank;
    }

    return g_ascii_strcasecmp(left->name, right->name);
}

static gboolean
is_directory_no_follow(const char *path)
{
    g_autoptr(GFile) file = g_file_new_for_path(path);
    g_autoptr(GFileInfo) info = NULL;

    info = g_file_query_info(file,
                             G_FILE_ATTRIBUTE_STANDARD_TYPE "," G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK,
                             G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                             NULL,
                             NULL);
    if (info == NULL) {
        return FALSE;
    }

    return g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY &&
           !g_file_info_get_is_symlink(info);
}

static char *
real_path_for_visit(const char *path)
{
    return g_canonicalize_filename(path, NULL);
}

static LmmeFileNode *
node_new(const char *path, const char *name, LmmeFileKind kind)
{
    LmmeFileNode *node = g_new0(LmmeFileNode, 1);
    node->path = g_strdup(path);
    node->name = g_strdup(name);
    node->kind = kind;
    node->children = kind == LMME_FILE_KIND_DIRECTORY ? g_ptr_array_new_with_free_func((GDestroyNotify)lmme_file_node_free) : NULL;
    return node;
}

LmmeWorkspace *
lmme_workspace_new(const char *path)
{
    LmmeWorkspace *workspace = g_new0(LmmeWorkspace, 1);
    workspace->path = g_canonicalize_filename(path, NULL);
    return workspace;
}

void
lmme_workspace_free(LmmeWorkspace *workspace)
{
    if (workspace == NULL) {
        return;
    }

    g_clear_pointer(&workspace->path, g_free);
    g_clear_pointer(&workspace->root, lmme_file_node_free);
    g_free(workspace);
}

void
lmme_file_node_free(LmmeFileNode *node)
{
    if (node == NULL) {
        return;
    }

    g_free(node->path);
    g_free(node->name);
    g_clear_pointer(&node->children, g_ptr_array_unref);
    g_free(node);
}

gboolean
lmme_workspace_rescan(LmmeWorkspace *workspace,
                      gboolean show_hidden_files,
                      gboolean show_images,
                      GError **error)
{
    g_autoptr(GHashTable) visited = NULL;

    if (workspace == NULL || !g_file_test(workspace->path, G_FILE_TEST_IS_DIR)) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_NOENT, "Could not open workspace.");
        return FALSE;
    }

    visited = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    g_clear_pointer(&workspace->root, lmme_file_node_free);
    workspace->root = scan_dir(workspace->path, TRUE, show_hidden_files, show_images, visited, error);

    return workspace->root != NULL;
}

static LmmeFileNode *
scan_dir(const char *path,
         gboolean is_root,
         gboolean show_hidden_files,
         gboolean show_images,
         GHashTable *visited,
         GError **error)
{
    g_autofree char *base = g_path_get_basename(path);
    g_autofree char *real = real_path_for_visit(path);
    g_autoptr(GDir) dir = NULL;
    LmmeFileNode *node = NULL;
    const char *name = NULL;

    if (g_hash_table_contains(visited, real)) {
        return NULL;
    }
    g_hash_table_add(visited, g_strdup(real));

    dir = g_dir_open(path, 0, error);
    if (dir == NULL) {
        return NULL;
    }

    node = node_new(path, is_root ? base : base, LMME_FILE_KIND_DIRECTORY);

    while ((name = g_dir_read_name(dir)) != NULL) {
        g_autofree char *child_path = NULL;
        gboolean child_is_dir = FALSE;
        LmmeFileKind kind = LMME_FILE_KIND_OTHER;

        if (!show_hidden_files && lmme_path_is_hidden_basename(name)) {
            continue;
        }
        if (lmme_path_should_skip_dir(name)) {
            continue;
        }

        child_path = g_build_filename(path, name, NULL);
        child_is_dir = is_directory_no_follow(child_path);
        kind = lmme_file_kind_from_path(child_path, child_is_dir, show_images);

        if (kind == LMME_FILE_KIND_DIRECTORY) {
            LmmeFileNode *child = scan_dir(child_path, FALSE, show_hidden_files, show_images, visited, NULL);
            if (child != NULL) {
                g_ptr_array_add(node->children, child);
            }
        } else if (kind == LMME_FILE_KIND_MARKDOWN || kind == LMME_FILE_KIND_IMAGE) {
            g_ptr_array_add(node->children, node_new(child_path, name, kind));
        }
    }

    g_ptr_array_sort(node->children, node_compare);
    return node;
}

static gboolean
ensure_workspace_target(const LmmeWorkspace *workspace, const char *path, GError **error)
{
    if (workspace == NULL || path == NULL || !lmme_path_is_inside(workspace->path, path)) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_PERM, "Target is outside the workspace.");
        return FALSE;
    }
    return TRUE;
}

static char *
clean_markdown_filename(const char *name)
{
    g_autofree char *copy = g_strdup(name);
    char *trimmed = g_strstrip(copy);

    if (lmme_path_has_markdown_extension(trimmed)) {
        return g_strdup(trimmed);
    }

    return g_strdup_printf("%s.md", trimmed);
}

gboolean
lmme_workspace_create_markdown_file(const LmmeWorkspace *workspace,
                                    const char *base_dir,
                                    const char *name,
                                    char **out_path,
                                    GError **error)
{
    g_autofree char *filename = NULL;
    g_autofree char *path = NULL;

    if (!lmme_validate_basename(name, error)) {
        return FALSE;
    }

    filename = clean_markdown_filename(name);
    path = g_build_filename(base_dir, filename, NULL);

    if (!ensure_workspace_target(workspace, path, error)) {
        return FALSE;
    }
    if (g_file_test(path, G_FILE_TEST_EXISTS)) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_EXIST, "A file with this name already exists.");
        return FALSE;
    }
    if (!g_file_set_contents(path, "", 0, error)) {
        return FALSE;
    }

    if (out_path != NULL) {
        *out_path = g_strdup(path);
    }

    return TRUE;
}

gboolean
lmme_workspace_create_folder(const LmmeWorkspace *workspace,
                             const char *base_dir,
                             const char *name,
                             GError **error)
{
    g_autofree char *copy = g_strdup(name);
    const char *trimmed = g_strstrip(copy);
    g_autofree char *path = NULL;

    if (!lmme_validate_basename(trimmed, error)) {
        return FALSE;
    }

    path = g_build_filename(base_dir, trimmed, NULL);
    if (!ensure_workspace_target(workspace, path, error)) {
        return FALSE;
    }
    if (g_file_test(path, G_FILE_TEST_EXISTS)) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_EXIST, "A folder with this name already exists.");
        return FALSE;
    }
    if (g_mkdir(path, 0700) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not create folder.");
        return FALSE;
    }

    return TRUE;
}

gboolean
lmme_workspace_rename_path(const LmmeWorkspace *workspace,
                           const char *path,
                           const char *new_basename,
                           char **out_path,
                           GError **error)
{
    g_autofree char *copy = g_strdup(new_basename);
    const char *trimmed = g_strstrip(copy);
    g_autofree char *dir = NULL;
    g_autofree char *target = NULL;

    if (!ensure_workspace_target(workspace, path, error) || !lmme_validate_basename(trimmed, error)) {
        return FALSE;
    }

    dir = g_path_get_dirname(path);
    target = g_build_filename(dir, trimmed, NULL);

    if (!ensure_workspace_target(workspace, target, error)) {
        return FALSE;
    }
    if (g_file_test(target, G_FILE_TEST_EXISTS)) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_EXIST, "A file with this name already exists.");
        return FALSE;
    }
    if (g_rename(path, target) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not rename item.");
        return FALSE;
    }

    if (out_path != NULL) {
        *out_path = g_strdup(target);
    }

    return TRUE;
}

static gboolean
delete_path_recursive(const char *path, GError **error)
{
    if (is_directory_no_follow(path)) {
        g_autoptr(GDir) dir = g_dir_open(path, 0, error);
        const char *name = NULL;

        if (dir == NULL) {
            return FALSE;
        }

        while ((name = g_dir_read_name(dir)) != NULL) {
            g_autofree char *child = g_build_filename(path, name, NULL);
            if (!delete_path_recursive(child, error)) {
                return FALSE;
            }
        }

        if (g_rmdir(path) != 0) {
            g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not delete folder.");
            return FALSE;
        }
    } else if (g_unlink(path) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not delete file.");
        return FALSE;
    }

    return TRUE;
}

gboolean
lmme_workspace_delete_path(const LmmeWorkspace *workspace, const char *path, GError **error)
{
    if (!ensure_workspace_target(workspace, path, error) || g_strcmp0(workspace->path, path) == 0) {
        if (error != NULL && *error == NULL) {
            g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_PERM, "Cannot delete the workspace root.");
        }
        return FALSE;
    }

    return delete_path_recursive(path, error);
}

char *
lmme_workspace_target_directory(const LmmeWorkspace *workspace,
                                const char *selected_path,
                                gboolean selected_is_dir)
{
    if (workspace == NULL) {
        return NULL;
    }
    if (selected_path == NULL) {
        return g_strdup(workspace->path);
    }
    if (selected_is_dir) {
        return g_strdup(selected_path);
    }
    return g_path_get_dirname(selected_path);
}
