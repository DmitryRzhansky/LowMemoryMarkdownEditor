#ifndef LMME_FEATURES_IMAGE_INSERT_H
#define LMME_FEATURES_IMAGE_INSERT_H

#include <gtk/gtk.h>

typedef struct _LmmeApp LmmeApp;
typedef struct _LmmeDocument LmmeDocument;

void lmme_image_insert_from_dialog(LmmeApp *app);
gboolean lmme_image_insert_from_file(LmmeApp *app, const char *source_path);
gboolean lmme_image_insert_for_document(LmmeDocument *doc, const char *source_path);
char *lmme_image_markdown_link(const char *workspace_path, const char *destination_path);
void lmme_image_insert_setup_for_document(LmmeDocument *doc);

#endif
