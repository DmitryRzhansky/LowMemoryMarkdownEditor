#include <glib.h>
#include <string.h>

#include "document/recovery.h"

static void
test_recovery_path(void)
{
    g_autofree char *path = lmme_recovery_path_for_original("/tmp/workspace/note.md");
    g_assert_nonnull(strstr(path, ".recover"));
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/recovery/path", test_recovery_path);
    return g_test_run();
}
