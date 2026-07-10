#include "infra/config.h"

#include "infra/safe_write.h"

#include <errno.h>
#include <string.h>

static gboolean
get_boolean(GKeyFile *key, const char *group, const char *name, gboolean fallback)
{
    g_autoptr(GError) error = NULL;
    const gboolean value = g_key_file_get_boolean(key, group, name, &error);
    return error == NULL ? value : fallback;
}

static int
get_integer(GKeyFile *key, const char *group, const char *name, int fallback)
{
    g_autoptr(GError) error = NULL;
    const int value = g_key_file_get_integer(key, group, name, &error);
    return error == NULL ? value : fallback;
}

static guint
get_uint(GKeyFile *key, const char *group, const char *name, guint fallback)
{
    const int value = get_integer(key, group, name, (int)fallback);
    return value > 0 ? (guint)value : fallback;
}

static int
clamp_int(int value, int min, int max)
{
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static guint
clamp_uint(guint value, guint min, guint max)
{
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static char *
get_string(GKeyFile *key, const char *group, const char *name, const char *fallback)
{
    g_autoptr(GError) error = NULL;
    char *value = g_key_file_get_string(key, group, name, &error);
    return error == NULL ? value : g_strdup(fallback);
}

void
lmme_config_init_defaults(LmmeConfig *config)
{
    memset(config, 0, sizeof(*config));

    config->window_width = 1200;
    config->window_height = 800;
    config->window_maximized = FALSE;
    config->last_workspace = g_strdup("");
    config->restore_last_workspace = TRUE;
    config->show_hidden_files = TRUE;
    config->show_images = TRUE;
    config->font_family = g_strdup("monospace");
    config->font_size = LMME_EDITOR_FONT_SIZE_DEFAULT;
    config->word_wrap = TRUE;
    config->line_numbers = TRUE;
    config->autosave = TRUE;
    config->autosave_delay_ms = 700;
    config->preview_enabled = FALSE;
    config->preview_update_delay_ms = 250;
    config->preview_hide_frontmatter = TRUE;
    config->sidebar_width = 260;
    config->show_statusbar = TRUE;
    config->show_toolbar = TRUE;
    config->show_breadcrumbs = TRUE;
    config->confirm_delete = TRUE;
    config->restore_tabs = TRUE;
    config->open_tabs = g_ptr_array_new_with_free_func(g_free);
}

void
lmme_config_clear(LmmeConfig *config)
{
    g_clear_pointer(&config->last_workspace, g_free);
    g_clear_pointer(&config->font_family, g_free);
    g_clear_pointer(&config->open_tabs, g_ptr_array_unref);
}

char *
lmme_config_default_path(void)
{
    return g_build_filename(g_get_user_config_dir(), "lmme", "config.ini", NULL);
}

gboolean
lmme_config_load(LmmeConfig *config, const char *path, GError **error)
{
    g_autoptr(GKeyFile) key = g_key_file_new();
    g_autoptr(GError) local_error = NULL;

    lmme_config_init_defaults(config);

    if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
        return TRUE;
    }

    if (!g_key_file_load_from_file(key, path, G_KEY_FILE_KEEP_COMMENTS, &local_error)) {
        g_propagate_error(error, g_error_copy(local_error));
        return TRUE;
    }

    config->window_width = get_integer(key, "window", "width", config->window_width);
    config->window_height = get_integer(key, "window", "height", config->window_height);
    config->window_maximized = get_boolean(key, "window", "maximized", config->window_maximized);

    g_free(config->last_workspace);
    config->last_workspace = get_string(key, "workspace", "last_path", "");
    config->restore_last_workspace = get_boolean(key, "workspace", "restore_last_workspace", TRUE);
    config->show_hidden_files = get_boolean(key, "workspace", "show_hidden_files", TRUE);
    config->show_images = get_boolean(key, "workspace", "show_images", TRUE);

    g_free(config->font_family);
    config->font_family = get_string(key, "editor", "font_family", "monospace");
    config->font_size = get_integer(key, "editor", "font_size", LMME_EDITOR_FONT_SIZE_DEFAULT);
    config->font_size = clamp_int(config->font_size,
                                  LMME_EDITOR_FONT_SIZE_MIN,
                                  LMME_EDITOR_FONT_SIZE_MAX);
    config->word_wrap = get_boolean(key, "editor", "word_wrap", TRUE);
    config->line_numbers = get_boolean(key, "editor", "line_numbers", TRUE);
    config->autosave = get_boolean(key, "editor", "autosave", TRUE);
    config->autosave_delay_ms = get_uint(key, "editor", "autosave_delay_ms", 700);

    config->preview_enabled = get_boolean(key, "preview", "enabled", FALSE);
    config->preview_update_delay_ms = clamp_uint(get_uint(key, "preview", "update_delay_ms", 250), 150, 500);
    config->preview_hide_frontmatter = get_boolean(key, "preview", "hide_frontmatter", TRUE);

    config->sidebar_width = get_integer(key, "ui", "sidebar_width", 260);
    config->show_statusbar = get_boolean(key, "ui", "show_statusbar", TRUE);
    config->show_toolbar = get_boolean(key, "ui", "show_toolbar", TRUE);
    config->show_breadcrumbs = get_boolean(key, "ui", "show_breadcrumbs", TRUE);

    config->confirm_delete = get_boolean(key, "delete", "confirm_delete", TRUE);
    config->restore_tabs = get_boolean(key, "session", "restore_tabs", TRUE);

    gsize length = 0;
    g_auto(GStrv) tabs = g_key_file_get_string_list(key, "session", "open_tabs", &length, NULL);
    if (tabs != NULL) {
        g_ptr_array_set_size(config->open_tabs, 0);
        for (gsize i = 0; i < length; i++) {
            if (tabs[i] != NULL && tabs[i][0] != '\0') {
                g_ptr_array_add(config->open_tabs, g_strdup(tabs[i]));
            }
        }
    }

    return TRUE;
}

gboolean
lmme_config_save(const LmmeConfig *config, const char *path, GError **error)
{
    g_autoptr(GKeyFile) key = g_key_file_new();
    g_autofree char *dir = g_path_get_dirname(path);
    g_autofree char *data = NULL;
    gsize length = 0;

    if (g_mkdir_with_parents(dir, 0700) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not create config directory.");
        return FALSE;
    }

    g_key_file_set_integer(key, "window", "width", config->window_width);
    g_key_file_set_integer(key, "window", "height", config->window_height);
    g_key_file_set_boolean(key, "window", "maximized", config->window_maximized);

    g_key_file_set_string(key, "workspace", "last_path", config->last_workspace != NULL ? config->last_workspace : "");
    g_key_file_set_boolean(key, "workspace", "restore_last_workspace", config->restore_last_workspace);
    g_key_file_set_boolean(key, "workspace", "show_hidden_files", config->show_hidden_files);
    g_key_file_set_boolean(key, "workspace", "show_images", config->show_images);

    g_key_file_set_string(key, "editor", "font_family", config->font_family != NULL ? config->font_family : "monospace");
    g_key_file_set_integer(key, "editor", "font_size", config->font_size);
    g_key_file_set_boolean(key, "editor", "word_wrap", config->word_wrap);
    g_key_file_set_boolean(key, "editor", "line_numbers", config->line_numbers);
    g_key_file_set_boolean(key, "editor", "autosave", config->autosave);
    g_key_file_set_integer(key, "editor", "autosave_delay_ms", (int)config->autosave_delay_ms);

    g_key_file_set_boolean(key, "preview", "enabled", config->preview_enabled);
    g_key_file_set_integer(key, "preview", "update_delay_ms", (int)config->preview_update_delay_ms);
    g_key_file_set_boolean(key, "preview", "hide_frontmatter", config->preview_hide_frontmatter);

    g_key_file_set_integer(key, "ui", "sidebar_width", config->sidebar_width);
    g_key_file_set_boolean(key, "ui", "show_statusbar", config->show_statusbar);
    g_key_file_set_boolean(key, "ui", "show_toolbar", config->show_toolbar);
    g_key_file_set_boolean(key, "ui", "show_breadcrumbs", config->show_breadcrumbs);

    g_key_file_set_boolean(key, "delete", "confirm_delete", config->confirm_delete);
    g_key_file_set_boolean(key, "session", "restore_tabs", config->restore_tabs);

    if (config->open_tabs != NULL && config->open_tabs->len > 0) {
        g_auto(GStrv) tabs = g_new0(char *, config->open_tabs->len + 1);
        for (guint i = 0; i < config->open_tabs->len; i++) {
            tabs[i] = g_strdup(g_ptr_array_index(config->open_tabs, i));
        }
        g_key_file_set_string_list(key, "session", "open_tabs", (const char * const *)tabs, config->open_tabs->len);
    }

    data = g_key_file_to_data(key, &length, error);
    if (data == NULL) {
        return FALSE;
    }

    return lmme_safe_write_file(path, data, length, error);
}

void
lmme_config_set_last_workspace(LmmeConfig *config, const char *path)
{
    g_free(config->last_workspace);
    config->last_workspace = g_strdup(path != NULL ? path : "");
}

void
lmme_config_set_open_tabs(LmmeConfig *config, GPtrArray *paths)
{
    g_ptr_array_set_size(config->open_tabs, 0);

    if (paths == NULL) {
        return;
    }

    for (guint i = 0; i < paths->len; i++) {
        const char *path = g_ptr_array_index(paths, i);
        if (path != NULL && path[0] != '\0') {
            g_ptr_array_add(config->open_tabs, g_strdup(path));
        }
    }
}
