#include "editor/preview.h"

#include "editor/preview_style.h"

#include <stdarg.h>
#include <string.h>

#define LMME_PREVIEW_MAX_STYLE_BYTES (2U * 1024U * 1024U)

static const char *preview_tag_names[] = {
    "lmme-preview-heading-1",
    "lmme-preview-heading-2",
    "lmme-preview-heading-3",
    "lmme-preview-heading-4",
    "lmme-preview-heading-5",
    "lmme-preview-heading-6",
    "lmme-preview-bold",
    "lmme-preview-italic",
    "lmme-preview-bold-italic",
    "lmme-preview-inline-code",
    "lmme-preview-code-block",
    "lmme-preview-blockquote",
    "lmme-preview-list-marker",
    "lmme-preview-task-marker",
    "lmme-preview-link-text",
    "lmme-preview-link-url",
    "lmme-preview-image",
    "lmme-preview-hr",
    "lmme-preview-frontmatter",
    "lmme-preview-hidden-marker",
    "lmme-preview-dim-marker",
};

static const char *
tag_name_for_kind(LmmePreviewRangeKind kind)
{
    if ((guint)kind >= G_N_ELEMENTS(preview_tag_names)) {
        return NULL;
    }

    return preview_tag_names[(guint)kind];
}

static char *
dup_buffer_text(GtkTextBuffer *buffer)
{
    GtkTextIter start;
    GtkTextIter end;

    gtk_text_buffer_get_start_iter(buffer, &start);
    gtk_text_buffer_get_end_iter(buffer, &end);
    return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
}

static void
create_tag_if_missing(GtkTextBuffer *buffer, const char *name, const char *first_property_name, ...)
{
    GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
    GtkTextTag *tag = NULL;
    va_list args;

    if (gtk_text_tag_table_lookup(table, name) != NULL) {
        return;
    }

    tag = gtk_text_tag_new(name);
    va_start(args, first_property_name);
    g_object_set_valist(G_OBJECT(tag), first_property_name, args);
    va_end(args);
    gtk_text_tag_table_add(table, tag);
    g_object_unref(tag);
}

void
lmme_preview_ensure_tags(GtkTextBuffer *buffer)
{
    if (buffer == NULL) {
        return;
    }

    create_tag_if_missing(buffer,
                          "lmme-preview-heading-1",
                          "weight",
                          PANGO_WEIGHT_BOLD,
                          "scale",
                          1.65,
                          "pixels-above-lines",
                          10,
                          "pixels-below-lines",
                          5,
                          NULL);
    create_tag_if_missing(buffer,
                          "lmme-preview-heading-2",
                          "weight",
                          PANGO_WEIGHT_BOLD,
                          "scale",
                          1.45,
                          "pixels-above-lines",
                          8,
                          "pixels-below-lines",
                          4,
                          NULL);
    create_tag_if_missing(buffer,
                          "lmme-preview-heading-3",
                          "weight",
                          PANGO_WEIGHT_BOLD,
                          "scale",
                          1.25,
                          "pixels-above-lines",
                          6,
                          "pixels-below-lines",
                          3,
                          NULL);
    create_tag_if_missing(buffer,
                          "lmme-preview-heading-4",
                          "weight",
                          PANGO_WEIGHT_BOLD,
                          "scale",
                          1.10,
                          "pixels-above-lines",
                          4,
                          "pixels-below-lines",
                          2,
                          NULL);
    create_tag_if_missing(buffer,
                          "lmme-preview-heading-5",
                          "weight",
                          PANGO_WEIGHT_BOLD,
                          "scale",
                          1.00,
                          NULL);
    create_tag_if_missing(buffer,
                          "lmme-preview-heading-6",
                          "weight",
                          PANGO_WEIGHT_BOLD,
                          "scale",
                          1.00,
                          NULL);
    create_tag_if_missing(buffer, "lmme-preview-bold", "weight", PANGO_WEIGHT_BOLD, NULL);
    create_tag_if_missing(buffer, "lmme-preview-italic", "style", PANGO_STYLE_ITALIC, NULL);
    create_tag_if_missing(buffer,
                          "lmme-preview-bold-italic",
                          "weight",
                          PANGO_WEIGHT_BOLD,
                          "style",
                          PANGO_STYLE_ITALIC,
                          NULL);
    create_tag_if_missing(buffer,
                          "lmme-preview-inline-code",
                          "family",
                          "monospace",
                          "background",
                          "#2b2f34",
                          "foreground",
                          "#e2e6ea",
                          NULL);
    create_tag_if_missing(buffer,
                          "lmme-preview-code-block",
                          "family",
                          "monospace",
                          "background",
                          "#252a30",
                          "foreground",
                          "#e2e6ea",
                          "left-margin",
                          18,
                          "right-margin",
                          18,
                          NULL);
    create_tag_if_missing(buffer,
                          "lmme-preview-blockquote",
                          "foreground",
                          "#4d5962",
                          "left-margin",
                          18,
                          "style",
                          PANGO_STYLE_ITALIC,
                          NULL);
    create_tag_if_missing(buffer,
                          "lmme-preview-list-marker",
                          "foreground",
                          "#6f7782",
                          "weight",
                          PANGO_WEIGHT_BOLD,
                          NULL);
    create_tag_if_missing(buffer,
                          "lmme-preview-task-marker",
                          "foreground",
                          "#6f7782",
                          "weight",
                          PANGO_WEIGHT_BOLD,
                          NULL);
    create_tag_if_missing(buffer,
                          "lmme-preview-link-text",
                          "foreground",
                          "#315f8a",
                          "underline",
                          PANGO_UNDERLINE_SINGLE,
                          NULL);
    create_tag_if_missing(buffer, "lmme-preview-link-url", "foreground", "#6f7782", NULL);
    create_tag_if_missing(buffer,
                          "lmme-preview-image",
                          "foreground",
                          "#315f8a",
                          "style",
                          PANGO_STYLE_ITALIC,
                          NULL);
    create_tag_if_missing(buffer,
                          "lmme-preview-hr",
                          "foreground",
                          "#6f7782",
                          "weight",
                          PANGO_WEIGHT_BOLD,
                          NULL);
    create_tag_if_missing(buffer,
                          "lmme-preview-frontmatter",
                          "invisible",
                          TRUE,
                          "foreground",
                          "#6f7782",
                          NULL);
    create_tag_if_missing(buffer, "lmme-preview-hidden-marker", "invisible", TRUE, NULL);
    create_tag_if_missing(buffer, "lmme-preview-dim-marker", "foreground", "#6f7782", NULL);
}

void
lmme_preview_clear_editable_preview(GtkTextBuffer *buffer)
{
    GtkTextIter start;
    GtkTextIter end;
    GtkTextTagTable *table = NULL;

    if (buffer == NULL) {
        return;
    }

    gtk_text_buffer_get_start_iter(buffer, &start);
    gtk_text_buffer_get_end_iter(buffer, &end);
    table = gtk_text_buffer_get_tag_table(buffer);

    for (guint i = 0; i < G_N_ELEMENTS(preview_tag_names); i++) {
        GtkTextTag *tag = gtk_text_tag_table_lookup(table, preview_tag_names[i]);
        if (tag != NULL) {
            gtk_text_buffer_remove_tag(buffer, tag, &start, &end);
        }
    }
}

LmmePreviewApplyResult
lmme_preview_apply_editable_preview(GtkTextBuffer *buffer,
                                    gboolean hide_frontmatter,
                                    gboolean hide_markdown_markers)
{
    g_autofree char *text = NULL;
    g_autoptr(GPtrArray) ranges = NULL;
    GtkTextIter cursor;
    guint active_line = 0;
    int total_chars = 0;

    if (buffer == NULL) {
        return LMME_PREVIEW_APPLY_FAILED;
    }

    lmme_preview_ensure_tags(buffer);
    lmme_preview_clear_editable_preview(buffer);

    text = dup_buffer_text(buffer);
    if (strlen(text) > LMME_PREVIEW_MAX_STYLE_BYTES) {
        return LMME_PREVIEW_APPLY_SKIPPED_LARGE_FILE;
    }

    gtk_text_buffer_get_iter_at_mark(buffer, &cursor, gtk_text_buffer_get_insert(buffer));
    active_line = (guint)gtk_text_iter_get_line(&cursor);
    ranges = lmme_preview_collect_ranges(text, hide_frontmatter, active_line, hide_markdown_markers);
    if (ranges == NULL) {
        return LMME_PREVIEW_APPLY_FAILED;
    }

    total_chars = gtk_text_buffer_get_char_count(buffer);
    for (guint i = 0; i < ranges->len; i++) {
        const LmmePreviewRange *range = g_ptr_array_index(ranges, i);
        const char *tag_name = tag_name_for_kind(range->kind);
        GtkTextIter start;
        GtkTextIter end;

        if (tag_name == NULL ||
            range->start_offset >= range->end_offset ||
            range->end_offset > (guint)total_chars ||
            range->end_offset > (guint)G_MAXINT) {
            continue;
        }

        gtk_text_buffer_get_iter_at_offset(buffer, &start, (int)range->start_offset);
        gtk_text_buffer_get_iter_at_offset(buffer, &end, (int)range->end_offset);
        gtk_text_buffer_apply_tag_by_name(buffer, tag_name, &start, &end);
    }

    return LMME_PREVIEW_APPLY_OK;
}
