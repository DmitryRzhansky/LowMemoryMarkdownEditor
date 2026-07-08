#ifndef LMME_TABS_H
#define LMME_TABS_H

#include <gtksourceview/gtksource.h>
#include <gtk/gtk.h>

#include "app.h"
#include "preview.h"

typedef enum {
    LMME_SAVE_STATE_SAVED = 0,
    LMME_SAVE_STATE_MODIFIED,
    LMME_SAVE_STATE_AUTOSAVED,
    LMME_SAVE_STATE_ERROR
} LmmeSaveState;

struct _LmmeDocument {
    LmmeApp *app;
    char *path;
    char *relative_path;

    GtkWidget *source_view;
    GtkWidget *scroller;
    GtkSourceBuffer *buffer;
    GtkWidget *tab_box;
    GtkWidget *title_label;

    gboolean modified;
    LmmeSaveState save_state;
    guint autosave_id;
    guint recovery_id;
    gulong changed_handler_id;
    GFileMonitor *monitor;
    gint64 last_internal_save_us;
};

LmmeDocument *lmme_tabs_get_active(LmmeApp *app);
LmmeDocument *lmme_tabs_find_by_path(LmmeApp *app, const char *path);
gboolean lmme_tabs_open_file(LmmeApp *app, const char *path, GError **error);
gboolean lmme_tabs_open_recovery_file(LmmeApp *app, const char *path, const char *title, GError **error);
gboolean lmme_tabs_save_document(LmmeDocument *doc, GError **error);
gboolean lmme_tabs_save_active(LmmeApp *app, GError **error);
void lmme_tabs_close_active(LmmeApp *app);
gboolean lmme_tabs_close_document(LmmeApp *app, LmmeDocument *doc);
gboolean lmme_tabs_close_tabs_to_right(LmmeApp *app, LmmeDocument *anchor);
gboolean lmme_tabs_close_tabs_to_left(LmmeApp *app, LmmeDocument *anchor);
gboolean lmme_tabs_close_other_tabs(LmmeApp *app, LmmeDocument *anchor);
gboolean lmme_tabs_close_all(LmmeApp *app);
void lmme_tabs_close_path(LmmeApp *app, const char *path);
void lmme_tabs_update_path(LmmeApp *app, const char *old_path, const char *new_path);
GPtrArray *lmme_tabs_open_paths(LmmeApp *app);
void lmme_document_free(LmmeDocument *doc);
void lmme_document_set_save_state(LmmeDocument *doc, LmmeSaveState state);
const char *lmme_document_save_state_label(const LmmeDocument *doc);
LmmePreviewApplyResult lmme_document_set_preview_visible(LmmeDocument *doc, gboolean visible);
LmmePreviewApplyResult lmme_tabs_set_preview_visible(LmmeApp *app, gboolean visible);

#endif
