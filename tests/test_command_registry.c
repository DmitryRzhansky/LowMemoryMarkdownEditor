#include <glib.h>

#include "command/command_registry.h"

static void
test_lookup(void)
{
    const LmmeCommandDef *command = lmme_command_registry_find("file.open_workspace");

    g_assert_nonnull(command);
    g_assert_cmpstr(command->action_name, ==, "open");
    g_assert_cmpstr(command->default_accel, ==, "<Ctrl>O");
    g_assert_cmpint(command->category, ==, LMME_COMMAND_CATEGORY_FILE);
    g_assert_null(lmme_command_registry_find("missing.command"));
}

static void
test_all(void)
{
    gsize count = 0;
    const LmmeCommandDef *commands = lmme_command_registry_get_all(&count);

    g_assert_nonnull(commands);
    g_assert_cmpuint(count, ==, 42);
    g_assert_cmpstr(commands[0].id, ==, "file.open_workspace");
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/command-registry/lookup", test_lookup);
    g_test_add_func("/command-registry/all", test_all);
    return g_test_run();
}
