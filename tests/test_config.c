#include <glib.h>
#include <glib/gstdio.h>

#include "config.h"

static void
test_defaults(void)
{
    LmmeConfig config;
    lmme_config_init_defaults(&config);

    g_assert_cmpint(config.window_width, ==, 1200);
    g_assert_cmpint(config.window_height, ==, 800);
    g_assert_true(config.show_hidden_files);
    g_assert_true(config.autosave);
    g_assert_cmpuint(config.autosave_delay_ms, ==, 700);
    g_assert_false(config.preview_enabled);
    g_assert_true(config.confirm_delete);

    lmme_config_clear(&config);
}

static void
test_missing_config_loads_defaults(void)
{
    LmmeConfig config;
    g_autofree char *dir = g_dir_make_tmp("lmme-test-config-XXXXXX", NULL);
    g_autofree char *path = g_build_filename(dir, "missing.ini", NULL);

    g_assert_true(lmme_config_load(&config, path, NULL));
    g_assert_cmpint(config.window_width, ==, 1200);
    g_assert_cmpstr(config.last_workspace, ==, "");

    lmme_config_clear(&config);
    g_rmdir(dir);
}

static void
test_save_load_roundtrip(void)
{
    LmmeConfig config;
    LmmeConfig loaded;
    g_autofree char *dir = g_dir_make_tmp("lmme-test-config-XXXXXX", NULL);
    g_autofree char *path = g_build_filename(dir, "config.ini", NULL);

    lmme_config_init_defaults(&config);
    config.window_width = 900;
    config.preview_enabled = TRUE;
    lmme_config_set_last_workspace(&config, "/tmp/workspace");

    g_assert_true(lmme_config_save(&config, path, NULL));
    g_assert_true(lmme_config_load(&loaded, path, NULL));
    g_assert_cmpint(loaded.window_width, ==, 900);
    g_assert_true(loaded.preview_enabled);
    g_assert_cmpstr(loaded.last_workspace, ==, "/tmp/workspace");

    lmme_config_clear(&config);
    lmme_config_clear(&loaded);
    g_remove(path);
    g_rmdir(dir);
}

static void
test_invalid_config_falls_back(void)
{
    LmmeConfig config;
    g_autofree char *dir = g_dir_make_tmp("lmme-test-config-XXXXXX", NULL);
    g_autofree char *path = g_build_filename(dir, "config.ini", NULL);
    g_autoptr(GError) error = NULL;

    g_assert_true(g_file_set_contents(path, "[window\nbad", -1, NULL));
    g_assert_true(lmme_config_load(&config, path, &error));
    g_assert_nonnull(error);
    g_assert_cmpint(config.window_width, ==, 1200);

    lmme_config_clear(&config);
    g_remove(path);
    g_rmdir(dir);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/config/defaults", test_defaults);
    g_test_add_func("/config/missing", test_missing_config_loads_defaults);
    g_test_add_func("/config/roundtrip", test_save_load_roundtrip);
    g_test_add_func("/config/invalid", test_invalid_config_falls_back);
    return g_test_run();
}
