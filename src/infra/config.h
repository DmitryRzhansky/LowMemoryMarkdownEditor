#ifndef LMME_INFRA_CONFIG_H
#define LMME_INFRA_CONFIG_H

#include <glib.h>

#define LMME_EDITOR_FONT_SIZE_MIN 9
#define LMME_EDITOR_FONT_SIZE_DEFAULT 14
#define LMME_EDITOR_FONT_SIZE_MAX 32

typedef struct {
    int window_width;
    int window_height;
    gboolean window_maximized;

    char *last_workspace;
    gboolean restore_last_workspace;
    gboolean show_hidden_files;
    gboolean show_images;

    char *font_family;
    int font_size;
    gboolean word_wrap;
    gboolean line_numbers;
    gboolean autosave;
    guint autosave_delay_ms;

    gboolean preview_enabled;
    /* Legacy key. Kept for config compatibility. Editable preview is single-pane. */
    double preview_split_ratio;
    guint preview_update_delay_ms;
    gboolean preview_hide_frontmatter;

    int sidebar_width;
    gboolean show_statusbar;
    gboolean show_toolbar;
    gboolean show_breadcrumbs;
    gboolean focus_mode;

    gboolean confirm_delete;
    gboolean restore_tabs;
    GPtrArray *open_tabs;
} LmmeConfig;

void lmme_config_init_defaults(LmmeConfig *config);
void lmme_config_clear(LmmeConfig *config);
char *lmme_config_default_path(void);
gboolean lmme_config_load(LmmeConfig *config, const char *path, GError **error);
gboolean lmme_config_save(const LmmeConfig *config, const char *path, GError **error);
void lmme_config_set_last_workspace(LmmeConfig *config, const char *path);
void lmme_config_set_open_tabs(LmmeConfig *config, GPtrArray *paths);

#endif
