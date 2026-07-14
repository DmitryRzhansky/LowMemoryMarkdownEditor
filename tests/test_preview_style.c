#include <glib.h>
#include <gtksourceview/gtksource.h>
#include <string.h>

#include "editor/preview.h"
#include "editor/preview_style.h"

static guint
char_offset_of(const char *text, const char *needle)
{
    const char *found = strstr(text, needle);

    g_assert_nonnull(found);
    return (guint)g_utf8_strlen(text, found - text);
}

static guint
char_len(const char *text)
{
    return (guint)g_utf8_strlen(text, -1);
}

static gboolean
has_range(GPtrArray *ranges, LmmePreviewRangeKind kind, guint start, guint end)
{
    for (guint i = 0; i < ranges->len; i++) {
        const LmmePreviewRange *range = g_ptr_array_index(ranges, i);
        if (range->kind == kind && range->start_offset == start && range->end_offset == end) {
            return TRUE;
        }
    }

    return FALSE;
}

static gboolean
has_kind(GPtrArray *ranges, LmmePreviewRangeKind kind)
{
    for (guint i = 0; i < ranges->len; i++) {
        const LmmePreviewRange *range = g_ptr_array_index(ranges, i);
        if (range->kind == kind) {
            return TRUE;
        }
    }

    return FALSE;
}

static void
test_heading_marker(void)
{
    const char *input = "# Title\n";
    g_autoptr(GPtrArray) ranges = lmme_preview_collect_ranges(input, TRUE, 99, TRUE);

    g_assert_true(has_range(ranges, LMME_PREVIEW_RANGE_HEADING_1, 0, char_offset_of(input, "\n")));
    g_assert_true(has_range(ranges, LMME_PREVIEW_RANGE_HIDDEN_MARKER, 0, 2));
}

static void
test_bold_and_italic(void)
{
    const char *input = "This is **bold** and *italic* text\n";
    guint bold_start = char_offset_of(input, "bold");
    guint italic_start = char_offset_of(input, "italic");
    g_autoptr(GPtrArray) ranges = lmme_preview_collect_ranges(input, TRUE, 99, TRUE);

    g_assert_true(has_range(ranges, LMME_PREVIEW_RANGE_BOLD, bold_start, bold_start + char_len("bold")));
    g_assert_true(has_range(ranges, LMME_PREVIEW_RANGE_ITALIC, italic_start, italic_start + char_len("italic")));
}

static void
test_inline_code(void)
{
    const char *input = "Use `printf()` here\n";
    guint code_start = char_offset_of(input, "printf()");
    g_autoptr(GPtrArray) ranges = lmme_preview_collect_ranges(input, TRUE, 99, TRUE);

    g_assert_true(has_range(ranges,
                            LMME_PREVIEW_RANGE_INLINE_CODE,
                            code_start,
                            code_start + char_len("printf()")));
}

static void
test_code_block_suppresses_inline(void)
{
    const char *input = "```c\n**not bold**\n```\n";
    g_autoptr(GPtrArray) ranges = lmme_preview_collect_ranges(input, TRUE, 99, TRUE);

    g_assert_true(has_kind(ranges, LMME_PREVIEW_RANGE_CODE_BLOCK));
    g_assert_false(has_kind(ranges, LMME_PREVIEW_RANGE_BOLD));
}

static void
test_frontmatter_hide(void)
{
    const char *input = "---\ntitle: Test\n---\n\n# Heading\n";
    guint frontmatter_end = char_offset_of(input, "\n\n# Heading") + 1;
    g_autoptr(GPtrArray) ranges = lmme_preview_collect_ranges(input, TRUE, 99, TRUE);

    g_assert_true(has_range(ranges, LMME_PREVIEW_RANGE_FRONTMATTER, 0, frontmatter_end));
    g_assert_true(has_kind(ranges, LMME_PREVIEW_RANGE_HEADING_1));
}

static void
test_active_line_reveals_marker(void)
{
    const char *input = "# First\n# Second\n";
    guint second_start = char_offset_of(input, "# Second");
    g_autoptr(GPtrArray) ranges = lmme_preview_collect_ranges(input, TRUE, 1, TRUE);

    g_assert_true(has_range(ranges, LMME_PREVIEW_RANGE_HIDDEN_MARKER, 0, 2));
    g_assert_true(has_range(ranges, LMME_PREVIEW_RANGE_DIM_MARKER, second_start, second_start + 2));
}

static void
test_links_and_images(void)
{
    const char *input = "See [site](https://example.com) and ![Alt](img/a.png)\n";
    guint title_start = char_offset_of(input, "site");
    guint url_start = char_offset_of(input, "https://example.com");
    guint image_start = char_offset_of(input, "![Alt]");
    guint image_end = image_start + char_len("![Alt](img/a.png)");
    g_autoptr(GPtrArray) ranges = lmme_preview_collect_ranges(input, TRUE, 99, TRUE);

    g_assert_true(has_range(ranges, LMME_PREVIEW_RANGE_LINK_TEXT, title_start, title_start + char_len("site")));
    g_assert_true(has_range(ranges,
                            LMME_PREVIEW_RANGE_LINK_URL,
                            url_start,
                            url_start + char_len("https://example.com")));
    g_assert_true(has_range(ranges, LMME_PREVIEW_RANGE_IMAGE, image_start, image_end));
}

static void
test_lists_and_tasks(void)
{
    const char *input = "- [x] done\n1. item\n";
    guint ordered_start = char_offset_of(input, "1. item");
    g_autoptr(GPtrArray) ranges = lmme_preview_collect_ranges(input, TRUE, 99, TRUE);

    g_assert_true(has_range(ranges, LMME_PREVIEW_RANGE_LIST_MARKER, 0, 2));
    g_assert_true(has_range(ranges, LMME_PREVIEW_RANGE_TASK_MARKER, 2, 6));
    g_assert_true(has_range(ranges, LMME_PREVIEW_RANGE_LIST_MARKER, ordered_start, ordered_start + 3));
}

static void
test_cyrillic_offsets(void)
{
    const char *input = "# Привет **мир**\n";
    guint bold_start = char_offset_of(input, "мир");
    g_autoptr(GPtrArray) ranges = lmme_preview_collect_ranges(input, TRUE, 99, TRUE);

    g_assert_true(has_kind(ranges, LMME_PREVIEW_RANGE_HEADING_1));
    g_assert_true(has_range(ranges, LMME_PREVIEW_RANGE_BOLD, bold_start, bold_start + char_len("мир")));
}

static void
test_ranges_are_sorted_and_bounded(void)
{
    const char *input = "# Heading with **bold** and [link](target)\n- [x] task\n";
    g_autoptr(GPtrArray) ranges = lmme_preview_collect_ranges(input, TRUE, 99, TRUE);
    guint previous_start = 0;

    for (guint i = 0; i < ranges->len; i++) {
        const LmmePreviewRange *range = g_ptr_array_index(ranges, i);
        g_assert_cmpuint(range->start_offset, <=, range->end_offset);
        g_assert_cmpuint(range->end_offset, <=, char_len(input));
        g_assert_cmpuint(range->start_offset, >=, previous_start);
        previous_start = range->start_offset;
    }
}

static void
test_range_lower_bound(void)
{
    g_autoptr(GPtrArray) ranges = g_ptr_array_new_with_free_func(g_free);
    const guint offsets[] = {2, 2, 8, 15, 21};

    for (guint i = 0; i < G_N_ELEMENTS(offsets); i++) {
        LmmePreviewRange *range = g_new0(LmmePreviewRange, 1);
        range->start_offset = offsets[i];
        range->end_offset = offsets[i] + 1;
        g_ptr_array_add(ranges, range);
    }

    g_assert_cmpuint(lmme_preview_range_lower_bound(ranges, 0), ==, 0);
    g_assert_cmpuint(lmme_preview_range_lower_bound(ranges, 2), ==, 0);
    g_assert_cmpuint(lmme_preview_range_lower_bound(ranges, 3), ==, 2);
    g_assert_cmpuint(lmme_preview_range_lower_bound(ranges, 15), ==, 3);
    g_assert_cmpuint(lmme_preview_range_lower_bound(ranges, 22), ==, 5);
    g_assert_cmpuint(lmme_preview_range_lower_bound(NULL, 10), ==, 0);
}

static guint
count_ranges_of_kind(GPtrArray *ranges, LmmePreviewRangeKind kind)
{
    guint count = 0;

    for (guint i = 0; i < ranges->len; i++) {
        const LmmePreviewRange *range = g_ptr_array_index(ranges, i);
        if (range->kind == kind) {
            count++;
        }
    }

    return count;
}

static const LmmePreviewRange *
find_first_range_of_kind(GPtrArray *ranges, LmmePreviewRangeKind kind)
{
    for (guint i = 0; i < ranges->len; i++) {
        const LmmePreviewRange *range = g_ptr_array_index(ranges, i);
        if (range->kind == kind) {
            return range;
        }
    }

    return NULL;
}

static guint
count_body_rows(GPtrArray *ranges)
{
    return count_ranges_of_kind(ranges, LMME_PREVIEW_RANGE_TABLE_BODY_ROW);
}

static gboolean
ranges_equal_ordered(GPtrArray *left, GPtrArray *right)
{
    if (left->len != right->len) {
        return FALSE;
    }

    for (guint i = 0; i < left->len; i++) {
        const LmmePreviewRange *a = g_ptr_array_index(left, i);
        const LmmePreviewRange *b = g_ptr_array_index(right, i);

        if (a->kind != b->kind || a->start_offset != b->start_offset || a->end_offset != b->end_offset) {
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean
has_pipe_marker_at(GPtrArray *ranges, guint offset, LmmePreviewRangeKind marker_kind)
{
    for (guint i = 0; i < ranges->len; i++) {
        const LmmePreviewRange *range = g_ptr_array_index(ranges, i);
        if (range->kind == marker_kind &&
            range->start_offset == offset &&
            range->end_offset == offset + 1) {
            return TRUE;
        }
    }

    return FALSE;
}

static void
assert_basic_table_shape(GPtrArray *ranges, guint expected_body_rows)
{
    g_assert_cmpuint(count_ranges_of_kind(ranges, LMME_PREVIEW_RANGE_TABLE), ==, 1);
    g_assert_cmpuint(count_ranges_of_kind(ranges, LMME_PREVIEW_RANGE_TABLE_HEADER_ROW), ==, 1);
    g_assert_cmpuint(count_ranges_of_kind(ranges, LMME_PREVIEW_RANGE_TABLE_SEPARATOR_ROW), ==, 1);
    g_assert_cmpuint(count_body_rows(ranges), ==, expected_body_rows);
}

static void
test_table_leading_trailing_pipes(void)
{
    const char *input = "| Column 1 | Column 2 |\n|----------|----------|\n| Value 1  | Value 2  |\n";
    g_autoptr(GPtrArray) ranges = lmme_preview_collect_ranges(input, TRUE, 99, TRUE);
    const LmmePreviewRange *table = find_first_range_of_kind(ranges, LMME_PREVIEW_RANGE_TABLE);

    assert_basic_table_shape(ranges, 1);
    g_assert_cmpuint(table->start_offset, ==, 0);
    g_assert_cmpuint(table->end_offset, ==, char_len(input) - 1U);
    g_assert_true(has_pipe_marker_at(ranges, char_offset_of(input, "| Column"), LMME_PREVIEW_RANGE_HIDDEN_MARKER));
}

static void
test_table_without_outer_pipes(void)
{
    const char *input = "Column 1 | Column 2\n---------|---------\nValue 1  | Value 2\n";
    g_autoptr(GPtrArray) ranges = lmme_preview_collect_ranges(input, TRUE, 99, TRUE);

    assert_basic_table_shape(ranges, 1);
}

static void
test_table_alignment_markers(void)
{
    const char *input = "| Left | Center | Right |\n|:-----|:------:|------:|\n| A    | B      | C     |\n";
    g_autoptr(GPtrArray) ranges = lmme_preview_collect_ranges(input, TRUE, 99, TRUE);

    assert_basic_table_shape(ranges, 1);
}

static void
test_table_empty_cells(void)
{
    const char *input_a = "| A || C |\n|---|---|---|\n| 1 || 3 |\n";
    const char *input_b = "| A | |\n|---|---|\n";
    g_autoptr(GPtrArray) ranges_a = lmme_preview_collect_ranges(input_a, TRUE, 99, TRUE);
    g_autoptr(GPtrArray) ranges_b = lmme_preview_collect_ranges(input_b, TRUE, 99, TRUE);

    assert_basic_table_shape(ranges_a, 1);
    assert_basic_table_shape(ranges_b, 0);
}

static void
test_table_multiple_body_rows(void)
{
    const char *input = "| A | B |\n|---|---|\n| 1 | 2 |\n| 3 | 4 |\n| 5 | 6 |\n";
    g_autoptr(GPtrArray) ranges = lmme_preview_collect_ranges(input, TRUE, 99, TRUE);

    assert_basic_table_shape(ranges, 3);
}

static void
test_table_at_file_start(void)
{
    const char *input = "| A | B |\n|---|---|\n";
    g_autoptr(GPtrArray) ranges = lmme_preview_collect_ranges(input, TRUE, 99, TRUE);
    const LmmePreviewRange *table = find_first_range_of_kind(ranges, LMME_PREVIEW_RANGE_TABLE);

    g_assert_nonnull(table);
    g_assert_cmpuint(table->start_offset, ==, 0);
}

static void
test_table_at_file_end(void)
{
    const char *input = "intro\n\n| A | B |\n|---|---|\n| 1 | 2 |";
    g_autoptr(GPtrArray) ranges = lmme_preview_collect_ranges(input, TRUE, 99, TRUE);
    const LmmePreviewRange *table = find_first_range_of_kind(ranges, LMME_PREVIEW_RANGE_TABLE);

    g_assert_nonnull(table);
    g_assert_cmpuint(table->end_offset, ==, char_len(input));
}

static void
test_table_last_line_without_newline(void)
{
    const char *input = "| A | B |\n|---|---|\n| 1 | 2 |";
    g_autoptr(GPtrArray) ranges = lmme_preview_collect_ranges(input, TRUE, 99, TRUE);

    assert_basic_table_shape(ranges, 1);
}

static void
test_table_ordinary_pipe_line(void)
{
    const char *input = "This is ordinary text | with another fragment\nNext ordinary line\n";
    g_autoptr(GPtrArray) ranges = lmme_preview_collect_ranges(input, TRUE, 99, TRUE);

    g_assert_false(has_kind(ranges, LMME_PREVIEW_RANGE_TABLE));
}

static void
test_table_separator_too_few_dashes(void)
{
    const char *input = "A | B\n--|---\n1 | 2\n";
    g_autoptr(GPtrArray) ranges = lmme_preview_collect_ranges(input, TRUE, 99, TRUE);

    g_assert_false(has_kind(ranges, LMME_PREVIEW_RANGE_TABLE));
}

static void
test_table_header_separator_column_mismatch(void)
{
    const char *input = "A | B | C\n---|---\n1 | 2 | 3\n";
    g_autoptr(GPtrArray) ranges = lmme_preview_collect_ranges(input, TRUE, 99, TRUE);

    g_assert_false(has_kind(ranges, LMME_PREVIEW_RANGE_TABLE));
}

static void
test_table_body_column_mismatch_allowed(void)
{
    const char *input = "A | B | C\n|---|---|---|\n1 | 2\n3 | 4 | 5 | 6\n";
    g_autoptr(GPtrArray) ranges = lmme_preview_collect_ranges(input, TRUE, 99, TRUE);

    assert_basic_table_shape(ranges, 2);
}

static void
test_table_inside_fenced_code_block(void)
{
    const char *input =
        "```markdown\n| A | B |\n|---|---|\n| 1 | 2 |\n```\n"
        "~~~\n| X | Y |\n|---|---|\n| 9 | 8 |\n~~~\n";
    g_autoptr(GPtrArray) ranges = lmme_preview_collect_ranges(input, TRUE, 99, TRUE);

    g_assert_false(has_kind(ranges, LMME_PREVIEW_RANGE_TABLE));
    g_assert_true(has_kind(ranges, LMME_PREVIEW_RANGE_CODE_BLOCK));
}

static void
test_table_cyrillic_offsets(void)
{
    const char *input = "| Имя | Статус |\n|-----|--------|\n| Модуль | Готов |\n";
    g_autoptr(GPtrArray) ranges = lmme_preview_collect_ranges(input, TRUE, 99, TRUE);

    assert_basic_table_shape(ranges, 1);
    g_assert_true(has_kind(ranges, LMME_PREVIEW_RANGE_TABLE_BODY_ROW));
}

static void
test_table_emoji_offsets(void)
{
    const char *input = "| Item | State |\n|------|-------|\n| Parser 🧩 | Ready ✅ |\n";
    g_autoptr(GPtrArray) ranges = lmme_preview_collect_ranges(input, TRUE, 99, TRUE);

    assert_basic_table_shape(ranges, 1);
    g_assert_true(has_kind(ranges, LMME_PREVIEW_RANGE_TABLE_BODY_ROW));
}

static void
test_table_escaped_pipe(void)
{
    const char *input = "| Name | Description |\n|------|-------------|\n| A | left \\| right |\n";
    guint escaped = char_offset_of(input, "\\|");
    g_autoptr(GPtrArray) ranges = lmme_preview_collect_ranges(input, TRUE, 99, TRUE);

    assert_basic_table_shape(ranges, 1);
    g_assert_false(has_pipe_marker_at(ranges, escaped, LMME_PREVIEW_RANGE_HIDDEN_MARKER));
}

static void
test_table_inline_code_pipe(void)
{
    const char *input = "| Expression | Meaning |\n|------------|---------|\n| `a | b` | OR |\n";
    g_autoptr(GPtrArray) ranges = lmme_preview_collect_ranges(input, TRUE, 99, TRUE);

    assert_basic_table_shape(ranges, 1);
    g_assert_true(has_kind(ranges, LMME_PREVIEW_RANGE_INLINE_CODE));
}

static void
test_table_two_tables(void)
{
    const char *input = "| A | B |\n|---|---|\n| 1 | 2 |\n\n| X | Y |\n|---|---|\n| 9 | 8 |\n";
    g_autoptr(GPtrArray) ranges = lmme_preview_collect_ranges(input, TRUE, 99, TRUE);

    g_assert_cmpuint(count_ranges_of_kind(ranges, LMME_PREVIEW_RANGE_TABLE), ==, 2);
}

static void
test_table_blank_line_rules(void)
{
    const char *invalid = "| A | B |\n\n|---|---|\n";
    const char *valid = "| A | B |\n|---|---|\n| 1 | 2 |\n\nafter\n";
    g_autoptr(GPtrArray) invalid_ranges = lmme_preview_collect_ranges(invalid, TRUE, 99, TRUE);
    g_autoptr(GPtrArray) valid_ranges = lmme_preview_collect_ranges(valid, TRUE, 99, TRUE);
    const LmmePreviewRange *table = find_first_range_of_kind(valid_ranges, LMME_PREVIEW_RANGE_TABLE);

    g_assert_false(has_kind(invalid_ranges, LMME_PREVIEW_RANGE_TABLE));
    g_assert_nonnull(table);
    g_assert_cmpuint(table->end_offset, ==, char_offset_of(valid, "\n\nafter"));
}

static void
test_table_incomplete_pair(void)
{
    const char *header_only = "| A | B |\n";
    const char *separator_only = "|---|---|\n";
    g_autoptr(GPtrArray) header_ranges = lmme_preview_collect_ranges(header_only, TRUE, 99, TRUE);
    g_autoptr(GPtrArray) separator_ranges = lmme_preview_collect_ranges(separator_only, TRUE, 99, TRUE);

    g_assert_false(has_kind(header_ranges, LMME_PREVIEW_RANGE_TABLE));
    g_assert_false(has_kind(separator_ranges, LMME_PREVIEW_RANGE_TABLE));
}

static void
test_table_repeatable_parser_pass(void)
{
    const char *input = "| A | B |\n|---|---|\n| 1 | 2 |\n";
    g_autoptr(GPtrArray) first = lmme_preview_collect_ranges(input, TRUE, G_MAXUINT, TRUE);
    g_autoptr(GPtrArray) second = lmme_preview_collect_ranges(input, TRUE, G_MAXUINT, TRUE);

    g_assert_true(ranges_equal_ordered(first, second));
}

static void
test_table_marker_reveal(void)
{
    const char *input = "| A | B |\n|---|---|\n| 1 | 2 |\n";
    guint header_pipe = char_offset_of(input, "| A");
    guint separator_line = char_offset_of(input, "|---");
    g_autoptr(GPtrArray) inactive = lmme_preview_collect_ranges(input, TRUE, 99, TRUE);
    g_autoptr(GPtrArray) active_header = lmme_preview_collect_ranges(input, TRUE, 0, TRUE);
    g_autoptr(GPtrArray) active_separator = lmme_preview_collect_ranges(input, TRUE, 1, TRUE);

    g_assert_true(has_pipe_marker_at(inactive, header_pipe, LMME_PREVIEW_RANGE_HIDDEN_MARKER));
    g_assert_true(has_pipe_marker_at(active_header, header_pipe, LMME_PREVIEW_RANGE_DIM_MARKER));
    g_assert_true(has_range(inactive,
                            LMME_PREVIEW_RANGE_HIDDEN_MARKER,
                            separator_line,
                            char_offset_of(input, "\n| 1")));
    g_assert_true(has_range(active_separator,
                            LMME_PREVIEW_RANGE_DIM_MARKER,
                            separator_line,
                            char_offset_of(input, "\n| 1")));
}

static void
test_table_unclosed_backtick(void)
{
    const char *input = "| A `broken | B |\n|----------|---|\n";
    g_autoptr(GPtrArray) ranges = lmme_preview_collect_ranges(input, TRUE, 99, TRUE);

    g_assert_false(has_kind(ranges, LMME_PREVIEW_RANGE_TABLE));
}

static void
test_table_user_real_world_alignment(void)
{
    const char *input =
        "| Поэт-футурист | Основное направление | Структура \"Хлебных крошек\" | Постоянный URL (ЧПУ) |\n"
        "| :--- | :--- | :--- | :--- |\n"
        "| Владимир Маяковский | Кубофутуризм | Главная > Энциклопедия > Маяковский | /encyclopedia/mayakovsky/ |\n";
    g_autoptr(GPtrArray) ranges = lmme_preview_collect_ranges(input, TRUE, 99, TRUE);

    assert_basic_table_shape(ranges, 1);
}

static void
test_table_preview_api_lifecycle(void)
{
    const char *input = "| A | B |\n|---|---|\n| 1 | 2 |\n";
    g_autoptr(GtkSourceBuffer) buffer = gtk_source_buffer_new(NULL);
    GtkTextTagTable *table = NULL;
    GtkTextTag *first_tag = NULL;
    GtkTextTag *second_tag = NULL;
    g_autofree char *before = NULL;
    g_autofree char *after = NULL;
    GtkTextIter start;
    GtkTextIter end;

    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(buffer), input, -1);
    before = g_strdup(input);

    g_assert_cmpint(lmme_preview_apply_editable_preview(GTK_TEXT_BUFFER(buffer), TRUE, TRUE),
                    ==,
                    LMME_PREVIEW_APPLY_OK);

    gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(buffer), &start, &end);
    after = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(buffer), &start, &end, TRUE);
    g_assert_cmpstr(after, ==, before);

    table = gtk_text_buffer_get_tag_table(GTK_TEXT_BUFFER(buffer));
    first_tag = gtk_text_tag_table_lookup(table, "lmme-preview-table-header-row");
    g_assert_nonnull(first_tag);

    g_assert_cmpint(lmme_preview_apply_editable_preview(GTK_TEXT_BUFFER(buffer), TRUE, TRUE),
                    ==,
                    LMME_PREVIEW_APPLY_OK);
    second_tag = gtk_text_tag_table_lookup(table, "lmme-preview-table-header-row");
    g_assert_true(first_tag == second_tag);

    lmme_preview_clear_editable_preview(GTK_TEXT_BUFFER(buffer));
    {
        GtkTextTag *body_tag = gtk_text_tag_table_lookup(table, "lmme-preview-table-body-row");
        GtkTextIter body_iter;

        g_assert_nonnull(body_tag);
        gtk_text_buffer_get_iter_at_line(GTK_TEXT_BUFFER(buffer), &body_iter, 2);
        g_assert_false(gtk_text_iter_has_tag(&body_iter, body_tag));
    }

    gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(buffer), &start, &end);
    g_free(after);
    after = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(buffer), &start, &end, TRUE);
    g_assert_cmpstr(after, ==, before);
}

static GtkTextTag *
required_tag(GtkTextTagTable *table, const char *name)
{
    GtkTextTag *tag = gtk_text_tag_table_lookup(table, name);

    g_assert_nonnull(tag);
    return tag;
}

static void
assert_tag_color_is_dark(GtkTextTag *tag, const char *property_name)
{
    g_autoptr(GdkRGBA) rgba = NULL;

    g_object_get(tag, property_name, &rgba, NULL);
    g_assert_nonnull(rgba);
    g_assert_cmpfloat(MAX(rgba->red, MAX(rgba->green, rgba->blue)), <, 0.45);
}

static void
assert_tag_color_differs(GtkTextTag *tag, const char *property_name, const char *old_color)
{
    g_autoptr(GdkRGBA) actual = NULL;
    GdkRGBA old_rgba = {0};

    g_object_get(tag, property_name, &actual, NULL);
    g_assert_nonnull(actual);
    g_assert_true(gdk_rgba_parse(&old_rgba, old_color));
    g_assert_false(gdk_rgba_equal(actual, &old_rgba));
}

static void
test_preview_dark_tag_properties(void)
{
    static const double heading_scales[] = {1.65, 1.45, 1.25, 1.10, 1.00, 1.00};
    g_autoptr(GtkSourceBuffer) buffer = gtk_source_buffer_new(NULL);
    GtkTextTagTable *table = gtk_text_buffer_get_tag_table(GTK_TEXT_BUFFER(buffer));
    GtkTextTag *tag = NULL;
    g_autofree char *family = NULL;
    int initial_tag_count = 0;
    gboolean invisible = FALSE;

    lmme_preview_ensure_tags(GTK_TEXT_BUFFER(buffer));
    initial_tag_count = gtk_text_tag_table_get_size(table);

    tag = required_tag(table, "lmme-preview-table-header-row");
    assert_tag_color_differs(tag, "paragraph-background-rgba", "#c7cdd2");
    assert_tag_color_is_dark(tag, "paragraph-background-rgba");

    tag = required_tag(table, "lmme-preview-table-separator-row");
    assert_tag_color_is_dark(tag, "paragraph-background-rgba");

    tag = required_tag(table, "lmme-preview-table-body-row");
    assert_tag_color_differs(tag, "paragraph-background-rgba", "#d0d5d9");
    assert_tag_color_is_dark(tag, "paragraph-background-rgba");

    tag = required_tag(table, "lmme-preview-hidden-marker");
    g_object_get(tag, "invisible", &invisible, NULL);
    g_assert_true(invisible);

    tag = required_tag(table, "lmme-preview-frontmatter");
    invisible = FALSE;
    g_object_get(tag, "invisible", &invisible, NULL);
    g_assert_true(invisible);

    tag = required_tag(table, "lmme-preview-inline-code");
    g_object_get(tag, "family", &family, NULL);
    g_assert_cmpstr(family, ==, "monospace");
    g_clear_pointer(&family, g_free);
    tag = required_tag(table, "lmme-preview-code-block");
    g_object_get(tag, "family", &family, NULL);
    g_assert_cmpstr(family, ==, "monospace");

    for (guint i = 0; i < G_N_ELEMENTS(heading_scales); i++) {
        g_autofree char *name = g_strdup_printf("lmme-preview-heading-%u", i + 1);
        int weight = 0;
        double scale = 0.0;

        tag = required_tag(table, name);
        g_object_get(tag, "weight", &weight, "scale", &scale, NULL);
        g_assert_cmpint(weight, ==, PANGO_WEIGHT_BOLD);
        g_assert_cmpfloat(scale, ==, heading_scales[i]);
    }

    tag = required_tag(table, "lmme-preview-table-header-row");
    lmme_preview_ensure_tags(GTK_TEXT_BUFFER(buffer));
    g_assert_cmpint(gtk_text_tag_table_get_size(table), ==, initial_tag_count);
    g_assert_true(tag == required_tag(table, "lmme-preview-table-header-row"));
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/preview-style/heading-marker", test_heading_marker);
    g_test_add_func("/preview-style/bold-italic", test_bold_and_italic);
    g_test_add_func("/preview-style/inline-code", test_inline_code);
    g_test_add_func("/preview-style/code-block", test_code_block_suppresses_inline);
    g_test_add_func("/preview-style/frontmatter-hide", test_frontmatter_hide);
    g_test_add_func("/preview-style/active-line", test_active_line_reveals_marker);
    g_test_add_func("/preview-style/links-images", test_links_and_images);
    g_test_add_func("/preview-style/lists-tasks", test_lists_and_tasks);
    g_test_add_func("/preview-style/cyrillic-offsets", test_cyrillic_offsets);
    g_test_add_func("/preview-style/sorted-bounded", test_ranges_are_sorted_and_bounded);
    g_test_add_func("/preview-style/lower-bound", test_range_lower_bound);
    g_test_add_func("/preview-style/table/leading-trailing-pipes", test_table_leading_trailing_pipes);
    g_test_add_func("/preview-style/table/without-outer-pipes", test_table_without_outer_pipes);
    g_test_add_func("/preview-style/table/alignment-markers", test_table_alignment_markers);
    g_test_add_func("/preview-style/table/empty-cells", test_table_empty_cells);
    g_test_add_func("/preview-style/table/multiple-body-rows", test_table_multiple_body_rows);
    g_test_add_func("/preview-style/table/at-file-start", test_table_at_file_start);
    g_test_add_func("/preview-style/table/at-file-end", test_table_at_file_end);
    g_test_add_func("/preview-style/table/last-line-without-newline", test_table_last_line_without_newline);
    g_test_add_func("/preview-style/table/ordinary-pipe-line", test_table_ordinary_pipe_line);
    g_test_add_func("/preview-style/table/separator-too-few-dashes", test_table_separator_too_few_dashes);
    g_test_add_func("/preview-style/table/header-separator-mismatch", test_table_header_separator_column_mismatch);
    g_test_add_func("/preview-style/table/body-column-mismatch", test_table_body_column_mismatch_allowed);
    g_test_add_func("/preview-style/table/fenced-code-block", test_table_inside_fenced_code_block);
    g_test_add_func("/preview-style/table/cyrillic-offsets", test_table_cyrillic_offsets);
    g_test_add_func("/preview-style/table/emoji-offsets", test_table_emoji_offsets);
    g_test_add_func("/preview-style/table/escaped-pipe", test_table_escaped_pipe);
    g_test_add_func("/preview-style/table/inline-code-pipe", test_table_inline_code_pipe);
    g_test_add_func("/preview-style/table/two-tables", test_table_two_tables);
    g_test_add_func("/preview-style/table/blank-line-rules", test_table_blank_line_rules);
    g_test_add_func("/preview-style/table/incomplete-pair", test_table_incomplete_pair);
    g_test_add_func("/preview-style/table/repeatable-parser-pass", test_table_repeatable_parser_pass);
    g_test_add_func("/preview-style/table/marker-reveal", test_table_marker_reveal);
    g_test_add_func("/preview-style/table/unclosed-backtick", test_table_unclosed_backtick);
    g_test_add_func("/preview-style/table/user-real-world", test_table_user_real_world_alignment);
    g_test_add_func("/preview-style/table/preview-api-lifecycle", test_table_preview_api_lifecycle);
    g_test_add_func("/preview-style/tags/dark-properties", test_preview_dark_tag_properties);
    return g_test_run();
}
