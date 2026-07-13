#include "editor/preview_style.h"

#include <string.h>

typedef struct {
    guint byte_offset;
    guint *destination;
} LmmePreviewEndpoint;

static gint
endpoint_compare(gconstpointer left, gconstpointer right)
{
    const LmmePreviewEndpoint *a = left;
    const LmmePreviewEndpoint *b = right;
    return a->byte_offset < b->byte_offset ? -1 : a->byte_offset > b->byte_offset;
}

static gint
range_compare(gconstpointer left, gconstpointer right)
{
    const LmmePreviewRange *a = *(LmmePreviewRange * const *)left;
    const LmmePreviewRange *b = *(LmmePreviewRange * const *)right;

    if (a->start_offset != b->start_offset) {
        return a->start_offset < b->start_offset ? -1 : 1;
    }
    if (a->end_offset != b->end_offset) {
        return a->end_offset < b->end_offset ? -1 : 1;
    }
    return (gint)a->kind - (gint)b->kind;
}

static void
convert_byte_ranges_to_char_ranges(const char *text, GPtrArray *ranges)
{
    g_autoptr(GArray) endpoints = g_array_sized_new(FALSE,
                                                    FALSE,
                                                    sizeof(LmmePreviewEndpoint),
                                                    ranges->len * 2);
    const char *cursor = text;
    guint byte_offset = 0;
    guint char_offset = 0;

    for (guint i = 0; i < ranges->len; i++) {
        LmmePreviewRange *range = g_ptr_array_index(ranges, i);
        LmmePreviewEndpoint start = {range->start_offset, &range->start_offset};
        LmmePreviewEndpoint end = {range->end_offset, &range->end_offset};
        g_array_append_val(endpoints, start);
        g_array_append_val(endpoints, end);
    }
    g_array_sort(endpoints, endpoint_compare);
    for (guint i = 0; i < endpoints->len; i++) {
        LmmePreviewEndpoint *endpoint = &g_array_index(endpoints, LmmePreviewEndpoint, i);
        while (byte_offset < endpoint->byte_offset && *cursor != '\0') {
            const char *next = g_utf8_next_char(cursor);
            byte_offset += (guint)(next - cursor);
            char_offset++;
            cursor = next;
        }
        *endpoint->destination = char_offset;
    }
    g_ptr_array_sort(ranges, range_compare);
}

static void
add_range_bytes(GPtrArray *ranges,
                const char *text,
                LmmePreviewRangeKind kind,
                gsize start_byte,
                gsize end_byte)
{
    LmmePreviewRange *range = NULL;

    (void)text;

    if (end_byte <= start_byte) {
        return;
    }

    range = g_new0(LmmePreviewRange, 1);
    range->kind = kind;
    if (start_byte > G_MAXUINT || end_byte > G_MAXUINT) {
        g_free(range);
        return;
    }
    range->start_offset = (guint)start_byte;
    range->end_offset = (guint)end_byte;

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
scan_inline_ranges(GPtrArray *ranges,
                   const char *text,
                   const char *start,
                   const char *end,
                   gboolean is_active_line,
                   gboolean hide_markdown_markers);

typedef struct {
    guint column_count;
    gboolean has_leading_pipe;
    gboolean has_trailing_pipe;
    gboolean has_nonempty_cell;
} LmmePreviewTableRowShape;

static const char *
skip_inline_code_span(const char *cursor, const char *line_end)
{
    const char *p = cursor;
    guint run_len = 0;
    const char *scan = NULL;

    if (p >= line_end || *p != '`') {
        return cursor;
    }

    while (p < line_end && *p == '`') {
        run_len++;
        p++;
    }

    scan = p;
    while (scan < line_end) {
        if (*scan == '`') {
            const char *close = scan;
            guint close_len = 0;

            while (close < line_end && *close == '`') {
                close_len++;
                close++;
            }

            if (close_len == run_len) {
                return close;
            }

            scan = close;
            continue;
        }

        scan++;
    }

    return cursor;
}

static gboolean
table_pipe_is_escaped(const char *line_start, const char *pipe)
{
    guint backslashes = 0;
    const char *p = pipe;

    while (p > line_start && p[-1] == '\\') {
        backslashes++;
        p--;
    }

    return (backslashes % 2U) == 1U;
}

static void
trim_cell_bounds(const char *cell_start, const char *cell_end, const char **out_start, const char **out_end)
{
    const char *start = skip_spaces(cell_start, cell_end);
    const char *end = cell_end;

    while (end > start && (end[-1] == ' ' || end[-1] == '\t')) {
        end--;
    }

    *out_start = start;
    *out_end = end;
}

static gboolean
table_cell_is_nonempty(const char *cell_start, const char *cell_end)
{
    const char *trimmed_start = NULL;
    const char *trimmed_end = NULL;

    trim_cell_bounds(cell_start, cell_end, &trimmed_start, &trimmed_end);
    return trimmed_start < trimmed_end;
}

static gboolean
separator_cell_is_valid(const char *cell_start, const char *cell_end)
{
    const char *p = NULL;
    const char *end = NULL;
    guint dashes = 0;

    trim_cell_bounds(cell_start, cell_end, &p, &end);
    if (p >= end) {
        return FALSE;
    }

    if (*p == ':') {
        p++;
    }

    while (p < end && *p == '-') {
        dashes++;
        p++;
    }

    if (p < end && *p == ':') {
        p++;
    }

    return dashes >= 3 && p == end;
}

static gboolean
table_row_get_cell_bounds(const char *line_start,
                          const char *line_end,
                          const GArray *pipe_offsets,
                          const LmmePreviewTableRowShape *shape,
                          guint cell_index,
                          const char **out_start,
                          const char **out_end)
{
    guint pipe_count = pipe_offsets->len;

    if (cell_index >= shape->column_count) {
        return FALSE;
    }

    if (!shape->has_leading_pipe && cell_index == 0) {
        *out_start = line_start;
        *out_end = pipe_count > 0 ? line_start + g_array_index(pipe_offsets, guint, 0) : line_end;
        return TRUE;
    }

    {
        guint pipe_index = shape->has_leading_pipe ? cell_index : cell_index - 1U;

        *out_start = line_start + g_array_index(pipe_offsets, guint, pipe_index) + 1;
        if (cell_index + 1U < shape->column_count) {
            guint next_pipe_index = shape->has_leading_pipe ? cell_index + 1U : cell_index;
            *out_end = line_start + g_array_index(pipe_offsets, guint, next_pipe_index);
        } else if (!shape->has_trailing_pipe) {
            *out_end = line_end;
        } else {
            *out_end = line_start + g_array_index(pipe_offsets, guint, pipe_count - 1U);
        }
    }

    return TRUE;
}

static gboolean
scan_table_row_shape(const char *line_start,
                     const char *line_end,
                     GArray *pipe_offsets,
                     LmmePreviewTableRowShape *out_shape)
{
    const char *p = line_start;
    const char *trimmed_start = skip_spaces(line_start, line_end);
    const char *trimmed_end = line_end;
    guint pipe_count = 0;

    g_array_set_size(pipe_offsets, 0);
    memset(out_shape, 0, sizeof(*out_shape));

    while (trimmed_end > trimmed_start &&
           (trimmed_end[-1] == ' ' || trimmed_end[-1] == '\t')) {
        trimmed_end--;
    }

    while (p < line_end) {
        if (*p == '`') {
            const char *after = skip_inline_code_span(p, line_end);
            if (after > p) {
                p = after;
                continue;
            }
            p++;
            continue;
        }

        if (*p == '|' && !table_pipe_is_escaped(line_start, p)) {
            guint offset = (guint)(p - line_start);
            g_array_append_val(pipe_offsets, offset);
            pipe_count++;
        }

        p++;
    }

    if (pipe_count == 0) {
        return FALSE;
    }

    out_shape->has_leading_pipe = trimmed_start < line_end && *trimmed_start == '|';
    out_shape->has_trailing_pipe = trimmed_end > line_start && trimmed_end[-1] == '|';

    if (out_shape->has_leading_pipe && out_shape->has_trailing_pipe) {
        out_shape->column_count = pipe_count - 1U;
    } else if (out_shape->has_leading_pipe || out_shape->has_trailing_pipe) {
        out_shape->column_count = pipe_count;
    } else {
        out_shape->column_count = pipe_count + 1U;
    }

    if (out_shape->column_count == 0) {
        return FALSE;
    }

    for (guint i = 0; i < out_shape->column_count; i++) {
        const char *cell_start = NULL;
        const char *cell_end = NULL;

        if (!table_row_get_cell_bounds(line_start,
                                       line_end,
                                       pipe_offsets,
                                       out_shape,
                                       i,
                                       &cell_start,
                                       &cell_end)) {
            return FALSE;
        }

        if (table_cell_is_nonempty(cell_start, cell_end)) {
            out_shape->has_nonempty_cell = TRUE;
            break;
        }
    }

    return TRUE;
}

static gboolean
table_separator_row_is_valid(const char *line_start,
                             const char *line_end,
                             const GArray *pipe_offsets,
                             const LmmePreviewTableRowShape *shape,
                             guint expected_columns)
{
    if (shape->column_count != expected_columns) {
        return FALSE;
    }

    for (guint i = 0; i < expected_columns; i++) {
        const char *cell_start = NULL;
        const char *cell_end = NULL;

        if (!table_row_get_cell_bounds(line_start,
                                       line_end,
                                       pipe_offsets,
                                       shape,
                                       i,
                                       &cell_start,
                                       &cell_end) ||
            !separator_cell_is_valid(cell_start, cell_end)) {
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean
line_starts_block_construct(const char *line_start, const char *line_end)
{
    const char *p = skip_spaces(line_start, line_end);
    const char *marker_end = NULL;

    if (p >= line_end) {
        return FALSE;
    }

    if (*p == '>') {
        return TRUE;
    }

    if (detect_unordered_list_marker(line_start, line_end, &marker_end) ||
        detect_ordered_list_marker(line_start, line_end, &marker_end)) {
        return TRUE;
    }

    if (*p == '#') {
        const char *hash = p;
        guint hashes = 0;

        while (hash < line_end && *hash == '#') {
            hashes++;
            hash++;
        }

        if (hashes >= 1 && hashes <= 6 && hash < line_end && *hash == ' ') {
            return TRUE;
        }
    }

    return FALSE;
}

static void
add_table_pipe_marker_ranges(GPtrArray *ranges,
                             const char *text,
                             const char *line_start,
                             const GArray *pipe_offsets,
                             gboolean is_active_line,
                             gboolean hide_markdown_markers)
{
    LmmePreviewRangeKind marker = marker_kind(is_active_line, hide_markdown_markers);

    for (guint i = 0; i < pipe_offsets->len; i++) {
        guint offset = g_array_index(pipe_offsets, guint, i);
        const char *pipe = line_start + offset;

        add_range_ptr(ranges, text, marker, pipe, pipe + 1);
    }
}

static gboolean
line_has_unclosed_backtick(const char *line_start, const char *line_end)
{
    const char *p = line_start;

    while (p < line_end) {
        if (*p == '`') {
            const char *after = skip_inline_code_span(p, line_end);
            if (after == p) {
                return TRUE;
            }
            p = after;
            continue;
        }

        p++;
    }

    return FALSE;
}

static gboolean
table_body_row_candidate(const char *line_start, const char *line_end, GArray *pipe_offsets)
{
    LmmePreviewTableRowShape shape = {0};

    if (line_start == line_end) {
        return FALSE;
    }

    if (line_starts_with_fence(line_start, line_end, NULL, NULL)) {
        return FALSE;
    }

    if (!scan_table_row_shape(line_start, line_end, pipe_offsets, &shape)) {
        return FALSE;
    }

    return pipe_offsets->len > 0;
}

static gboolean
scan_table(GPtrArray *ranges,
           const char *text,
           const char *header_start,
           guint header_line_number,
           guint active_line,
           gboolean hide_markdown_markers,
           const char **out_next_line,
           guint *out_consumed_lines)
{
    const char *header_end = strchr(header_start, '\n');
    const char *separator_start = NULL;
    const char *separator_end = NULL;
    const char *table_end = NULL;
    const char *cursor = NULL;
    guint consumed_lines = 0;
    LmmePreviewTableRowShape header_shape = {0};
    LmmePreviewTableRowShape separator_shape = {0};
    LmmePreviewTableRowShape body_shape = {0};
    g_autoptr(GArray) pipe_offsets = g_array_new(FALSE, FALSE, sizeof(guint));

    if (out_next_line != NULL) {
        *out_next_line = header_start;
    }
    if (out_consumed_lines != NULL) {
        *out_consumed_lines = 0;
    }

    if (header_end == NULL) {
        header_end = header_start + strlen(header_start);
    }

    if (memchr(header_start, '|', (size_t)(header_end - header_start)) == NULL) {
        return FALSE;
    }

    if (line_starts_block_construct(header_start, header_end)) {
        return FALSE;
    }

    if (line_has_unclosed_backtick(header_start, header_end)) {
        return FALSE;
    }

    if (!scan_table_row_shape(header_start, header_end, pipe_offsets, &header_shape) ||
        header_shape.column_count < 2 ||
        !header_shape.has_nonempty_cell) {
        return FALSE;
    }

    if (header_end[0] != '\n') {
        return FALSE;
    }

    separator_start = header_end + 1;
    separator_end = strchr(separator_start, '\n');
    if (separator_end == NULL) {
        separator_end = separator_start + strlen(separator_start);
    }

    if (separator_start == separator_end) {
        return FALSE;
    }

    if (line_has_unclosed_backtick(separator_start, separator_end)) {
        return FALSE;
    }

    g_array_set_size(pipe_offsets, 0);
    if (!scan_table_row_shape(separator_start, separator_end, pipe_offsets, &separator_shape) ||
        !table_separator_row_is_valid(separator_start,
                                      separator_end,
                                      pipe_offsets,
                                      &separator_shape,
                                      header_shape.column_count)) {
        return FALSE;
    }

    table_end = separator_end;
    consumed_lines = 2;

    add_range_ptr(ranges, text, LMME_PREVIEW_RANGE_TABLE_HEADER_ROW, header_start, header_end);
    add_range_ptr(ranges, text, LMME_PREVIEW_RANGE_TABLE_SEPARATOR_ROW, separator_start, separator_end);

    g_array_set_size(pipe_offsets, 0);
    (void)scan_table_row_shape(header_start, header_end, pipe_offsets, &header_shape);
    add_table_pipe_marker_ranges(ranges,
                                 text,
                                 header_start,
                                 pipe_offsets,
                                 header_line_number == active_line,
                                 hide_markdown_markers);
    scan_inline_ranges(ranges,
                       text,
                       header_start,
                       header_end,
                       header_line_number == active_line,
                       hide_markdown_markers);

    add_range_ptr(ranges,
                  text,
                  marker_kind(header_line_number + 1U == active_line, hide_markdown_markers),
                  separator_start,
                  separator_end);

    cursor = separator_end[0] == '\n' ? separator_end + 1 : separator_end;
    while (*cursor != '\0') {
        const char *body_start = cursor;
        const char *body_end = strchr(body_start, '\n');
        guint body_line_number = header_line_number + consumed_lines;
        gboolean body_active = body_line_number == active_line;

        if (body_end == NULL) {
            body_end = body_start + strlen(body_start);
        }

        if (body_start == body_end) {
            break;
        }

        if (!table_body_row_candidate(body_start, body_end, pipe_offsets)) {
            break;
        }

        g_array_set_size(pipe_offsets, 0);
        (void)scan_table_row_shape(body_start, body_end, pipe_offsets, &body_shape);

        add_range_ptr(ranges, text, LMME_PREVIEW_RANGE_TABLE_BODY_ROW, body_start, body_end);
        add_table_pipe_marker_ranges(ranges,
                                     text,
                                     body_start,
                                     pipe_offsets,
                                     body_active,
                                     hide_markdown_markers);
        scan_inline_ranges(ranges,
                           text,
                           body_start,
                           body_end,
                           body_active,
                           hide_markdown_markers);

        table_end = body_end;
        consumed_lines++;

        if (body_end[0] != '\n') {
            cursor = body_end;
            break;
        }

        cursor = body_end + 1;
    }

    add_range_ptr(ranges, text, LMME_PREVIEW_RANGE_TABLE, header_start, table_end);

    if (out_next_line != NULL) {
        *out_next_line = cursor;
    }
    if (out_consumed_lines != NULL) {
        *out_consumed_lines = consumed_lines;
    }

    return TRUE;
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

        {
            const char *table_next_line = next_line;
            guint table_consumed_lines = 0;

            if (scan_table(ranges,
                           text,
                           line_start,
                           line_number,
                           active_line,
                           hide_markdown_markers,
                           &table_next_line,
                           &table_consumed_lines)) {
                line_number += table_consumed_lines;
                line_start = table_next_line;
                continue;
            }
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

    convert_byte_ranges_to_char_ranges(text, ranges);
    return ranges;
}
