#ifndef LMME_EDITOR_EDITOR_H
#define LMME_EDITOR_EDITOR_H

#include <gtksourceview/gtksource.h>
#include <gtk/gtk.h>

#include "infra/config.h"

GtkWidget *lmme_editor_create_view(GtkSourceBuffer **out_buffer, const LmmeConfig *config);
void lmme_editor_apply_font_css(const LmmeConfig *config);
void lmme_editor_setup_zoom_keys(GtkWidget *view, GActionGroup *action_group);
char *lmme_editor_dup_text(GtkTextBuffer *buffer);
void lmme_editor_get_cursor(GtkTextBuffer *buffer, int *line, int *column);
void lmme_editor_insert_text_at_cursor(GtkTextBuffer *buffer, const char *text);
guint lmme_editor_word_count(GtkTextBuffer *buffer);

#endif
