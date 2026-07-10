#include <glib.h>

#include "app/app.h"
#include "document/document.h"
#include "document/tabs.h"
#include "features/image_insert.h"

static void
test_document_lookup_uses_stable_id(void)
{
    LmmeApp app = {0};
    LmmeDocument first = {0};
    LmmeDocument second = {0};

    app.documents = g_ptr_array_new();
    first.id = 10;
    second.id = 20;
    g_ptr_array_add(app.documents, &first);
    g_ptr_array_add(app.documents, &second);

    g_assert_true(lmme_tabs_find_by_id(&app, 10) == &first);
    g_assert_true(lmme_tabs_find_by_id(&app, 20) == &second);
    g_assert_null(lmme_tabs_find_by_id(&app, 30));
    g_ptr_array_unref(app.documents);
}

static void
test_markdown_link_is_workspace_relative(void)
{
    g_autofree char *link = lmme_image_markdown_link("/tmp/workspace",
                                                     "/tmp/workspace/img/image.png");
    g_assert_cmpstr(link, ==, "![](img/image.png)");
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/image-insert/document-id", test_document_lookup_uses_stable_id);
    g_test_add_func("/image-insert/link", test_markdown_link_is_workspace_relative);
    return g_test_run();
}
