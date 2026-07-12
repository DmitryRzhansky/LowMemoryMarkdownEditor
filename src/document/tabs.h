#ifndef LMME_DOCUMENT_TABS_H
#define LMME_DOCUMENT_TABS_H

#include <glib.h>

#include "document/document.h"
#include "document/recovery.h"

typedef struct _LmmeApp LmmeApp;

LmmeDocument *lmme_tabs_get_active(LmmeApp *app);
LmmeDocument *lmme_tabs_find_by_path(LmmeApp *app, const char *path);
LmmeDocument *lmme_tabs_find_by_id(LmmeApp *app, guint64 document_id);
gboolean lmme_tabs_open_file(LmmeApp *app, const char *path, GError **error);
gboolean lmme_tabs_open_recovery_entry(LmmeApp *app,
                                       const LmmeRecoveryEntry *entry,
                                       GError **error);
LmmeDocumentSaveResult lmme_tabs_save_document(LmmeDocument *doc, GError **error);
LmmeDocumentSaveResult lmme_tabs_save_active(LmmeApp *app, GError **error);
void lmme_tabs_close_active(LmmeApp *app);
gboolean lmme_tabs_close_document(LmmeApp *app, LmmeDocument *doc);
gboolean lmme_tabs_close_tabs_to_right(LmmeApp *app, LmmeDocument *anchor);
gboolean lmme_tabs_close_tabs_to_left(LmmeApp *app, LmmeDocument *anchor);
gboolean lmme_tabs_close_other_tabs(LmmeApp *app, LmmeDocument *anchor);
gboolean lmme_tabs_close_all(LmmeApp *app);
gboolean lmme_tabs_prepare_close_all(LmmeApp *app);
void lmme_tabs_resume_pending_saves(LmmeApp *app);
GPtrArray *lmme_tabs_find_in_subtree(LmmeApp *app, const char *root);
gboolean lmme_tabs_validate_subtree_remap(LmmeApp *app,
                                          const char *old_root,
                                          const char *new_root,
                                          GError **error);
gboolean lmme_tabs_remap_subtree(LmmeApp *app,
                                 const char *old_root,
                                 const char *new_root,
                                 GError **error);
void lmme_tabs_forget_subtree(LmmeApp *app, const char *root);
void lmme_tabs_close_path(LmmeApp *app, const char *path);
void lmme_tabs_update_path(LmmeApp *app, const char *old_path, const char *new_path);
GPtrArray *lmme_tabs_open_paths(LmmeApp *app);
LmmePreviewApplyResult lmme_tabs_set_preview_visible(LmmeApp *app, gboolean visible);

#endif
