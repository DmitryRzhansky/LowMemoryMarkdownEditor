#include <glib.h>
#include <glib/gstdio.h>

#include "app/app.h"
#include "infra/util.h"

static void
test_extensions(void)
{
    g_assert_true(lmme_path_has_markdown_extension("note.md"));
    g_assert_true(lmme_path_has_markdown_extension("note.MARKDOWN"));
    g_assert_false(lmme_path_has_markdown_extension("note.txt"));

    g_assert_true(lmme_path_has_image_extension("a.png"));
    g_assert_true(lmme_path_has_image_extension("a.JPEG"));
    g_assert_true(lmme_path_has_image_extension("a.webp"));
    g_assert_false(lmme_path_has_image_extension("a.pdf"));
}

static void
test_slugify(void)
{
    g_autofree char *slug = lmme_slugify("Project Concept 001!", 48);
    g_assert_cmpstr(slug, ==, "project-concept-001");

    g_autofree char *cyrillic = lmme_slugify("Привет мир", 48);
    g_assert_cmpstr(cyrillic, ==, "note");
}

static void
test_image_filename(void)
{
    g_autofree char *dir = g_dir_make_tmp("lmme-test-util-XXXXXX", NULL);
    g_autoptr(GDateTime) now = g_date_time_new_local(2026, 7, 8, 17, 30, 44);
    g_autofree char *filename = lmme_generate_image_filename(dir, "/tmp/project-concept.md", "png", now);
    g_assert_cmpstr(filename, ==, "2026-07-08-173044-project-concept-001.png");

    g_autofree char *existing = g_build_filename(dir, filename, NULL);
    g_assert_true(g_file_set_contents(existing, "", 0, NULL));

    g_autofree char *second = lmme_generate_image_filename(dir, "/tmp/project-concept.md", ".png", now);
    g_assert_cmpstr(second, ==, "2026-07-08-173044-project-concept-002.png");

    g_remove(existing);
    g_rmdir(dir);
}

static void
test_relative_path(void)
{
    g_autofree char *relative = lmme_path_relative_to("/tmp/workspace", "/tmp/workspace/img/a.png");
    g_assert_cmpstr(relative, ==, "img/a.png");
    g_assert_true(lmme_path_is_inside("/tmp/workspace", "/tmp/workspace/notes/a.md"));
    g_assert_false(lmme_path_is_inside("/tmp/workspace", "/tmp/workspace-other/a.md"));
}

static void
test_path_context_lifecycle(void)
{
    LmmePathContext context = {0};

    lmme_path_context_set(&context, "/workspace/note.md", LMME_FILE_KIND_MARKDOWN, FALSE);
    g_assert_cmpstr(context.path, ==, "/workspace/note.md");
    g_assert_cmpint(context.kind, ==, LMME_FILE_KIND_MARKDOWN);
    g_assert_false(context.empty_area);

    lmme_path_context_set(&context, NULL, LMME_FILE_KIND_OTHER, TRUE);
    g_assert_null(context.path);
    g_assert_cmpint(context.kind, ==, LMME_FILE_KIND_OTHER);
    g_assert_true(context.empty_area);

    lmme_path_context_clear(&context);
    g_assert_null(context.path);
    g_assert_cmpint(context.kind, ==, LMME_FILE_KIND_OTHER);
    g_assert_false(context.empty_area);
}

static void
test_validate_basename_handles_null(void)
{
    g_autoptr(GError) error = NULL;

    g_assert_false(lmme_validate_basename(NULL, &error));
    g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL);
    g_assert_cmpstr(error->message, ==, "Name must not be empty.");

    g_clear_error(&error);
    g_assert_false(lmme_validate_basename("  ", &error));
    g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL);

    g_clear_error(&error);
    g_assert_false(lmme_validate_basename("notes/name", &error));
    g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL);

    g_clear_error(&error);
    g_assert_true(lmme_validate_basename("name.md", &error));
    g_assert_no_error(error);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/util/extensions", test_extensions);
    g_test_add_func("/util/slugify", test_slugify);
    g_test_add_func("/util/image-filename", test_image_filename);
    g_test_add_func("/util/relative-path", test_relative_path);
    g_test_add_func("/util/path-context", test_path_context_lifecycle);
    g_test_add_func("/util/validate-basename", test_validate_basename_handles_null);
    return g_test_run();
}
