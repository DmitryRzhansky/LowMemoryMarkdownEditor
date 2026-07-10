#include <glib.h>

#include "command/command_handlers.h"
#include "command/command_registry.h"

static void
test_lookup(void)
{
    const LmmeCommandDef *command = lmme_command_registry_find("file.open_workspace");

    g_assert_nonnull(command);
    g_assert_cmpstr(command->action_name, ==, "open");
    g_assert_cmpstr(command->default_accels[0], ==, "<Ctrl>O");
    g_assert_cmpint(command->category, ==, LMME_COMMAND_CATEGORY_FILE);
    g_assert_null(lmme_command_registry_find("missing.command"));
}

static void
test_catalog_contracts(void)
{
    g_autoptr(GError) error = NULL;
    g_assert_true(lmme_command_registry_validate(&error));
    g_assert_no_error(error);
}

static void
test_all(void)
{
    gsize count = 0;
    const LmmeCommandDef *commands = lmme_command_registry_get_all(&count);

    g_assert_nonnull(commands);
    g_assert_cmpuint(count, >, 0);
    g_assert_cmpstr(commands[0].id, ==, "file.open_workspace");
    for (gsize i = 0; i < count; i++) {
        g_assert_cmpint(lmme_command_handler_domain(commands[i].handler),
                        !=,
                        LMME_COMMAND_DOMAIN_NONE);
    }
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/command-registry/lookup", test_lookup);
    g_test_add_func("/command-registry/all", test_all);
    g_test_add_func("/command-registry/contracts", test_catalog_contracts);
    return g_test_run();
}
