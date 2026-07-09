#ifndef LMME_UI_WINDOW_H
#define LMME_UI_WINDOW_H

#include <glib.h>

typedef struct _LmmeApp LmmeApp;

void lmme_window_build(LmmeApp *app);
gboolean lmme_window_open_workspace_path(LmmeApp *app, const char *path);
void lmme_window_refresh_tree(LmmeApp *app);
void lmme_window_update_status(LmmeApp *app);
void lmme_window_set_status_error(LmmeApp *app, const char *message);
void lmme_window_schedule_preview(LmmeApp *app);
void lmme_window_refresh_preview_now(LmmeApp *app);
void lmme_window_toggle_preview(LmmeApp *app);

#endif
