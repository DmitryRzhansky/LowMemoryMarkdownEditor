#ifndef LMME_APP_H
#define LMME_APP_H

#include <gtk/gtk.h>

#include "config.h"
#include "workspace.h"

typedef struct _LmmeDocument LmmeDocument;

typedef struct _LmmeApp {
    GtkApplication *gtk_app;
    GtkWidget *window;

    LmmeConfig config;
    char *config_path;
    LmmeWorkspace *workspace;

    GtkWidget *root_box;
    GtkWidget *menu_bar;
    GtkWidget *toolbar;
    GtkWidget *sidebar;
    GtkWidget *tree_view;
    GtkWidget *main_paned;
    GtkWidget *right_box;
    GtkWidget *notebook;
    GtkWidget *status_label;
    GtkWidget *breadcrumbs_label;
    GtkWidget *search_bar;
    GtkWidget *find_entry;
    GtkWidget *replace_entry;

    char *selected_path;
    gboolean selected_is_dir;
    gboolean selected_is_markdown;
    gboolean selected_is_image;

    /* TRUE means editable inline preview mode, not a separate preview widget. */
    gboolean preview_enabled;
    gboolean focus_mode;
    guint preview_timeout_id;

    GPtrArray *documents;

    /* Non-owning pointer to the tab that opened the current context menu. */
    LmmeDocument *tab_context_document;
} LmmeApp;

int lmme_app_run(int argc, char **argv);
void lmme_app_free(LmmeApp *app);

#endif
