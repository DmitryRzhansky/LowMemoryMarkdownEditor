#ifndef LMME_EDITOR_PREVIEW_H
#define LMME_EDITOR_PREVIEW_H

#include <gtk/gtk.h>

typedef enum {
    LMME_PREVIEW_APPLY_OK = 0,
    LMME_PREVIEW_APPLY_SKIPPED_LARGE_FILE,
    LMME_PREVIEW_APPLY_FAILED
} LmmePreviewApplyResult;

void lmme_preview_ensure_tags(GtkTextBuffer *buffer);
LmmePreviewApplyResult lmme_preview_apply_editable_preview(GtkWidget *view,
                                                           GtkTextBuffer *buffer,
                                                           gboolean hide_frontmatter,
                                                           gboolean hide_markdown_markers,
                                                           const char *workspace_root);
void lmme_preview_clear_editable_preview(GtkWidget *view, GtkTextBuffer *buffer);
guint lmme_preview_range_lower_bound(GPtrArray *ranges, guint start_offset);
void lmme_preview_update_active_line(GtkTextBuffer *buffer,
                                     guint old_line,
                                     gboolean old_line_valid,
                                     guint new_line);
gboolean lmme_preview_marker_cache_is_current(GtkTextBuffer *buffer);

#endif
