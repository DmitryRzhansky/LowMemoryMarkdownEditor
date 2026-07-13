#ifndef LMME_EDITOR_PREVIEW_STYLE_H
#define LMME_EDITOR_PREVIEW_STYLE_H

#include <glib.h>

typedef enum {
    LMME_PREVIEW_RANGE_HEADING_1,
    LMME_PREVIEW_RANGE_HEADING_2,
    LMME_PREVIEW_RANGE_HEADING_3,
    LMME_PREVIEW_RANGE_HEADING_4,
    LMME_PREVIEW_RANGE_HEADING_5,
    LMME_PREVIEW_RANGE_HEADING_6,
    LMME_PREVIEW_RANGE_BOLD,
    LMME_PREVIEW_RANGE_ITALIC,
    LMME_PREVIEW_RANGE_BOLD_ITALIC,
    LMME_PREVIEW_RANGE_INLINE_CODE,
    LMME_PREVIEW_RANGE_CODE_BLOCK,
    LMME_PREVIEW_RANGE_BLOCKQUOTE,
    LMME_PREVIEW_RANGE_LIST_MARKER,
    LMME_PREVIEW_RANGE_TASK_MARKER,
    LMME_PREVIEW_RANGE_LINK_TEXT,
    LMME_PREVIEW_RANGE_LINK_URL,
    LMME_PREVIEW_RANGE_IMAGE,
    LMME_PREVIEW_RANGE_HR,
    LMME_PREVIEW_RANGE_FRONTMATTER,
    LMME_PREVIEW_RANGE_HIDDEN_MARKER,
    LMME_PREVIEW_RANGE_DIM_MARKER,
    LMME_PREVIEW_RANGE_TABLE,
    LMME_PREVIEW_RANGE_TABLE_HEADER_ROW,
    LMME_PREVIEW_RANGE_TABLE_SEPARATOR_ROW,
    LMME_PREVIEW_RANGE_TABLE_BODY_ROW,
    LMME_PREVIEW_RANGE_COUNT
} LmmePreviewRangeKind;

typedef struct {
    LmmePreviewRangeKind kind;
    guint start_offset;
    guint end_offset;
} LmmePreviewRange;

/* Returns an owned array whose range elements are released when it is unrefed. */
GPtrArray *lmme_preview_collect_ranges(const char *markdown,
                                       gboolean hide_frontmatter,
                                       guint active_line,
                                       gboolean hide_markdown_markers);

#endif
