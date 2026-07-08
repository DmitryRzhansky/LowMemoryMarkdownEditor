#ifndef LMME_PREVIEW_H
#define LMME_PREVIEW_H

#include <gtk/gtk.h>

GtkWidget *lmme_preview_create_view(void);
void lmme_preview_set_markdown(GtkWidget *preview_view, const char *markdown, gboolean hide_frontmatter);

#endif
