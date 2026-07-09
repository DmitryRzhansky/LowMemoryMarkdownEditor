#ifndef LMME_EDITOR_PREVIEW_H
#define LMME_EDITOR_PREVIEW_H

#include <gtk/gtk.h>

typedef enum {
    LMME_PREVIEW_APPLY_OK = 0,
    LMME_PREVIEW_APPLY_SKIPPED_LARGE_FILE,
    LMME_PREVIEW_APPLY_FAILED
} LmmePreviewApplyResult;

void lmme_preview_ensure_tags(GtkTextBuffer *buffer);
LmmePreviewApplyResult lmme_preview_apply_editable_preview(GtkTextBuffer *buffer,
                                                           gboolean hide_frontmatter,
                                                           gboolean hide_markdown_markers);
void lmme_preview_clear_editable_preview(GtkTextBuffer *buffer);

#endif
