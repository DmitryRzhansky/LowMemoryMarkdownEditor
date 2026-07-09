#include "editor/preview_style.h"

#include <string.h>

static guint
byte_offset_to_char_offset(const char *text, gsize byte_offset)
{
    const char *p = text;
    const char *end = text + byte_offset;
    guint chars = 0;

    while (p < end && *p != '\0') {
        p = g_utf8_next_char(p);
        chars++;
    }

    return chars;
}

static void
add_range_bytes(GPtrArray *ranges,
                const char *text,
                LmmePreviewRangeKind kind,
                gsize start_byte,
                gsize end_byte)
{
    LmmePreviewRange *range = NULL;

    if (end_byte <= start_byte) {
        return;
    }

    range = g_new0(LmmePreviewRange, 1);
    range->kind = kind;
    range->start_offset = byte_offset_to_char_offset(text, start_byte);
    range->end_offset = byte_offset_to_char_offset(text, end_byte);

    if (range->end_offset <= range->start_offset) {
        g_free(range);
        return;
    }

    g_ptr_array_add(ranges, range);
}

static void
add_range_ptr(GPtrArray *ranges,
              const char *text,
              LmmePreviewRangeKind kind,
              const char *start,
              const char *end)
{
    add_range_bytes(ranges,
                    text,
                    kind,
                    (gsize)(start - text),
                    (gsize)(end - text));
}

static LmmePreviewRangeKind
marker_kind(gboolean is_active_line, gboolean hide_markdown_markers)
{
    return is_active_line || !hide_markdown_markers ? LMME_PREVIEW_RANGE_DIM_MARKER
                                                    : LMME_PREVIEW_RANGE_HIDDEN_MARKER;
}

static const char *
skip_spaces(const char *p, const char *end)
{
    while (p < end && (*p == ' ' || *p == '\t')) {
        p++;
    }

    return p;
}

static gboolean
line_starts_with_fence(const char *line_start,
                       const char *line_end,
                       char *out_char,
                       guint *out_len)
{
    const char *p = skip_spaces(line_start, line_end);
    char fence = '\0';
    guint count = 0;

    if (p >= line_end || (*p != '`' && *p != '~')) {
        return FALSE;
    }

    fence = *p;
    while (p < line_end && *p == fence) {
        count++;
        p++;
    }

    if (count < 3) {
        return FALSE;
    }

    if (out_char != NULL) {
        *out_char = fence;
    }
    if (out_len != NULL) {
        *out_len = count;
    }

    return TRUE;
}

static gboolean
line_closes_fence(const char *line_start, const char *line_end, char fence_char)
{
    const char *p = skip_spaces(line_start, line_end);
    guint count = 0;

    if (p >= line_end || *p != fence_char) {
        return FALSE;
    }

    while (p < line_end && *p == fence_char) {
        count++;
        p++;
    }

    return count >= 3;
}

static gboolean
line_is_hr(const char *line_start, const char *line_end)
{
    const char *p = skip_spaces(line_start, line_end);
    char marker = '\0';
    guint count = 0;

    if (p >= line_end || (*p != '-' && *p != '*' && *p != '_')) {
        return FALSE;
    }

    marker = *p;
    while (p < line_end) {
        if (*p == marker) {
            count++;
        } else if (*p != ' ' && *p != '\t') {
            return FALSE;
        }
        p++;
    }

    return count >= 3;
}

static gboolean
find_frontmatter_end(const char *markdown, gsize *out_end_byte)
{
    const char *line = NULL;
    const char *closing = NULL;
    char delimiter = '\0';

    if (g_str_has_prefix(markdown, "---\n")) {
        delimiter = '-';
        line = markdown + 4;
    } else if (g_str_has_prefix(markdown, "+++\n")) {
        delimiter = '+';
        line = markdown + 4;
    } else {
        return FALSE;
    }

    while (*line != '\0') {
        const char *next = strchr(line, '\n');
        gsize len = next != NULL ? (gsize)(next - line) : strlen(line);

        if (len == 3 && line[0] == delimiter && line[1] == delimiter && line[2] == delimiter) {
            closing = next != NULL ? next + 1 : line + len;
            *out_end_byte = (gsize)(closing - markdown);
            return TRUE;
        }

        if (next == NULL) {
            break;
        }
        line = next + 1;
    }

    return FALSE;
}

static gboolean
detect_unordered_list_marker(const char *line_start, const char *line_end, const char **marker_end)
{
    const char *p = skip_spaces(line_start, line_end);

    if (p + 1 >= line_end || (*p != '-' && *p != '*' && *p != '+') || p[1] != ' ') {
        return FALSE;
    }

    *marker_end = p + 2;
    return TRUE;
}

static gboolean
detect_ordered_list_marker(const char *line_start, const char *line_end, const char **marker_end)
{
    const char *p = skip_spaces(line_start, line_end);
    const char *digits_start = p;

    while (p < line_end && g_ascii_isdigit((guchar)*p)) {
        p++;
    }

    if (p == digits_start || p + 1 >= line_end || *p != '.' || p[1] != ' ') {
        return FALSE;
    }

    *marker_end = p + 2;
    return TRUE;
}

static const char *
detect_task_marker(const char *content_start, const char *line_end)
{
    if (content_start + 4 <= line_end &&
        content_start[0] == '[' &&
        (content_start[1] == ' ' || content_start[1] == 'x' || content_start[1] == 'X') &&
        content_start[2] == ']' &&
        content_start[3] == ' ') {
        return content_start + 4;
    }

    return NULL;
}

static void
scan_link_or_image(GPtrArray *ranges,
                   const char *text,
                   const char **cursor,
                   const char *line_end,
                   gboolean is_active_line,
                   gboolean hide_markdown_markers)
{
    const char *p = *cursor;
    gboolean is_image = p + 1 < line_end && p[0] == '!' && p[1] == '[';
    const char *label_start = is_image ? p + 2 : p + 1;
    const char *label_end = NULL;
    const char *url_start = NULL;
    const char *url_end = NULL;
    LmmePreviewRangeKind marker = marker_kind(is_active_line, hide_markdown_markers);

    if (p >= line_end || (!is_image && *p != '[')) {
        return;
    }

    label_end = memchr(label_start, ']', (gsize)(line_end - label_start));
    if (label_end == NULL || label_end + 1 >= line_end || label_end[1] != '(') {
        return;
    }

    url_start = label_end + 2;
    url_end = memchr(url_start, ')', (gsize)(line_end - url_start));
    if (url_end == NULL) {
        return;
    }

    if (is_image) {
        add_range_ptr(ranges, text, LMME_PREVIEW_RANGE_IMAGE, p, url_end + 1);
        add_range_ptr(ranges, text, marker, p, label_start);
    } else {
        add_range_ptr(ranges, text, LMME_PREVIEW_RANGE_LINK_TEXT, label_start, label_end);
        add_range_ptr(ranges, text, marker, p, label_start);
    }

    add_range_ptr(ranges, text, LMME_PREVIEW_RANGE_LINK_URL, url_start, url_end);
    add_range_ptr(ranges, text, marker, label_end, url_start);
    add_range_ptr(ranges, text, marker, url_end, url_end + 1);
    *cursor = url_end + 1;
}

static gboolean
scan_delimited_inline(GPtrArray *ranges,
                      const char *text,
                      const char **cursor,
                      const char *line_end,
                      const char *delimiter,
                      gsize delimiter_len,
                      LmmePreviewRangeKind content_kind,
                      gboolean is_active_line,
                      gboolean hide_markdown_markers)
{
    const char *p = *cursor;
    const char *content_start = p + delimiter_len;
    const char *content_end = NULL;
    LmmePreviewRangeKind marker = marker_kind(is_active_line, hide_markdown_markers);

    if ((gsize)(line_end - p) < delimiter_len || strncmp(p, delimiter, delimiter_len) != 0) {
        return FALSE;
    }

    content_end = g_strstr_len(content_start, (gssize)(line_end - content_start), delimiter);
    if (content_end == NULL || content_end == content_start) {
        return FALSE;
    }

    add_range_ptr(ranges, text, content_kind, content_start, content_end);
    add_range_ptr(ranges, text, marker, p, content_start);
    add_range_ptr(ranges, text, marker, content_end, content_end + delimiter_len);
    *cursor = content_end + delimiter_len;
    return TRUE;
}

static void
scan_inline_ranges(GPtrArray *ranges,
                   const char *text,
                   const char *start,
                   const char *end,
                   gboolean is_active_line,
                   gboolean hide_markdown_markers)
{
    const char *p = start;

    while (p < end) {
        const char *before = p;

        if (*p == '`' &&
            scan_delimited_inline(ranges,
                                  text,
                                  &p,
                                  end,
                                  "`",
                                  1,
                                  LMME_PREVIEW_RANGE_INLINE_CODE,
                                  is_active_line,
                                  hide_markdown_markers)) {
            continue;
        }

        if ((p + 1 < end && p[0] == '!' && p[1] == '[') || *p == '[') {
            scan_link_or_image(ranges, text, &p, end, is_active_line, hide_markdown_markers);
            if (p != before) {
                continue;
            }
        }

        if (scan_delimited_inline(ranges,
                                  text,
                                  &p,
                                  end,
                                  "***",
                                  3,
                                  LMME_PREVIEW_RANGE_BOLD_ITALIC,
                                  is_active_line,
                                  hide_markdown_markers) ||
            scan_delimited_inline(ranges,
                                  text,
                                  &p,
                                  end,
                                  "___",
                                  3,
                                  LMME_PREVIEW_RANGE_BOLD_ITALIC,
                                  is_active_line,
                                  hide_markdown_markers) ||
            scan_delimited_inline(ranges,
                                  text,
                                  &p,
                                  end,
                                  "**",
                                  2,
                                  LMME_PREVIEW_RANGE_BOLD,
                                  is_active_line,
                                  hide_markdown_markers) ||
            scan_delimited_inline(ranges,
                                  text,
                                  &p,
                                  end,
                                  "__",
                                  2,
                                  LMME_PREVIEW_RANGE_BOLD,
                                  is_active_line,
                                  hide_markdown_markers) ||
            scan_delimited_inline(ranges,
                                  text,
                                  &p,
                                  end,
                                  "*",
                                  1,
                                  LMME_PREVIEW_RANGE_ITALIC,
                                  is_active_line,
                                  hide_markdown_markers) ||
            scan_delimited_inline(ranges,
                                  text,
                                  &p,
                                  end,
                                  "_",
                                  1,
                                  LMME_PREVIEW_RANGE_ITALIC,
                                  is_active_line,
                                  hide_markdown_markers)) {
            continue;
        }

        p++;
    }
}

static void
scan_line(GPtrArray *ranges,
          const char *text,
          const char *line_start,
          const char *line_end,
          guint line_number,
          guint active_line,
          gboolean hide_markdown_markers)
{
    const char *p = skip_spaces(line_start, line_end);
    const char *inline_start = p;
    gboolean is_active_line = line_number == active_line;
    LmmePreviewRangeKind marker = marker_kind(is_active_line, hide_markdown_markers);

    if (line_start == line_end) {
        return;
    }

    if (line_is_hr(line_start, line_end)) {
        add_range_ptr(ranges, text, LMME_PREVIEW_RANGE_HR, line_start, line_end);
        return;
    }

    if (p < line_end && *p == '#') {
        const char *hash_start = p;
        guint hashes = 0;

        while (p < line_end && *p == '#') {
            hashes++;
            p++;
        }

        if (hashes >= 1 && hashes <= 6 && p < line_end && *p == ' ') {
            LmmePreviewRangeKind heading = (LmmePreviewRangeKind)((guint)LMME_PREVIEW_RANGE_HEADING_1 + hashes - 1U);
            add_range_ptr(ranges, text, heading, line_start, line_end);
            add_range_ptr(ranges, text, marker, hash_start, p + 1);
            scan_inline_ranges(ranges, text, p + 1, line_end, is_active_line, hide_markdown_markers);
            return;
        }
    }

    if (p < line_end && *p == '>') {
        const char *marker_end = p + 1;
        if (marker_end < line_end && *marker_end == ' ') {
            marker_end++;
        }
        add_range_ptr(ranges, text, LMME_PREVIEW_RANGE_BLOCKQUOTE, line_start, line_end);
        add_range_ptr(ranges, text, marker, p, marker_end);
        scan_inline_ranges(ranges, text, marker_end, line_end, is_active_line, hide_markdown_markers);
        return;
    }

    if (detect_unordered_list_marker(line_start, line_end, &inline_start) ||
        detect_ordered_list_marker(line_start, line_end, &inline_start)) {
        const char *task_end = detect_task_marker(inline_start, line_end);

        add_range_ptr(ranges, text, LMME_PREVIEW_RANGE_LIST_MARKER, p, inline_start);
        if (task_end != NULL) {
            add_range_ptr(ranges, text, LMME_PREVIEW_RANGE_TASK_MARKER, inline_start, task_end);
            inline_start = task_end;
        }
        scan_inline_ranges(ranges, text, inline_start, line_end, is_active_line, hide_markdown_markers);
        return;
    }

    scan_inline_ranges(ranges, text, inline_start, line_end, is_active_line, hide_markdown_markers);
}

GPtrArray *
lmme_preview_collect_ranges(const char *markdown,
                            gboolean hide_frontmatter,
                            guint active_line,
                            gboolean hide_markdown_markers)
{
    const char *text = markdown != NULL ? markdown : "";
    const char *line_start = text;
    gsize frontmatter_end = 0;
    guint line_number = 0;
    gboolean in_code_block = FALSE;
    char code_fence_char = '\0';
    const char *code_block_start = NULL;
    GPtrArray *ranges = g_ptr_array_new_with_free_func(g_free);

    if (hide_frontmatter && find_frontmatter_end(text, &frontmatter_end)) {
        add_range_bytes(ranges, text, LMME_PREVIEW_RANGE_FRONTMATTER, 0, frontmatter_end);
    }

    while (*line_start != '\0') {
        const char *newline = strchr(line_start, '\n');
        const char *line_end = newline != NULL ? newline : line_start + strlen(line_start);
        const char *next_line = newline != NULL ? newline + 1 : line_end;
        gsize line_start_byte = (gsize)(line_start - text);
        gsize line_end_byte = (gsize)(line_end - text);

        if (frontmatter_end > 0 &&
            line_start_byte < frontmatter_end &&
            line_end_byte <= frontmatter_end) {
            line_start = next_line;
            line_number++;
            continue;
        }

        if (in_code_block) {
            if (line_closes_fence(line_start, line_end, code_fence_char)) {
                add_range_ptr(ranges, text, LMME_PREVIEW_RANGE_DIM_MARKER, line_start, line_end);
                add_range_ptr(ranges, text, LMME_PREVIEW_RANGE_CODE_BLOCK, code_block_start, line_end);
                in_code_block = FALSE;
                code_block_start = NULL;
            }
            line_start = next_line;
            line_number++;
            continue;
        }

        if (line_starts_with_fence(line_start, line_end, &code_fence_char, NULL)) {
            add_range_ptr(ranges, text, LMME_PREVIEW_RANGE_DIM_MARKER, line_start, line_end);
            in_code_block = TRUE;
            code_block_start = line_start;
            line_start = next_line;
            line_number++;
            continue;
        }

        scan_line(ranges, text, line_start, line_end, line_number, active_line, hide_markdown_markers);
        line_start = next_line;
        line_number++;
    }

    if (in_code_block && code_block_start != NULL) {
        add_range_bytes(ranges,
                        text,
                        LMME_PREVIEW_RANGE_CODE_BLOCK,
                        (gsize)(code_block_start - text),
                        strlen(text));
    }

    return ranges;
}
