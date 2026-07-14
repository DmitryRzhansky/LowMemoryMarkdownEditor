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
    "lmme-preview-table",
    "lmme-preview-table-header-row",
    "lmme-preview-table-separator-row",
    "lmme-preview-table-body-row",
};

G_STATIC_ASSERT(G_N_ELEMENTS(preview_tag_names) == LMME_PREVIEW_RANGE_COUNT);

static const char *active_marker_tag_name = "lmme-preview-active-marker";

static const char *marker_cache_key = "lmme-preview-marker-ranges";
static const char *marker_char_count_key = "lmme-preview-marker-char-count";

static const char *
tag_name_for_kind(LmmePreviewRangeKind kind)
{
    if ((guint)kind >= LMME_PREVIEW_RANGE_COUNT) {
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
                          "#30353b",
                          "foreground",
                          "#e7eaee",
                          NULL);
    create_tag_if_missing(buffer,
                          "lmme-preview-code-block",
                          "family",
                          "monospace",
                          "background",
                          "#272c32",
                          "foreground",
                          "#e7eaee",
                          "left-margin",
                          18,
                          "right-margin",
                          18,
                          NULL);
    create_tag_if_missing(buffer,
                          "lmme-preview-blockquote",
                          "foreground",
                          "#aab4be",
                          "left-margin",
                          18,
                          "style",
                          PANGO_STYLE_ITALIC,
                          NULL);
    create_tag_if_missing(buffer,
                          "lmme-preview-list-marker",
                          "foreground",
                          "#9aa6b2",
                          "weight",
                          PANGO_WEIGHT_BOLD,
                          NULL);
    create_tag_if_missing(buffer,
                          "lmme-preview-task-marker",
                          "foreground",
                          "#9aa6b2",
                          "weight",
                          PANGO_WEIGHT_BOLD,
                          NULL);
    create_tag_if_missing(buffer,
                          "lmme-preview-link-text",
                          "foreground",
                          "#78a9d1",
                          "underline",
                          PANGO_UNDERLINE_SINGLE,
                          NULL);
    create_tag_if_missing(buffer, "lmme-preview-link-url", "foreground", "#89939e", NULL);
    create_tag_if_missing(buffer,
                          "lmme-preview-image",
                          "foreground",
                          "#78a9d1",
                          "style",
                          PANGO_STYLE_ITALIC,
                          NULL);
    create_tag_if_missing(buffer,
                          "lmme-preview-hr",
                          "foreground",
                          "#89939e",
                          "weight",
                          PANGO_WEIGHT_BOLD,
                          NULL);
    create_tag_if_missing(buffer,
                          "lmme-preview-frontmatter",
                          "invisible",
                          TRUE,
                          "foreground",
                          "#89939e",
                          NULL);
    create_tag_if_missing(buffer, "lmme-preview-table", "left-margin", 10, "right-margin", 10, NULL);
    create_tag_if_missing(buffer,
                          "lmme-preview-table-header-row",
                          "weight",
                          PANGO_WEIGHT_BOLD,
                          "paragraph-background",
                          "#303942",
                          "pixels-above-lines",
                          4,
                          "pixels-below-lines",
                          2,
                          NULL);
    create_tag_if_missing(buffer,
                          "lmme-preview-table-separator-row",
                          "foreground",
                          "#84919c",
                          "paragraph-background",
                          "#303942",
                          "pixels-above-lines",
                          0,
                          "pixels-below-lines",
                          0,
                          NULL);
    create_tag_if_missing(buffer,
                          "lmme-preview-table-body-row",
                          "paragraph-background",
                          "#292f35",
                          "pixels-above-lines",
                          1,
                          "pixels-below-lines",
                          1,
                          NULL);
    create_tag_if_missing(buffer, "lmme-preview-hidden-marker", "invisible", TRUE, NULL);
    create_tag_if_missing(buffer, "lmme-preview-dim-marker", "foreground", "#747e89", NULL);
    create_tag_if_missing(buffer, active_marker_tag_name, "foreground", "#a7b0ba", NULL);
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
    {
        GtkTextTag *active_marker = gtk_text_tag_table_lookup(table, active_marker_tag_name);
        if (active_marker != NULL) {
            gtk_text_buffer_remove_tag(buffer, active_marker, &start, &end);
        }
    }
    g_object_set_data(G_OBJECT(buffer), marker_cache_key, NULL);
    g_object_set_data(G_OBJECT(buffer), marker_char_count_key, NULL);
}

static void
apply_cached_marker_style_to_line(GtkTextBuffer *buffer, guint line, gboolean active)
{
    GtkTextIter line_start;
    GtkTextIter line_end;
    GPtrArray *ranges = g_object_get_data(G_OBJECT(buffer), marker_cache_key);
    GtkTextTagTable *table = NULL;
    GtkTextTag *hidden = NULL;
    GtkTextTag *active_marker = NULL;
    int line_count = gtk_text_buffer_get_line_count(buffer);
    guint line_start_offset = 0;
    guint line_end_offset = 0;
    int char_count = 0;
    guint cached_char_count = 0;

    if (ranges == NULL || line > (guint)G_MAXINT || line >= (guint)line_count) {
        return;
    }
    char_count = gtk_text_buffer_get_char_count(buffer);
    cached_char_count = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(buffer), marker_char_count_key));
    if (cached_char_count != (guint)char_count) {
        return;
    }
    gtk_text_buffer_get_iter_at_line(buffer, &line_start, (int)line);
    line_end = line_start;
    gtk_text_iter_forward_to_line_end(&line_end);
    line_start_offset = (guint)gtk_text_iter_get_offset(&line_start);
    line_end_offset = (guint)gtk_text_iter_get_offset(&line_end);
    table = gtk_text_buffer_get_tag_table(buffer);
    hidden = gtk_text_tag_table_lookup(table, "lmme-preview-hidden-marker");
    active_marker = gtk_text_tag_table_lookup(table, active_marker_tag_name);
    if (active_marker != NULL) {
        gtk_text_buffer_remove_tag(buffer, active_marker, &line_start, &line_end);
    }

    for (guint i = lmme_preview_range_lower_bound(ranges, line_start_offset);
         i < ranges->len;
         i++) {
        const LmmePreviewRange *range = g_ptr_array_index(ranges, i);
        GtkTextIter start;
        GtkTextIter end;

        if (range->start_offset >= line_end_offset) {
            break;
        }
        if (range->start_offset < line_start_offset ||
            range->end_offset <= range->start_offset ||
            range->end_offset > line_end_offset ||
            range->end_offset > (guint)char_count ||
            range->start_offset >= (guint)char_count ||
            range->end_offset > (guint)G_MAXINT) {
            continue;
        }
        gtk_text_buffer_get_iter_at_offset(buffer, &start, (int)range->start_offset);
        gtk_text_buffer_get_iter_at_offset(buffer, &end, (int)range->end_offset);
        if (active) {
            if (hidden != NULL) {
                gtk_text_buffer_remove_tag(buffer, hidden, &start, &end);
            }
            if (active_marker != NULL) {
                gtk_text_buffer_apply_tag(buffer, active_marker, &start, &end);
            }
        } else if (hidden != NULL) {
            gtk_text_buffer_apply_tag(buffer, hidden, &start, &end);
        }
    }
}

guint
lmme_preview_range_lower_bound(GPtrArray *ranges, guint start_offset)
{
    guint first = 0;
    guint count = ranges != NULL ? ranges->len : 0;

    while (count > 0) {
        guint step = count / 2;
        guint index = first + step;
        const LmmePreviewRange *range = g_ptr_array_index(ranges, index);

        if (range->start_offset < start_offset) {
            first = index + 1;
            count -= step + 1;
        } else {
            count = step;
        }
    }
    return first;
}

void
lmme_preview_update_active_line(GtkTextBuffer *buffer,
                                guint old_line,
                                gboolean old_line_valid,
                                guint new_line)
{
    if (buffer == NULL) {
        return;
    }
    lmme_preview_ensure_tags(buffer);
    if (old_line_valid && old_line != new_line) {
        apply_cached_marker_style_to_line(buffer, old_line, FALSE);
    }
    apply_cached_marker_style_to_line(buffer, new_line, TRUE);
}

gboolean
lmme_preview_marker_cache_is_current(GtkTextBuffer *buffer)
{
    guint cached_char_count = 0;
    int char_count = 0;

    if (buffer == NULL) {
        return FALSE;
    }
    if (g_object_get_data(G_OBJECT(buffer), marker_cache_key) == NULL) {
        return FALSE;
    }
    cached_char_count = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(buffer), marker_char_count_key));
    char_count = gtk_text_buffer_get_char_count(buffer);
    return cached_char_count == (guint)char_count;
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
    GPtrArray *marker_ranges = NULL;

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
    ranges = lmme_preview_collect_ranges(text, hide_frontmatter, G_MAXUINT, hide_markdown_markers);
    if (ranges == NULL) {
        return LMME_PREVIEW_APPLY_FAILED;
    }

    total_chars = gtk_text_buffer_get_char_count(buffer);
    marker_ranges = g_ptr_array_new_with_free_func(g_free);
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
        if (range->kind == LMME_PREVIEW_RANGE_HIDDEN_MARKER) {
            LmmePreviewRange *copy = g_memdup2(range, sizeof(*range));
            g_ptr_array_add(marker_ranges, copy);
        }
    }
    g_object_set_data_full(G_OBJECT(buffer),
                           marker_cache_key,
                           marker_ranges,
                           (GDestroyNotify)g_ptr_array_unref);
    g_object_set_data(G_OBJECT(buffer),
                       marker_char_count_key,
                       GUINT_TO_POINTER((guint)total_chars));

    lmme_preview_update_active_line(buffer, 0, FALSE, active_line);

    return LMME_PREVIEW_APPLY_OK;
}
