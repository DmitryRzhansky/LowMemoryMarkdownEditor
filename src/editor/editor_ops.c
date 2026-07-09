#include "editor/editor_ops.h"

void
lmme_editor_wrap_selection(GtkTextBuffer *buffer,
                           const char *prefix,
                           const char *suffix,
                           int cursor_offset_when_empty)
{
    GtkTextIter start;
    GtkTextIter end;

    if (gtk_text_buffer_get_selection_bounds(buffer, &start, &end)) {
        g_autofree char *selected = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
        g_autofree char *wrapped = g_strdup_printf("%s%s%s", prefix, selected, suffix);
        gtk_text_buffer_begin_user_action(buffer);
        gtk_text_buffer_delete(buffer, &start, &end);
        gtk_text_buffer_insert(buffer, &start, wrapped, -1);
        gtk_text_buffer_end_user_action(buffer);
    } else {
        GtkTextIter insert_iter;
        GtkTextIter cursor_iter;
        g_autofree char *text = g_strdup_printf("%s%s", prefix, suffix);
        gtk_text_buffer_get_iter_at_mark(buffer, &insert_iter, gtk_text_buffer_get_insert(buffer));
        gtk_text_buffer_begin_user_action(buffer);
        gtk_text_buffer_insert(buffer, &insert_iter, text, -1);
        gtk_text_buffer_get_iter_at_mark(buffer, &cursor_iter, gtk_text_buffer_get_insert(buffer));
        gtk_text_iter_backward_chars(&cursor_iter, cursor_offset_when_empty);
        gtk_text_buffer_place_cursor(buffer, &cursor_iter);
        gtk_text_buffer_end_user_action(buffer);
    }
}
