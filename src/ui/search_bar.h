#ifndef LMME_UI_SEARCH_BAR_H
#define LMME_UI_SEARCH_BAR_H

#include <gtk/gtk.h>

typedef struct _LmmeApp LmmeApp;

GtkWidget *lmme_search_bar_create(LmmeApp *app);
void lmme_search_bar_show(LmmeApp *app, gboolean with_replace);
void lmme_search_bar_hide(LmmeApp *app);
gboolean lmme_search_bar_is_visible(const LmmeApp *app);
gboolean lmme_search_bar_is_replace_mode(const LmmeApp *app);
gboolean lmme_search_bar_handle_key_press(LmmeApp *app, guint keyval);

#endif
