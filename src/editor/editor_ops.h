#ifndef LMME_EDITOR_EDITOR_OPS_H
#define LMME_EDITOR_EDITOR_OPS_H

#include <gtk/gtk.h>

void lmme_editor_wrap_selection(GtkTextBuffer *buffer,
                                const char *prefix,
                                const char *suffix,
                                int cursor_offset_when_empty);

#endif
