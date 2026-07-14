#ifndef LMME_UI_STATUSBAR_H
#define LMME_UI_STATUSBAR_H

#include <glib.h>

typedef struct _LmmeApp LmmeApp;
typedef struct _LmmeDocument LmmeDocument;

typedef enum {
    LMME_STATUS_SEVERITY_NORMAL,
    LMME_STATUS_SEVERITY_WARNING,
    LMME_STATUS_SEVERITY_ERROR
} LmmeStatusSeverity;

LmmeStatusSeverity lmme_statusbar_document_severity(const LmmeDocument *doc);
void lmme_statusbar_update(LmmeApp *app);
void lmme_statusbar_set_error(LmmeApp *app, const char *message);
/* Returns an owned status string for the supplied document snapshot. */
char *lmme_statusbar_format_document(const LmmeDocument *doc,
                                     int line,
                                     int column,
                                     guint words,
                                     gboolean words_valid,
                                     gboolean preview_enabled);

#endif
