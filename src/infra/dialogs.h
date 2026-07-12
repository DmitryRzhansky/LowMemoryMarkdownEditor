#ifndef LMME_INFRA_DIALOGS_H
#define LMME_INFRA_DIALOGS_H

#include <gtk/gtk.h>

typedef enum {
    LMME_SAVE_FAILURE_CANCEL = 0,
    LMME_SAVE_FAILURE_RETRY,
    LMME_SAVE_FAILURE_KEEP_RECOVERY,
    LMME_SAVE_FAILURE_DISCARD_LOCAL
} LmmeSaveFailureChoice;

typedef enum {
    LMME_RECOVERY_CHOICE_LATER = 0,
    LMME_RECOVERY_CHOICE_RESTORE,
    LMME_RECOVERY_CHOICE_DISCARD
} LmmeRecoveryChoice;

typedef enum {
    LMME_EXTERNAL_CONFLICT_KEEP_LOCAL = 0,
    LMME_EXTERNAL_CONFLICT_RELOAD,
    LMME_EXTERNAL_CONFLICT_SAVE_AS,
    LMME_EXTERNAL_CONFLICT_OVERWRITE
} LmmeExternalConflictChoice;

void lmme_dialog_error(GtkWindow *parent, const char *message, const char *detail);
void lmme_dialog_info(GtkWindow *parent, const char *message, const char *detail);
char *lmme_dialog_prompt_text(GtkWindow *parent,
                              const char *title,
                              const char *label,
                              const char *initial_text);
gboolean lmme_dialog_confirm_delete(GtkWindow *parent, gboolean *dont_show_again);
gboolean lmme_dialog_confirm_delete_open_documents(GtkWindow *parent,
                                                   guint modified_count,
                                                   guint total_count);
char *lmme_dialog_open_folder(GtkWindow *parent);
char *lmme_dialog_open_image(GtkWindow *parent);
char *lmme_dialog_save_markdown(GtkWindow *parent, const char *suggested_path);
gboolean lmme_dialog_confirm_overwrite(GtkWindow *parent, const char *path);
gboolean lmme_dialog_confirm_close_unsaved(GtkWindow *parent, const char *filename);
LmmeSaveFailureChoice lmme_dialog_resolve_save_failure(GtkWindow *parent,
                                                       const char *filename,
                                                       const char *detail,
                                                       gboolean allow_retry,
                                                       gboolean allow_keep_recovery,
                                                       gboolean allow_discard_local);
LmmeRecoveryChoice lmme_dialog_choose_recovery(GtkWindow *parent,
                                               const char *original_path,
                                               gboolean original_changed);
LmmeExternalConflictChoice lmme_dialog_external_conflict(GtkWindow *parent,
                                                         const char *path,
                                                         gboolean file_exists,
                                                         gboolean allow_reload);

#endif
