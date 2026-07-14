#include <glib.h>
#include <gtksourceview/gtksource.h>

#include "editor/preview.h"

static char *
make_markdown(guint lines)
{
    GString *text = g_string_sized_new((gsize)lines * 24U);

    for (guint i = 0; i < lines; i++) {
        g_string_append(text, "# Heading **bold**\n");
    }
    return g_string_free(text, FALSE);
}

static void
run_case(guint lines, guint moves)
{
    g_autofree char *markdown = make_markdown(lines);
    g_autoptr(GtkSourceBuffer) buffer = gtk_source_buffer_new(NULL);
    gint64 started = 0;
    gint64 elapsed = 0;
    guint old_line = 0;

    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(buffer), markdown, -1);
    g_assert_cmpint(lmme_preview_apply_editable_preview(NULL,
                                                       GTK_TEXT_BUFFER(buffer),
                                                       TRUE,
                                                       TRUE,
                                                       NULL),
                    ==,
                    LMME_PREVIEW_APPLY_OK);

    started = g_get_monotonic_time();
    for (guint i = 0; i < moves; i++) {
        guint new_line = (i * 7919U) % lines;
        lmme_preview_update_active_line(GTK_TEXT_BUFFER(buffer), old_line, TRUE, new_line);
        old_line = new_line;
    }
    elapsed = g_get_monotonic_time() - started;
    g_print("%u lines, %u moves: %.3f ms, %.3f us/move\n",
            lines,
            moves,
            (double)elapsed / 1000.0,
            (double)elapsed / (double)moves);
}

int
main(void)
{
    run_case(10000, 10000);
    run_case(50000, 10000);
    return 0;
}
