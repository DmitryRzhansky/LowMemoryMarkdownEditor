#ifndef LMME_DOCUMENT_DOCUMENT_H
#define LMME_DOCUMENT_DOCUMENT_H

#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>

#include "infra/file_fingerprint.h"
#include "editor/preview.h"

typedef struct _LmmeApp LmmeApp;
typedef struct _LmmeDocument LmmeDocument;

typedef enum {
    LMME_SAVE_STATE_SAVED = 0,
    LMME_SAVE_STATE_MODIFIED,
    LMME_SAVE_STATE_AUTOSAVED,
    LMME_SAVE_STATE_ERROR
} LmmeSaveState;

typedef enum {
    LMME_DISK_STATE_NORMAL = 0,
    LMME_DISK_STATE_EXTERNAL_CHANGED,
    LMME_DISK_STATE_EXTERNAL_DELETED
} LmmeDiskState;

typedef enum {
    LMME_DOCUMENT_SAVE_NOT_COMMITTED,
    LMME_DOCUMENT_SAVE_COMMITTED_DURABLE,
    LMME_DOCUMENT_SAVE_COMMITTED_NOT_DURABLE
} LmmeDocumentSaveResult;

typedef enum {
    LMME_PENDING_CLOSE_NONE = 0,
    LMME_PENDING_CLOSE_KEEP_RECOVERY,
    LMME_PENDING_CLOSE_DISCARD_LOCAL
} LmmePendingCloseDisposition;

struct _LmmeDocument {
    LmmeApp *app;
    guint64 id;
    char *path;
    char *relative_path;
    char *recovery_source_path;
    gboolean restored_from_recovery;
    gboolean recovery_failed;
    guint64 content_revision;
    LmmePendingCloseDisposition pending_close;

    GtkWidget *source_view;
    GtkWidget *scroller;
    GtkSourceBuffer *buffer;
    GtkWidget *tab_box;
    GtkWidget *title_label;

    gboolean modified;
    LmmeSaveState save_state;
    LmmeDiskState disk_state;
    guint autosave_id;
    guint recovery_id;
    guint stats_timeout_id;
    guint word_count;
    gboolean word_count_dirty;
    gulong changed_handler_id;
    gboolean preview_dirty;
    gboolean preview_active_line_valid;
    guint preview_active_line;
    guint preview_full_parse_count;
    GFileMonitor *monitor;
    LmmeFileFingerprint last_known_fingerprint;
    LmmeFileFingerprint base_fingerprint;
    LmmeFileFingerprint expected_internal_fingerprint;
    gboolean has_expected_internal_fingerprint;
    GCancellable *clipboard_cancellable;
};

/*
 * Returns an owned document. app is borrowed for the document lifetime; path,
 * contents, and relative_title are borrowed only for this call.
 */
LmmeDocument *lmme_document_new(LmmeApp *app,
                                const char *path,
                                const char *contents,
                                const char *relative_title);
void lmme_document_free(LmmeDocument *doc);

void lmme_document_set_save_state(LmmeDocument *doc, LmmeSaveState state);
const char *lmme_document_save_state_label(const LmmeDocument *doc);
void lmme_document_refresh_title(LmmeDocument *doc);
void lmme_document_mark_recovered(LmmeDocument *doc,
                                  const char *recovery_source_path,
                                  gboolean original_changed);
gboolean lmme_document_flush_recovery(LmmeDocument *doc, GError **error);
/* snapshot is borrowed; success is durable only when revision is still current. */
gboolean lmme_document_write_recovery_snapshot(LmmeDocument *doc,
                                               const char *snapshot,
                                               gsize length,
                                               guint64 revision,
                                               GError **error);
/* Save errors are optional; COMMITTED_NOT_DURABLE still means the target changed. */
LmmeDocumentSaveResult lmme_document_save(LmmeDocument *doc, GError **error);
LmmeDocumentSaveResult lmme_document_overwrite(LmmeDocument *doc, GError **error);
LmmeDocumentSaveResult lmme_document_save_as(LmmeDocument *doc,
                                             const char *new_path,
                                             GError **error);
guint lmme_document_cached_word_count(const LmmeDocument *doc);
LmmePreviewApplyResult lmme_document_set_preview_visible(LmmeDocument *doc, gboolean visible);
void lmme_document_update_preview_active_line(LmmeDocument *doc);

#endif
