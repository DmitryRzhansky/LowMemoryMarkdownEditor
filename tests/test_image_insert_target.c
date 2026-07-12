#include <glib.h>
#include <glib/gstdio.h>

#include "app/app.h"
#include "document/document.h"
#include "document/tabs.h"
#include "features/image_insert.h"

typedef struct {
    GMainLoop *loop;
    gboolean saved;
    GError *error;
} PngSaveResult;

static void
on_png_saved(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    PngSaveResult *save_result = user_data;

    save_result->saved = lmme_image_texture_save_png_finish(GDK_TEXTURE(source_object),
                                                            result,
                                                            &save_result->error);
    g_main_loop_quit(save_result->loop);
}

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

static void
test_texture_png_save_is_async_and_valid(void)
{
    const guint8 pixels[] = {
        255, 0, 0, 255,
        0, 255, 0, 255,
        0, 0, 255, 255,
        255, 255, 255, 255,
    };
    g_autoptr(GBytes) bytes = g_bytes_new_static(pixels, sizeof(pixels));
    g_autoptr(GdkTexture) texture = GDK_TEXTURE(gdk_memory_texture_new(2,
                                                                       2,
                                                                       GDK_MEMORY_R8G8B8A8,
                                                                       bytes,
                                                                       8));
    g_autofree char *directory = g_dir_make_tmp("lmme-image-png-test-XXXXXX", NULL);
    g_autofree char *path = g_build_filename(directory, "image.png", NULL);
    g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
    g_autoptr(GdkTexture) loaded = NULL;
    g_autoptr(GError) load_error = NULL;
    guint8 expected_pixels[16] = {0};
    guint8 loaded_pixels[16] = {0};
    PngSaveResult result = {.loop = loop};

    lmme_image_texture_save_png_async(texture, path, NULL, on_png_saved, &result);
    g_assert_false(result.saved);
    g_main_loop_run(loop);
    g_assert_no_error(result.error);
    g_assert_true(result.saved);
    loaded = gdk_texture_new_from_filename(path, &load_error);
    g_assert_no_error(load_error);
    g_assert_nonnull(loaded);
    g_assert_cmpint(gdk_texture_get_width(loaded), ==, 2);
    g_assert_cmpint(gdk_texture_get_height(loaded), ==, 2);
    gdk_texture_download(texture, expected_pixels, 8);
    gdk_texture_download(loaded, loaded_pixels, 8);
    g_assert_cmpmem(loaded_pixels,
                    sizeof(loaded_pixels),
                    expected_pixels,
                    sizeof(expected_pixels));

    g_clear_error(&result.error);
    g_unlink(path);
    g_rmdir(directory);
}

static void
test_texture_png_save_honors_cancellation(void)
{
    const guint8 pixels[] = {0, 0, 0, 255};
    g_autoptr(GBytes) bytes = g_bytes_new_static(pixels, sizeof(pixels));
    g_autoptr(GdkTexture) texture = GDK_TEXTURE(gdk_memory_texture_new(1,
                                                                       1,
                                                                       GDK_MEMORY_R8G8B8A8,
                                                                       bytes,
                                                                       4));
    g_autofree char *directory = g_dir_make_tmp("lmme-image-png-cancel-XXXXXX", NULL);
    g_autofree char *path = g_build_filename(directory, "image.png", NULL);
    g_autoptr(GCancellable) cancellable = g_cancellable_new();
    g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
    PngSaveResult result = {.loop = loop};

    g_cancellable_cancel(cancellable);
    lmme_image_texture_save_png_async(texture,
                                      path,
                                      cancellable,
                                      on_png_saved,
                                      &result);
    g_main_loop_run(loop);
    g_assert_error(result.error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    g_assert_false(result.saved);
    g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));

    g_clear_error(&result.error);
    g_rmdir(directory);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/image-insert/document-id", test_document_lookup_uses_stable_id);
    g_test_add_func("/image-insert/link", test_markdown_link_is_workspace_relative);
    g_test_add_func("/image-insert/png-save", test_texture_png_save_is_async_and_valid);
    g_test_add_func("/image-insert/png-cancel", test_texture_png_save_honors_cancellation);
    return g_test_run();
}
