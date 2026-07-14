#ifndef LMME_EDITOR_EDITOR_VIEW_H
#define LMME_EDITOR_EDITOR_VIEW_H

#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>

#include "editor/preview_style.h"

GtkWidget *lmme_editor_view_new(GtkSourceBuffer *buffer);

void lmme_editor_view_update_preview_images(GtkWidget *view,
                                            GtkTextBuffer *buffer,
                                            const char *markdown,
                                            const GPtrArray *ranges,
                                            const char *workspace_root);
void lmme_editor_view_clear_preview_images(GtkWidget *view, gboolean discard_cache);
guint lmme_editor_view_preview_image_count(GtkWidget *view);

#endif
