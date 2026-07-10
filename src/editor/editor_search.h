#ifndef LMME_EDITOR_EDITOR_SEARCH_H
#define LMME_EDITOR_EDITOR_SEARCH_H

#include <gtk/gtk.h>

gboolean lmme_editor_find(GtkTextBuffer *buffer, const char *needle, gboolean from_cursor);
gboolean lmme_editor_find_next(GtkTextBuffer *buffer, const char *needle);
gboolean lmme_editor_find_previous(GtkTextBuffer *buffer, const char *needle);
gboolean lmme_editor_replace_current(GtkTextBuffer *buffer, const char *needle, const char *replacement);
guint lmme_editor_replace_all(GtkTextBuffer *buffer, const char *needle, const char *replacement);

#endif
