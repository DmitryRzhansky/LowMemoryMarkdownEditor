#define _GNU_SOURCE

#include "workspace/workspace.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <gio/gio.h>
#include <glib/gstdio.h>

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

static LmmeFileNode *
node_new(const char *path, const char *name, LmmeFileKind kind)
{
    LmmeFileNode *node = g_new0(LmmeFileNode, 1);
    node->path = g_strdup(path);
    node->name = g_strdup(name);
    node->kind = kind;
    node->children = kind == LMME_FILE_KIND_DIRECTORY ? g_ptr_array_new_with_free_func((GDestroyNotify)lmme_file_node_free) : NULL;
    node->loaded = kind != LMME_FILE_KIND_DIRECTORY;
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
    g_autofree char *base = NULL;

    if (workspace == NULL || !g_file_test(workspace->path, G_FILE_TEST_IS_DIR)) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_NOENT, "Could not open workspace.");
        return FALSE;
    }

    g_clear_pointer(&workspace->root, lmme_file_node_free);
    base = g_path_get_basename(workspace->path);
    workspace->root = node_new(workspace->path, base, LMME_FILE_KIND_DIRECTORY);
    return lmme_workspace_load_directory(workspace,
                                         workspace->root,
                                         show_hidden_files,
                                         show_images,
                                         error);
}

gboolean
lmme_workspace_load_directory(LmmeWorkspace *workspace,
                              LmmeFileNode *node,
                              gboolean show_hidden_files,
                              gboolean show_images,
                              GError **error)
{
    g_autoptr(GDir) dir = NULL;
    const char *name = NULL;

    if (workspace == NULL || node == NULL || node->kind != LMME_FILE_KIND_DIRECTORY ||
        !lmme_path_is_inside(workspace->path, node->path)) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Invalid workspace directory node.");
        return FALSE;
    }
    if (node->loaded && !node->dirty) {
        return TRUE;
    }
    dir = g_dir_open(node->path, 0, error);
    if (dir == NULL) {
        return FALSE;
    }
    g_ptr_array_set_size(node->children, 0);

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

        child_path = g_build_filename(node->path, name, NULL);
        child_is_dir = is_directory_no_follow(child_path);
        kind = lmme_file_kind_from_path(child_path, child_is_dir, show_images);

        if (kind == LMME_FILE_KIND_DIRECTORY) {
            g_ptr_array_add(node->children,
                            node_new(child_path, name, LMME_FILE_KIND_DIRECTORY));
        } else if (kind == LMME_FILE_KIND_MARKDOWN || kind == LMME_FILE_KIND_IMAGE) {
            g_ptr_array_add(node->children, node_new(child_path, name, kind));
        }
    }

    g_ptr_array_sort(node->children, node_compare);
    node->loaded = TRUE;
    node->dirty = FALSE;
    return TRUE;
}

static LmmeFileNode *
find_node_recursive(LmmeFileNode *node, const char *canonical_path)
{
    if (node == NULL) {
        return NULL;
    }
    if (g_strcmp0(node->path, canonical_path) == 0) {
        return node;
    }
    if (!node->loaded || node->children == NULL) {
        return NULL;
    }
    for (guint i = 0; i < node->children->len; i++) {
        LmmeFileNode *found = find_node_recursive(g_ptr_array_index(node->children, i), canonical_path);
        if (found != NULL) {
            return found;
        }
    }
    return NULL;
}

LmmeFileNode *
lmme_workspace_find_node(LmmeWorkspace *workspace, const char *path)
{
    g_autofree char *canonical_path = NULL;

    if (workspace == NULL || path == NULL) {
        return NULL;
    }
    canonical_path = g_canonicalize_filename(path, NULL);
    return find_node_recursive(workspace->root, canonical_path);
}

gboolean
lmme_workspace_refresh_directory(LmmeWorkspace *workspace,
                                 const char *directory_path,
                                 gboolean show_hidden_files,
                                 gboolean show_images,
                                 GError **error)
{
    LmmeFileNode *node = lmme_workspace_find_node(workspace, directory_path);

    if (node == NULL) {
        if (workspace != NULL && g_strcmp0(workspace->path, directory_path) == 0) {
            return lmme_workspace_rescan(workspace, show_hidden_files, show_images, error);
        }
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_NOENT, "Workspace directory is not loaded.");
        return FALSE;
    }
    node->dirty = TRUE;
    return lmme_workspace_load_directory(workspace, node, show_hidden_files, show_images, error);
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

gboolean
lmme_workspace_validate_target_parent(const LmmeWorkspace *workspace,
                                      const char *path,
                                      GError **error)
{
    g_autofree char *parent = NULL;
    g_autofree char *workspace_real = NULL;
    g_autofree char *parent_real = NULL;

    if (!ensure_workspace_target(workspace, path, error)) {
        return FALSE;
    }

    parent = g_path_get_dirname(path);
    workspace_real = realpath(workspace->path, NULL);
    parent_real = realpath(parent, NULL);
    if (workspace_real == NULL || parent_real == NULL) {
        g_set_error(error,
                    G_FILE_ERROR,
                    g_file_error_from_errno(errno),
                    "Could not resolve the workspace path safely.");
        return FALSE;
    }
    if (!lmme_path_is_inside(workspace_real, parent_real)) {
        g_set_error_literal(error,
                            G_FILE_ERROR,
                            G_FILE_ERROR_PERM,
                            "Target resolves outside the workspace.");
        return FALSE;
    }
    return TRUE;
}

gboolean
lmme_workspace_validate_save_target(const LmmeWorkspace *workspace,
                                    const char *path,
                                    GError **error)
{
    g_autofree char *canonical_path = NULL;
    g_autofree char *parent = NULL;
    g_autofree char *workspace_real = NULL;
    g_autofree char *parent_real = NULL;
    g_autofree char *target_real = NULL;
    struct stat target_info;
    int saved_errno = 0;

    if (workspace == NULL || path == NULL || path[0] == '\0') {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Invalid save destination.");
        return FALSE;
    }

    canonical_path = g_canonicalize_filename(path, NULL);
    if (!ensure_workspace_target(workspace, canonical_path, error)) {
        return FALSE;
    }

    workspace_real = realpath(workspace->path, NULL);
    if (workspace_real == NULL) {
        saved_errno = errno;
        g_set_error(error,
                    G_FILE_ERROR,
                    (gint)g_file_error_from_errno(saved_errno),
                    "Could not resolve the workspace path safely.");
        return FALSE;
    }

    parent = g_path_get_dirname(canonical_path);
    parent_real = realpath(parent, NULL);
    if (parent_real == NULL) {
        saved_errno = errno;
        g_set_error(error,
                    G_FILE_ERROR,
                    (gint)g_file_error_from_errno(saved_errno),
                    "Could not resolve the save destination parent.");
        return FALSE;
    }
    if (!lmme_path_is_inside(workspace_real, parent_real)) {
        g_set_error_literal(error,
                            G_FILE_ERROR,
                            G_FILE_ERROR_PERM,
                            "Save destination resolves outside the workspace.");
        return FALSE;
    }

    if (lstat(canonical_path, &target_info) != 0) {
        if (errno == ENOENT) {
            return TRUE;
        }
        saved_errno = errno;
        g_set_error(error,
                    G_FILE_ERROR,
                    (gint)g_file_error_from_errno(saved_errno),
                    "Could not inspect the save destination.");
        return FALSE;
    }
    if (S_ISDIR(target_info.st_mode)) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_ISDIR, "Save destination is a directory.");
        return FALSE;
    }

    target_real = realpath(canonical_path, NULL);
    if (target_real == NULL) {
        saved_errno = errno;
        g_set_error(error,
                    G_FILE_ERROR,
                    (gint)g_file_error_from_errno(saved_errno),
                    "Could not resolve the save destination safely.");
        return FALSE;
    }
    if (!lmme_path_is_inside(workspace_real, target_real)) {
        g_set_error_literal(error,
                            G_FILE_ERROR,
                            G_FILE_ERROR_PERM,
                            "Save destination resolves outside the workspace.");
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

    if (!lmme_workspace_validate_target_parent(workspace, path, error)) {
        return FALSE;
    }
    int fd = g_open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) {
        g_set_error(error,
                    G_FILE_ERROR,
                    g_file_error_from_errno(errno),
                    errno == EEXIST ? "A file with this name already exists."
                                     : "Could not create Markdown file.");
        return FALSE;
    }
    if (close(fd) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not close new Markdown file.");
        g_unlink(path);
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
    if (!lmme_workspace_validate_target_parent(workspace, path, error)) {
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

    if (!lmme_workspace_validate_target_parent(workspace, path, error) ||
        !lmme_validate_basename(trimmed, error)) {
        return FALSE;
    }

    dir = g_path_get_dirname(path);
    target = g_build_filename(dir, trimmed, NULL);

    if (!lmme_workspace_validate_target_parent(workspace, target, error)) {
        return FALSE;
    }
    struct stat target_info;
    if (lstat(target, &target_info) == 0) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_EXIST, "A file with this name already exists.");
        return FALSE;
    } else if (errno != ENOENT) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not inspect rename destination.");
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
preflight_delete_path(const char *path, dev_t workspace_device, GError **error)
{
    struct stat info;

    if (lstat(path, &info) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not inspect item before deletion.");
        return FALSE;
    }
    if (S_ISLNK(info.st_mode) || !S_ISDIR(info.st_mode)) {
        return TRUE;
    }
    if (info.st_dev != workspace_device) {
        g_set_error_literal(error,
                            G_FILE_ERROR,
                            G_FILE_ERROR_PERM,
                            "Cannot recursively delete across a filesystem or mount boundary.");
        return FALSE;
    }

    g_autoptr(GDir) dir = g_dir_open(path, 0, error);
    const char *name = NULL;
    if (dir == NULL) {
        return FALSE;
    }
    while ((name = g_dir_read_name(dir)) != NULL) {
        g_autofree char *child = g_build_filename(path, name, NULL);
        if (!preflight_delete_path(child, workspace_device, error)) {
            return FALSE;
        }
    }
    return TRUE;
}

static gboolean
delete_path_recursive(const char *path, GError **error)
{
    struct stat info;

    if (lstat(path, &info) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not inspect item during deletion.");
        return FALSE;
    }
    if (S_ISDIR(info.st_mode) && !S_ISLNK(info.st_mode)) {
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
    struct stat workspace_info;

    if (!lmme_workspace_validate_target_parent(workspace, path, error) ||
        g_strcmp0(workspace->path, path) == 0) {
        if (error != NULL && *error == NULL) {
            g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_PERM, "Cannot delete the workspace root.");
        }
        return FALSE;
    }

    if (stat(workspace->path, &workspace_info) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not inspect workspace before deletion.");
        return FALSE;
    }
    if (!preflight_delete_path(path, workspace_info.st_dev, error)) {
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
