#ifndef LMME_APP_APP_H
#define LMME_APP_APP_H

#include <gtk/gtk.h>

#include "infra/config.h"
#include "infra/util.h"

typedef struct _LmmeDocument LmmeDocument;
typedef struct _LmmeRecoveryStore LmmeRecoveryStore;
typedef struct _LmmeWorkspace LmmeWorkspace;
typedef struct _LmmeAppLifetime LmmeAppLifetime;

typedef struct {
    char *path;
    LmmeFileKind kind;
    gboolean empty_area;
} LmmePathContext;

void lmme_path_context_clear(LmmePathContext *context);
void lmme_path_context_set(LmmePathContext *context,
                           const char *path,
                           LmmeFileKind kind,
                           gboolean empty_area);

static inline gboolean
lmme_path_context_is_directory(const LmmePathContext *context)
{
    return context != NULL && context->kind == LMME_FILE_KIND_DIRECTORY;
}

static inline gboolean
lmme_path_context_is_markdown(const LmmePathContext *context)
{
    return context != NULL && context->kind == LMME_FILE_KIND_MARKDOWN;
}

static inline gboolean
lmme_path_context_is_image(const LmmePathContext *context)
{
    return context != NULL && context->kind == LMME_FILE_KIND_IMAGE;
}

typedef struct _LmmeApp {
    GtkApplication *gtk_app;
    GtkWidget *window;

    LmmeConfig config;
    char *config_path;
    LmmeWorkspace *workspace;
    LmmeRecoveryStore *recovery_store;
    guint64 next_document_id;
    gboolean shutdown_in_progress;
    gboolean scheduling_blocked;

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
    GtkWidget *replace_button;

    LmmePathContext selection;
    LmmePathContext tree_context;

    /* TRUE means editable inline preview mode, not a separate preview widget. */
    gboolean preview_enabled;
    gboolean focus_mode;
    guint preview_timeout_id;
    guint command_refresh_source_id;

    GPtrArray *documents;

    /* Non-owning pointer to the tab that opened the current context menu. */
    LmmeDocument *tab_context_document;

    LmmeAppLifetime *lifetime;
} LmmeApp;

LmmeAppLifetime *lmme_app_lifetime_ref(LmmeApp *app);
void lmme_app_lifetime_unref(LmmeAppLifetime *lifetime);
LmmeApp *lmme_app_lifetime_get_app(LmmeAppLifetime *lifetime);

int lmme_app_run(int argc, char **argv);
gboolean lmme_app_request_shutdown(LmmeApp *app);
void lmme_app_clear_widget_refs(LmmeApp *app);
void lmme_app_free(LmmeApp *app);

#endif
