#include <glib.h>

#include "editor/preview_style.h"

static char *
make_markdown(gsize target_bytes)
{
    GString *text = g_string_sized_new(target_bytes + 128);
    const char *line = "## Heading **bold** *italic* [link](img/file.png) and `code`\n";

    while (text->len < target_bytes) {
        g_string_append(text, line);
    }
    return g_string_free(text, FALSE);
}

static void
run_case(gsize bytes)
{
    g_autofree char *markdown = make_markdown(bytes);
    gint64 started = g_get_monotonic_time();
    g_autoptr(GPtrArray) ranges = lmme_preview_collect_ranges(markdown, TRUE, G_MAXUINT, TRUE);
    gint64 elapsed = g_get_monotonic_time() - started;

    g_print("%" G_GSIZE_FORMAT " bytes: %.3f ms, %u ranges\n",
            bytes,
            (double)elapsed / 1000.0,
            ranges->len);
}

int
main(void)
{
    run_case(100U * 1024U);
    run_case(1024U * 1024U);
    run_case(2U * 1024U * 1024U);
    return 0;
}
