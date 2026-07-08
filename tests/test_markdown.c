#include <glib.h>
#include <string.h>

#include "markdown.h"

static void
test_basic_preview(void)
{
    const char *input = "# Title\n\nHello **world**.\n\n![alt](img/a.png)\n";
    g_autofree char *preview = lmme_markdown_to_preview_text(input, TRUE);

    g_assert_nonnull(strstr(preview, "Title"));
    g_assert_nonnull(strstr(preview, "Hello world."));
    g_assert_nonnull(strstr(preview, "[Image: img/a.png]"));
    g_assert_null(strstr(preview, "**"));
}

static void
test_lists_and_quotes(void)
{
    const char *input = "> Quote\n\n- one\n- two\n";
    g_autofree char *preview = lmme_markdown_to_preview_text(input, TRUE);

    g_assert_nonnull(strstr(preview, "Quote"));
    g_assert_nonnull(strstr(preview, "one"));
    g_assert_nonnull(strstr(preview, "two"));
}

static void
test_frontmatter_hide(void)
{
    const char *input = "---\ntitle: Secret\n---\n\n# Visible\n";
    g_autofree char *preview = lmme_markdown_to_preview_text(input, TRUE);

    g_assert_null(strstr(preview, "Secret"));
    g_assert_nonnull(strstr(preview, "Visible"));
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/markdown/basic-preview", test_basic_preview);
    g_test_add_func("/markdown/lists-quotes", test_lists_and_quotes);
    g_test_add_func("/markdown/frontmatter-hide", test_frontmatter_hide);
    return g_test_run();
}
