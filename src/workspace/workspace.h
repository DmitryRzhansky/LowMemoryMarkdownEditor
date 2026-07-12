#ifndef LMME_WORKSPACE_WORKSPACE_H
#define LMME_WORKSPACE_WORKSPACE_H

#include <glib.h>

#include "infra/util.h"

typedef struct _LmmeFileNode {
    char *path;
    char *name;
    LmmeFileKind kind;
    GPtrArray *children;
    gboolean loaded;
    gboolean dirty;
} LmmeFileNode;

typedef struct _LmmeWorkspace {
    char *path;
    LmmeFileNode *root;
} LmmeWorkspace;

LmmeWorkspace *lmme_workspace_new(const char *path);
void lmme_workspace_free(LmmeWorkspace *workspace);
gboolean lmme_workspace_rescan(LmmeWorkspace *workspace,
                               gboolean show_hidden_files,
                               gboolean show_images,
                               GError **error);
gboolean lmme_workspace_load_directory(LmmeWorkspace *workspace,
                                       LmmeFileNode *node,
                                       gboolean show_hidden_files,
                                       gboolean show_images,
                                       GError **error);
gboolean lmme_workspace_refresh_directory(LmmeWorkspace *workspace,
                                          const char *directory_path,
                                          gboolean show_hidden_files,
                                          gboolean show_images,
                                          GError **error);
LmmeFileNode *lmme_workspace_find_node(LmmeWorkspace *workspace, const char *path);
gboolean lmme_workspace_validate_target_parent(const LmmeWorkspace *workspace,
                                                const char *path,
                                                GError **error);
gboolean lmme_workspace_validate_save_target(const LmmeWorkspace *workspace,
                                              const char *path,
                                              GError **error);

void lmme_file_node_free(LmmeFileNode *node);

gboolean lmme_workspace_create_markdown_file(const LmmeWorkspace *workspace,
                                             const char *base_dir,
                                             const char *name,
                                             char **out_path,
                                             GError **error);
gboolean lmme_workspace_create_folder(const LmmeWorkspace *workspace,
                                      const char *base_dir,
                                      const char *name,
                                      GError **error);
gboolean lmme_workspace_rename_path(const LmmeWorkspace *workspace,
                                    const char *path,
                                    const char *new_basename,
                                    char **out_path,
                                    GError **error);
gboolean lmme_workspace_delete_path(const LmmeWorkspace *workspace, const char *path, GError **error);
char *lmme_workspace_target_directory(const LmmeWorkspace *workspace,
                                      const char *selected_path,
                                      gboolean selected_is_dir);

#endif
