#include <glib.h>
#include <string.h>

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
    return g_test_run();
}
