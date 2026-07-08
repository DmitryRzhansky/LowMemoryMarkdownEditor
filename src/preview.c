#include "preview.h"

#include "markdown.h"

GtkWidget *
lmme_preview_create_view(void)
{
    GtkWidget *view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
    gtk_widget_set_hexpand(view, TRUE);
    gtk_widget_set_vexpand(view, TRUE);
    return view;
}

void
lmme_preview_set_markdown(GtkWidget *preview_view, const char *markdown, gboolean hide_frontmatter)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(preview_view));
    g_autofree char *preview = lmme_markdown_to_preview_text(markdown, hide_frontmatter);
    gtk_text_buffer_set_text(buffer, preview, -1);
}
