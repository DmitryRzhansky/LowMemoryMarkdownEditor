#ifndef LMME_UI_STATUSBAR_H
#define LMME_UI_STATUSBAR_H

typedef struct _LmmeApp LmmeApp;

void lmme_statusbar_update(LmmeApp *app);
void lmme_statusbar_set_error(LmmeApp *app, const char *message);

#endif
