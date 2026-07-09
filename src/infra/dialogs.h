#ifndef LMME_INFRA_DIALOGS_H
#define LMME_INFRA_DIALOGS_H

#include <gtk/gtk.h>

void lmme_dialog_error(GtkWindow *parent, const char *message, const char *detail);
void lmme_dialog_info(GtkWindow *parent, const char *message, const char *detail);
char *lmme_dialog_prompt_text(GtkWindow *parent,
                              const char *title,
                              const char *label,
                              const char *initial_text);
gboolean lmme_dialog_confirm_delete(GtkWindow *parent, gboolean *dont_show_again);
char *lmme_dialog_open_folder(GtkWindow *parent);
char *lmme_dialog_open_image(GtkWindow *parent);
gboolean lmme_dialog_confirm_close_unsaved(GtkWindow *parent, const char *filename);

#endif
