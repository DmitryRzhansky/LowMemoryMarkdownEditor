#ifndef LMME_DOCUMENT_DOCUMENT_H
#define LMME_DOCUMENT_DOCUMENT_H

#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>

#include "editor/preview.h"

typedef struct _LmmeApp LmmeApp;
typedef struct _LmmeDocument LmmeDocument;

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

LmmeDocument *lmme_document_new(LmmeApp *app,
                                const char *path,
                                const char *contents,
                                const char *relative_title);
void lmme_document_free(LmmeDocument *doc);

void lmme_document_set_save_state(LmmeDocument *doc, LmmeSaveState state);
const char *lmme_document_save_state_label(const LmmeDocument *doc);
gboolean lmme_document_save(LmmeDocument *doc, GError **error);
LmmePreviewApplyResult lmme_document_set_preview_visible(LmmeDocument *doc, gboolean visible);

#endif
