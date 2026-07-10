#include <gtk/gtk.h>

#include "editor/editor_search.h"

static char *
selected_text(GtkTextBuffer *buffer)
{
    GtkTextIter start;
    GtkTextIter end;

    if (!gtk_text_buffer_get_selection_bounds(buffer, &start, &end)) {
        return NULL;
    }
    return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
}

static void
test_find_next_previous_and_wrap(void)
{
    g_autoptr(GtkTextBuffer) buffer = gtk_text_buffer_new(NULL);
    g_autofree char *selected = NULL;

    gtk_text_buffer_set_text(buffer, "one two one", -1);
    g_assert_true(lmme_editor_find_next(buffer, "one"));
    selected = selected_text(buffer);
    g_assert_cmpstr(selected, ==, "one");
    g_clear_pointer(&selected, g_free);

    g_assert_true(lmme_editor_find_next(buffer, "one"));
    selected = selected_text(buffer);
    g_assert_cmpstr(selected, ==, "one");
    g_clear_pointer(&selected, g_free);

    g_assert_true(lmme_editor_find_next(buffer, "one"));
    g_assert_true(lmme_editor_find_previous(buffer, "one"));
}

static void
test_replace_current_selects_next(void)
{
    g_autoptr(GtkTextBuffer) buffer = gtk_text_buffer_new(NULL);
    g_autofree char *selected = NULL;
    GtkTextIter start;
    GtkTextIter end;
    g_autofree char *text = NULL;

    gtk_text_buffer_set_text(buffer, "one one", -1);
    g_assert_true(lmme_editor_find_next(buffer, "one"));
    g_assert_true(lmme_editor_replace_current(buffer, "one", "two"));
    selected = selected_text(buffer);
    g_assert_cmpstr(selected, ==, "one");
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    g_assert_cmpstr(text, ==, "two one");
}

static void
test_replace_all(void)
{
    g_autoptr(GtkTextBuffer) buffer = gtk_text_buffer_new(NULL);
    GtkTextIter start;
    GtkTextIter end;
    g_autofree char *text = NULL;

    gtk_text_buffer_set_text(buffer, "a b a", -1);
    g_assert_cmpuint(lmme_editor_replace_all(buffer, "a", "x"), ==, 2);
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    g_assert_cmpstr(text, ==, "x b x");
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/search/find-wrap", test_find_next_previous_and_wrap);
    g_test_add_func("/search/replace-current", test_replace_current_selects_next);
    g_test_add_func("/search/replace-all", test_replace_all);
    return g_test_run();
}
