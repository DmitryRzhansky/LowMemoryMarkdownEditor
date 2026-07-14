#include <glib.h>
#include <gtksourceview/gtksource.h>

#include "editor/preview.h"
#include "editor/preview_style.h"

#define BENCHMARK_RUNS 5U

typedef enum {
    BENCHMARK_DATASET_ORDINARY = 0,
    BENCHMARK_DATASET_TABLE_HEAVY
} BenchmarkDataset;

static const char *
dataset_name(BenchmarkDataset dataset)
{
    switch (dataset) {
    case BENCHMARK_DATASET_ORDINARY:
        return "ordinary";
    case BENCHMARK_DATASET_TABLE_HEAVY:
        return "table-heavy";
    default:
        return "unknown";
    }
}

static char *
make_ordinary_markdown(gsize target_bytes)
{
    GString *text = g_string_sized_new(target_bytes + 128);
    const char *line = "## Heading **bold** *italic* [link](img/file.png) and `code`\n";

    while (text->len < target_bytes) {
        g_string_append(text, line);
    }
    return g_string_free(text, FALSE);
}

static char *
make_table_heavy_markdown(gsize target_bytes)
{
    GString *text = g_string_sized_new(target_bytes + 256);
    const char *block =
        "| Col A | Col B | Col C |\n"
        "|-------|-------|-------|\n"
        "| **x** | `y`   | [z](u) |\n"
        "| one   | two   | three  |\n"
        "\n";

    while (text->len < target_bytes) {
        g_string_append(text, block);
    }
    return g_string_free(text, FALSE);
}

static char *
make_markdown(BenchmarkDataset dataset, gsize target_bytes)
{
    if (dataset == BENCHMARK_DATASET_TABLE_HEAVY) {
        return make_table_heavy_markdown(target_bytes);
    }
    return make_ordinary_markdown(target_bytes);
}

static const char *
result_name(LmmePreviewApplyResult result)
{
    switch (result) {
    case LMME_PREVIEW_APPLY_OK:
        return "OK";
    case LMME_PREVIEW_APPLY_SKIPPED_LARGE_FILE:
        return "SKIPPED_LARGE_FILE";
    case LMME_PREVIEW_APPLY_FAILED:
        return "FAILED";
    default:
        return "UNKNOWN";
    }
}

static double
median_elapsed_ms(gint64 *samples, guint count)
{
    for (guint i = 0; i + 1 < count; i++) {
        for (guint j = i + 1; j < count; j++) {
            if (samples[j] < samples[i]) {
                gint64 tmp = samples[i];
                samples[i] = samples[j];
                samples[j] = tmp;
            }
        }
    }
    return (double)samples[count / 2] / 1000.0;
}

static void
run_case(BenchmarkDataset dataset, gsize bytes)
{
    g_autofree char *markdown = make_markdown(dataset, bytes);
    g_autoptr(GtkSourceBuffer) buffer = gtk_source_buffer_new(NULL);
    gint64 samples[BENCHMARK_RUNS];
    LmmePreviewApplyResult last_result = LMME_PREVIEW_APPLY_FAILED;
    guint range_count = 0;
    gboolean range_count_valid = FALSE;

    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(buffer), markdown, -1);

    for (guint run = 0; run < BENCHMARK_RUNS; run++) {
        gint64 started = 0;
        gint64 elapsed = 0;

        lmme_preview_clear_editable_preview(NULL, GTK_TEXT_BUFFER(buffer));
        started = g_get_monotonic_time();
        last_result = lmme_preview_apply_editable_preview(NULL,
                                                         GTK_TEXT_BUFFER(buffer),
                                                         TRUE,
                                                         TRUE,
                                                         NULL);
        elapsed = g_get_monotonic_time() - started;
        samples[run] = elapsed;
    }

    if (last_result == LMME_PREVIEW_APPLY_OK) {
        g_autoptr(GPtrArray) ranges = lmme_preview_collect_ranges(markdown, TRUE, G_MAXUINT, TRUE);
        range_count = ranges->len;
        range_count_valid = TRUE;
    }

    if (range_count_valid) {
        g_print("%s | %" G_GSIZE_FORMAT " | %s | %.3f | %u\n",
                dataset_name(dataset),
                bytes,
                result_name(last_result),
                median_elapsed_ms(samples, BENCHMARK_RUNS),
                range_count);
    } else {
        g_print("%s | %" G_GSIZE_FORMAT " | %s | %.3f | N/A\n",
                dataset_name(dataset),
                bytes,
                result_name(last_result),
                median_elapsed_ms(samples, BENCHMARK_RUNS));
    }
}

int
main(void)
{
    const gsize sizes[] = {10U * 1024U, 1024U * 1024U, 10U * 1024U * 1024U};
    BenchmarkDataset datasets[] = {BENCHMARK_DATASET_ORDINARY, BENCHMARK_DATASET_TABLE_HEAVY};

    gtk_init();

    g_print("dataset | bytes | result | elapsed_ms | range_count\n");
    for (guint d = 0; d < G_N_ELEMENTS(datasets); d++) {
        for (guint s = 0; s < G_N_ELEMENTS(sizes); s++) {
            run_case(datasets[d], sizes[s]);
        }
    }

    return 0;
}
