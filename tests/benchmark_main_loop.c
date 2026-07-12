#define _GNU_SOURCE

#include <fcntl.h>
#include <gdk/gdk.h>
#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "document/file_io.h"
#include "workspace/workspace.h"

typedef void (*BenchmarkOperation)(gpointer user_data);

static guint64
peak_rss_kib(void)
{
    g_autofree char *status = NULL;
    const char *peak = NULL;
    guint64 value = 0;

    if (!g_file_get_contents("/proc/self/status", &status, NULL, NULL)) {
        return 0;
    }
    peak = strstr(status, "VmHWM:");
    if (peak != NULL) {
        (void)sscanf(peak, "VmHWM: %" G_GUINT64_FORMAT " kB", &value);
    }
    return value;
}

static void
run_samples(const char *name,
            guint samples,
            BenchmarkOperation operation,
            gpointer user_data)
{
    g_autofree gint64 *elapsed = g_new(gint64, samples);
    guint p95_index = 0;

    for (guint i = 0; i < samples; i++) {
        gint64 started = g_get_monotonic_time();
        operation(user_data);
        elapsed[i] = g_get_monotonic_time() - started;
    }
    for (guint i = 1; i < samples; i++) {
        gint64 value = elapsed[i];
        guint position = i;

        while (position > 0 && elapsed[position - 1] > value) {
            elapsed[position] = elapsed[position - 1];
            position--;
        }
        elapsed[position] = value;
    }
    p95_index = ((samples * 95U + 99U) / 100U) - 1U;
    g_print("%s: median %.3f ms, p95 %.3f ms, peak RSS %" G_GUINT64_FORMAT " KiB\n",
            name,
            (double)elapsed[samples / 2U] / 1000.0,
            (double)elapsed[p95_index] / 1000.0,
            peak_rss_kib());
}

typedef struct {
    const char *path;
    gsize maximum_bytes;
} ReadContext;

static void
read_operation(gpointer user_data)
{
    ReadContext *context = user_data;
    g_autofree char *contents = NULL;

    g_assert_true(lmme_file_read_utf8(context->path,
                                      context->maximum_bytes,
                                      &contents,
                                      NULL,
                                      NULL));
}

typedef struct {
    GFile *source;
    GFile *destination;
} CopyContext;

static void
copy_operation(gpointer user_data)
{
    CopyContext *context = user_data;

    (void)g_file_delete(context->destination, NULL, NULL);
    g_assert_true(g_file_copy(context->source,
                              context->destination,
                              G_FILE_COPY_NONE,
                              NULL,
                              NULL,
                              NULL,
                              NULL));
}

typedef struct {
    const char *path;
    guint entries;
} WorkspaceScanContext;

static void
workspace_scan_operation(gpointer user_data)
{
    WorkspaceScanContext *context = user_data;
    LmmeWorkspace *workspace = lmme_workspace_new_scanned(context->path, TRUE, TRUE, NULL);

    g_assert_nonnull(workspace);
    g_assert_cmpuint(workspace->root->children->len, ==, context->entries);
    lmme_workspace_free(workspace);
}

typedef struct {
    GdkTexture *texture;
    const char *path;
} PngContext;

static void
png_operation(gpointer user_data)
{
    PngContext *context = user_data;
    g_assert_true(gdk_texture_save_to_png(context->texture, context->path));
}

static void
write_bytes(const char *path, gsize bytes)
{
    g_autofree char *contents = g_malloc(bytes);

    g_assert_cmpuint(bytes, <=, G_MAXSSIZE);
    memset(contents, 'x', bytes);
    g_assert_true(g_file_set_contents(path, contents, (gssize)bytes, NULL));
}

static void
benchmark_reads_and_copies(const char *root)
{
    const gsize sizes[] = {100U * 1024U, 1024U * 1024U, 2U * 1024U * 1024U, 10U * 1024U * 1024U};

    for (guint i = 0; i < G_N_ELEMENTS(sizes); i++) {
        g_autofree char *source_path = g_strdup_printf("%s/read-%u.md", root, i);
        g_autofree char *copy_path = g_strdup_printf("%s/copy-%u.png", root, i);
        g_autofree char *read_name = g_strdup_printf("read %" G_GSIZE_FORMAT " bytes", sizes[i]);
        g_autofree char *copy_name = g_strdup_printf("copy %" G_GSIZE_FORMAT " bytes", sizes[i]);
        g_autoptr(GFile) source = NULL;
        g_autoptr(GFile) destination = NULL;
        ReadContext read_context = {.path = source_path, .maximum_bytes = sizes[i]};
        CopyContext copy_context;

        write_bytes(source_path, sizes[i]);
        source = g_file_new_for_path(source_path);
        destination = g_file_new_for_path(copy_path);
        copy_context.source = source;
        copy_context.destination = destination;
        run_samples(read_name, 9, read_operation, &read_context);
        run_samples(copy_name, 7, copy_operation, &copy_context);
        g_unlink(copy_path);
        g_unlink(source_path);
    }
}

static void
benchmark_workspace_scans(const char *root)
{
    const guint counts[] = {100, 1000, 10000};

    for (guint fixture = 0; fixture < G_N_ELEMENTS(counts); fixture++) {
        g_autofree char *directory = g_strdup_printf("%s/workspace-%u", root, counts[fixture]);
        g_autofree char *name = g_strdup_printf("workspace root scan %u entries", counts[fixture]);
        WorkspaceScanContext context = {.path = directory, .entries = counts[fixture]};

        g_assert_cmpint(g_mkdir(directory, 0700), ==, 0);
        for (guint i = 0; i < counts[fixture]; i++) {
            g_autofree char *file_name = g_strdup_printf("note-%05u.md", i);
            g_autofree char *path = g_build_filename(directory, file_name, NULL);
            g_assert_true(g_file_set_contents(path, "", 0, NULL));
        }
        run_samples(name, 5, workspace_scan_operation, &context);
        for (guint i = 0; i < counts[fixture]; i++) {
            g_autofree char *file_name = g_strdup_printf("note-%05u.md", i);
            g_autofree char *path = g_build_filename(directory, file_name, NULL);
            g_unlink(path);
        }
        g_rmdir(directory);
    }
}

static void
benchmark_png_encoding(const char *root)
{
    const guint widths[] = {512, 1600, 3600};

    for (guint i = 0; i < G_N_ELEMENTS(widths); i++) {
        guint width = widths[i];
        guint height = widths[i];
        gsize stride = (gsize)width * 4U;
        gsize bytes_count = stride * height;
        g_autofree guint8 *pixels = g_malloc0(bytes_count);
        g_autoptr(GBytes) bytes = g_bytes_new_take(g_steal_pointer(&pixels), bytes_count);
        g_autoptr(GdkTexture) texture = GDK_TEXTURE(gdk_memory_texture_new((int)width,
                                                                           (int)height,
                                                                           GDK_MEMORY_R8G8B8A8_PREMULTIPLIED,
                                                                           bytes,
                                                                           stride));
        g_autofree char *path = g_strdup_printf("%s/texture-%u.png", root, width);
        g_autofree char *name = g_strdup_printf("PNG encode %" G_GSIZE_FORMAT " raw bytes", bytes_count);
        PngContext context = {.texture = texture, .path = path};

        run_samples(name, 3, png_operation, &context);
        g_unlink(path);
    }
}

static void
benchmark_recursive_delete(const char *root)
{
    const guint counts[] = {100, 1000, 10000};

    for (guint fixture = 0; fixture < G_N_ELEMENTS(counts); fixture++) {
        g_autofree char *workspace_path = g_strdup_printf("%s/delete-workspace-%u", root, counts[fixture]);
        g_autofree char *target = g_build_filename(workspace_path, "target", NULL);
        g_autofree char *name = g_strdup_printf("recursive delete %u files", counts[fixture]);
        LmmeWorkspace *workspace = NULL;
        gint64 started = 0;
        gint64 elapsed = 0;

        g_assert_cmpint(g_mkdir(workspace_path, 0700), ==, 0);
        g_assert_cmpint(g_mkdir(target, 0700), ==, 0);
        for (guint i = 0; i < counts[fixture]; i++) {
            g_autofree char *file_name = g_strdup_printf("file-%05u.md", i);
            g_autofree char *path = g_build_filename(target, file_name, NULL);
            g_assert_true(g_file_set_contents(path, "", 0, NULL));
        }
        workspace = lmme_workspace_new(workspace_path);
        started = g_get_monotonic_time();
        g_assert_true(lmme_workspace_delete_path(workspace, target, NULL));
        elapsed = g_get_monotonic_time() - started;
        g_print("%s: %.3f ms, peak RSS %" G_GUINT64_FORMAT " KiB\n",
                name,
                (double)elapsed / 1000.0,
                peak_rss_kib());
        lmme_workspace_free(workspace);
        g_rmdir(workspace_path);
    }
}

int
main(void)
{
    g_autofree char *root = g_dir_make_tmp("lmme-main-loop-benchmark-XXXXXX", NULL);

    g_assert_nonnull(root);
    benchmark_reads_and_copies(root);
    benchmark_workspace_scans(root);
    benchmark_png_encoding(root);
    benchmark_recursive_delete(root);
    g_rmdir(root);
    return 0;
}
