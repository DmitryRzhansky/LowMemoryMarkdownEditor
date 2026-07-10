#include "editor/editor_search.h"

static gboolean
select_forward_match(GtkTextBuffer *buffer, const char *needle, GtkTextIter *start)
{
    GtkTextIter match_start;
    GtkTextIter match_end;

    if (!gtk_text_iter_forward_search(start,
                                      needle,
                                      GTK_TEXT_SEARCH_TEXT_ONLY,
                                      &match_start,
                                      &match_end,
                                      NULL)) {
        return FALSE;
    }
    gtk_text_buffer_select_range(buffer, &match_start, &match_end);
    return TRUE;
}

static gboolean
select_backward_match(GtkTextBuffer *buffer, const char *needle, GtkTextIter *start)
{
    GtkTextIter match_start;
    GtkTextIter match_end;

    if (!gtk_text_iter_backward_search(start,
                                       needle,
                                       GTK_TEXT_SEARCH_TEXT_ONLY,
                                       &match_start,
                                       &match_end,
                                       NULL)) {
        return FALSE;
    }
    gtk_text_buffer_select_range(buffer, &match_start, &match_end);
    return TRUE;
}

gboolean
lmme_editor_find_next(GtkTextBuffer *buffer, const char *needle)
{
    GtkTextIter start;
    GtkTextIter selection_start;
    GtkTextIter selection_end;

    if (buffer == NULL || needle == NULL || needle[0] == '\0') {
        return FALSE;
    }
    if (gtk_text_buffer_get_selection_bounds(buffer, &selection_start, &selection_end)) {
        start = selection_end;
    } else {
        gtk_text_buffer_get_iter_at_mark(buffer, &start, gtk_text_buffer_get_insert(buffer));
    }
    if (select_forward_match(buffer, needle, &start)) {
        return TRUE;
    }
    gtk_text_buffer_get_start_iter(buffer, &start);
    return select_forward_match(buffer, needle, &start);
}

gboolean
lmme_editor_find_previous(GtkTextBuffer *buffer, const char *needle)
{
    GtkTextIter start;
    GtkTextIter selection_start;
    GtkTextIter selection_end;

    if (buffer == NULL || needle == NULL || needle[0] == '\0') {
        return FALSE;
    }
    if (gtk_text_buffer_get_selection_bounds(buffer, &selection_start, &selection_end)) {
        start = selection_start;
    } else {
        gtk_text_buffer_get_iter_at_mark(buffer, &start, gtk_text_buffer_get_insert(buffer));
    }
    if (select_backward_match(buffer, needle, &start)) {
        return TRUE;
    }
    gtk_text_buffer_get_end_iter(buffer, &start);
    return select_backward_match(buffer, needle, &start);
}

gboolean
lmme_editor_find(GtkTextBuffer *buffer, const char *needle, gboolean from_cursor)
{
    GtkTextIter start;

    if (from_cursor) {
        return lmme_editor_find_next(buffer, needle);
    }
    if (buffer == NULL || needle == NULL || needle[0] == '\0') {
        return FALSE;
    }
    gtk_text_buffer_get_start_iter(buffer, &start);
    return select_forward_match(buffer, needle, &start);
}

gboolean
lmme_editor_replace_current(GtkTextBuffer *buffer, const char *needle, const char *replacement)
{
    GtkTextIter start;
    GtkTextIter end;
    g_autofree char *selected = NULL;

    if (buffer == NULL || needle == NULL || needle[0] == '\0') {
        return FALSE;
    }
    if (!gtk_text_buffer_get_selection_bounds(buffer, &start, &end)) {
        return lmme_editor_find_next(buffer, needle);
    }

    selected = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    if (g_strcmp0(selected, needle) != 0) {
        return lmme_editor_find_next(buffer, needle);
    }

    gtk_text_buffer_begin_user_action(buffer);
    gtk_text_buffer_delete(buffer, &start, &end);
    gtk_text_buffer_insert(buffer, &start, replacement != NULL ? replacement : "", -1);
    gtk_text_buffer_place_cursor(buffer, &start);
    gtk_text_buffer_end_user_action(buffer);
    (void)lmme_editor_find_next(buffer, needle);
    return TRUE;
}

guint
lmme_editor_replace_all(GtkTextBuffer *buffer, const char *needle, const char *replacement)
{
    GtkTextIter cursor;
    GtkTextIter match_start;
    GtkTextIter match_end;
    guint count = 0;

    if (buffer == NULL || needle == NULL || needle[0] == '\0') {
        return 0;
    }
    gtk_text_buffer_get_start_iter(buffer, &cursor);
    gtk_text_buffer_begin_user_action(buffer);
    while (gtk_text_iter_forward_search(&cursor,
                                        needle,
                                        GTK_TEXT_SEARCH_TEXT_ONLY,
                                        &match_start,
                                        &match_end,
                                        NULL)) {
        gtk_text_buffer_delete(buffer, &match_start, &match_end);
        gtk_text_buffer_insert(buffer, &match_start, replacement != NULL ? replacement : "", -1);
        cursor = match_start;
        count++;
    }
    gtk_text_buffer_end_user_action(buffer);
    return count;
}
