#include "preview.h"

#include <string.h>

static char *
skip_frontmatter(const char *markdown, gboolean hide_frontmatter)
{
    const char *input = markdown != NULL ? markdown : "";
    const char *line = NULL;

    if (!hide_frontmatter ||
        (!g_str_has_prefix(input, "---\n") && !g_str_has_prefix(input, "+++\n"))) {
        return g_strdup(input);
    }

    const char delimiter = input[0];
    line = input + 4;
    while (*line != '\0') {
        const char *next = strchr(line, '\n');
        gsize len = next != NULL ? (gsize)(next - line) : strlen(line);

        if (len == 3 && line[0] == delimiter && line[1] == delimiter && line[2] == delimiter) {
            return g_strdup(next != NULL ? next + 1 : "");
        }
        if (next == NULL) {
            break;
        }
        line = next + 1;
    }

    return g_strdup(input);
}

static void
append_until(GString *out, const char *start, const char *end)
{
    if (end > start) {
        g_string_append_len(out, start, (gssize)(end - start));
    }
}

static char *
clean_inline_markdown(const char *line)
{
    GString *out = g_string_new(NULL);
    const char *p = line != NULL ? line : "";

    while (*p != '\0') {
        if (g_str_has_prefix(p, "![") || *p == '[') {
            gboolean is_image = g_str_has_prefix(p, "![");
            const char *label_start = p + (is_image ? 2 : 1);
            const char *label_end = strstr(label_start, "](");
            const char *url_end = label_end != NULL ? strchr(label_end + 2, ')') : NULL;

            if (label_end != NULL && url_end != NULL) {
                if (is_image) {
                    g_string_append(out, "[Image: ");
                    append_until(out, label_end + 2, url_end);
                    g_string_append_c(out, ']');
                } else {
                    append_until(out, label_start, label_end);
                    g_string_append(out, " (");
                    append_until(out, label_end + 2, url_end);
                    g_string_append_c(out, ')');
                }
                p = url_end + 1;
                continue;
            }
        }

        if (g_str_has_prefix(p, "**") || g_str_has_prefix(p, "__")) {
            p += 2;
            continue;
        }
        if (*p == '*' || *p == '_' || *p == '`') {
            p++;
            continue;
        }

        g_string_append_c(out, *p);
        p++;
    }

    return g_string_free(out, FALSE);
}

static gboolean
line_is_ordered_list(const char *line, const char **content)
{
    const char *p = line;

    while (g_ascii_isdigit(*p)) {
        p++;
    }

    if (p == line || *p != '.' || p[1] != ' ') {
        return FALSE;
    }

    *content = p + 2;
    return TRUE;
}

static void
insert_tagged(GtkTextBuffer *buffer, const char *text, const char *tag)
{
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);

    if (tag != NULL) {
        gtk_text_buffer_insert_with_tags_by_name(buffer, &end, text, -1, tag, NULL);
    } else {
        gtk_text_buffer_insert(buffer, &end, text, -1);
    }
}

static void
insert_line(GtkTextBuffer *buffer, const char *text, const char *tag)
{
    insert_tagged(buffer, text, tag);
    insert_tagged(buffer, "\n", tag);
}

static void
create_tags(GtkTextBuffer *buffer)
{
    GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
    if (gtk_text_tag_table_lookup(table, "body") != NULL) {
        return;
    }

    gtk_text_buffer_create_tag(buffer, "body",
                               "family", "sans-serif",
                               "foreground", "#22282d",
                               "pixels-above-lines", 2,
                               "pixels-below-lines", 2,
                               NULL);
    gtk_text_buffer_create_tag(buffer, "h1",
                               "family", "sans-serif",
                               "foreground", "#155f65",
                               "weight", PANGO_WEIGHT_BOLD,
                               "scale", 1.35,
                               "pixels-above-lines", 10,
                               "pixels-below-lines", 5,
                               NULL);
    gtk_text_buffer_create_tag(buffer, "h2",
                               "family", "sans-serif",
                               "foreground", "#1c7076",
                               "weight", PANGO_WEIGHT_BOLD,
                               "scale", 1.18,
                               "pixels-above-lines", 8,
                               "pixels-below-lines", 4,
                               NULL);
    gtk_text_buffer_create_tag(buffer, "quote",
                               "family", "sans-serif",
                               "foreground", "#4d5962",
                               "style", PANGO_STYLE_ITALIC,
                               "left-margin", 18,
                               NULL);
    gtk_text_buffer_create_tag(buffer, "code",
                               "family", "monospace",
                               "foreground", "#1f2428",
                               "background", "#c7ccd1",
                               "left-margin", 18,
                               "right-margin", 18,
                               NULL);
    gtk_text_buffer_create_tag(buffer, "image",
                               "family", "sans-serif",
                               "foreground", "#315f8a",
                               "style", PANGO_STYLE_ITALIC,
                               NULL);
}

GtkWidget *
lmme_preview_create_view(void)
{
    GtkWidget *view = gtk_text_view_new();
    gtk_widget_add_css_class(view, "preview-view");
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view), 22);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(view), 22);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(view), 16);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(view), 16);
    gtk_widget_set_hexpand(view, TRUE);
    gtk_widget_set_vexpand(view, TRUE);
    return view;
}

void
lmme_preview_set_markdown(GtkWidget *preview_view, const char *markdown, gboolean hide_frontmatter)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(preview_view));
    g_autofree char *input = skip_frontmatter(markdown, hide_frontmatter);
    g_auto(GStrv) lines = g_strsplit(input, "\n", -1);
    gboolean in_code = FALSE;

    gtk_text_buffer_set_text(buffer, "", 0);
    create_tags(buffer);

    for (gsize i = 0; lines[i] != NULL; i++) {
        char *line = lines[i];
        char *trimmed = g_strstrip(line);
        const char *content = NULL;

        if (g_str_has_prefix(trimmed, "```") || g_str_has_prefix(trimmed, "~~~")) {
            in_code = !in_code;
            continue;
        }

        if (in_code) {
            insert_line(buffer, trimmed, "code");
            continue;
        }

        if (trimmed[0] == '\0') {
            insert_tagged(buffer, "\n", NULL);
            continue;
        }

        int hashes = 0;
        while (trimmed[hashes] == '#') {
            hashes++;
        }
        if (hashes > 0 && hashes <= 6 && trimmed[hashes] == ' ') {
            g_autofree char *clean = clean_inline_markdown(trimmed + hashes + 1);
            insert_line(buffer, clean, hashes == 1 ? "h1" : "h2");
            continue;
        }

        if (g_str_has_prefix(trimmed, ">")) {
            content = trimmed + 1;
            while (*content == ' ') {
                content++;
            }
            g_autofree char *clean = clean_inline_markdown(content);
            insert_line(buffer, clean, "quote");
            continue;
        }

        if (g_str_has_prefix(trimmed, "- ") ||
            g_str_has_prefix(trimmed, "* ") ||
            g_str_has_prefix(trimmed, "+ ")) {
            g_autofree char *clean = clean_inline_markdown(trimmed + 2);
            g_autofree char *bullet = g_strdup_printf("• %s", clean);
            insert_line(buffer, bullet, "body");
            continue;
        }

        if (line_is_ordered_list(trimmed, &content)) {
            g_autofree char *clean = clean_inline_markdown(content);
            g_autofree char *numbered = g_strdup_printf("• %s", clean);
            insert_line(buffer, numbered, "body");
            continue;
        }

        if (g_str_has_prefix(trimmed, "![") && strstr(trimmed, "](") != NULL) {
            g_autofree char *clean = clean_inline_markdown(trimmed);
            insert_line(buffer, clean, "image");
            continue;
        }

        g_autofree char *clean = clean_inline_markdown(trimmed);
        insert_line(buffer, clean, "body");
    }
}
