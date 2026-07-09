#include "editor/editor_search.h"

gboolean
lmme_editor_find(GtkTextBuffer *buffer, const char *needle, gboolean from_cursor)
{
    GtkTextIter start;
    GtkTextIter match_start;
    GtkTextIter match_end;

    if (needle == NULL || needle[0] == '\0') {
        return FALSE;
    }
    if (from_cursor) {
        gtk_text_buffer_get_iter_at_mark(buffer, &start, gtk_text_buffer_get_insert(buffer));
    } else {
        gtk_text_buffer_get_start_iter(buffer, &start);
    }
    if (!gtk_text_iter_forward_search(&start, needle, GTK_TEXT_SEARCH_TEXT_ONLY, &match_start, &match_end, NULL)) {
        gtk_text_buffer_get_start_iter(buffer, &start);
        if (!gtk_text_iter_forward_search(&start, needle, GTK_TEXT_SEARCH_TEXT_ONLY, &match_start, &match_end, NULL)) {
            return FALSE;
        }
    }
    gtk_text_buffer_select_range(buffer, &match_start, &match_end);
    return TRUE;
}

gboolean
lmme_editor_replace_current(GtkTextBuffer *buffer, const char *needle, const char *replacement)
{
    GtkTextIter start;
    GtkTextIter end;
    g_autofree char *selected = NULL;

    if (needle == NULL || needle[0] == '\0') {
        return FALSE;
    }
    if (!gtk_text_buffer_get_selection_bounds(buffer, &start, &end)) {
        return lmme_editor_find(buffer, needle, TRUE);
    }

    selected = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    if (g_strcmp0(selected, needle) != 0) {
        return lmme_editor_find(buffer, needle, TRUE);
    }

    gtk_text_buffer_begin_user_action(buffer);
    gtk_text_buffer_delete(buffer, &start, &end);
    gtk_text_buffer_insert(buffer, &start, replacement != NULL ? replacement : "", -1);
    gtk_text_buffer_end_user_action(buffer);
    return TRUE;
}
