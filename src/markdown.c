#include "markdown.h"

#include <cmark.h>
#include <string.h>

static void render_node(cmark_node *node, GString *out, guint depth);

static void
append_text(GString *out, const char *text)
{
    if (text != NULL) {
        g_string_append(out, text);
    }
}

static void
append_newline_if_needed(GString *out)
{
    if (out->len > 0 && out->str[out->len - 1] != '\n') {
        g_string_append_c(out, '\n');
    }
}

static void
append_blank_line(GString *out)
{
    append_newline_if_needed(out);
    if (out->len < 2 || out->str[out->len - 2] != '\n') {
        g_string_append_c(out, '\n');
    }
}

static gboolean
node_has_children(cmark_node *node)
{
    return cmark_node_first_child(node) != NULL;
}

static void
render_children(cmark_node *node, GString *out, guint depth)
{
    for (cmark_node *child = cmark_node_first_child(node); child != NULL; child = cmark_node_next(child)) {
        render_node(child, out, depth);
    }
}

static void
render_link(cmark_node *node, GString *out, guint depth)
{
    gsize before = out->len;
    const char *url = cmark_node_get_url(node);

    render_children(node, out, depth);

    if (url != NULL && url[0] != '\0') {
        if (out->len > before) {
            g_string_append_printf(out, " (%s)", url);
        } else {
            g_string_append(out, url);
        }
    }
}

static void
render_image(cmark_node *node, GString *out)
{
    const char *url = cmark_node_get_url(node);
    append_newline_if_needed(out);
    g_string_append_printf(out, "[Image: %s]", url != NULL ? url : "");
}

static void
render_node(cmark_node *node, GString *out, guint depth)
{
    switch (cmark_node_get_type(node)) {
    case CMARK_NODE_DOCUMENT:
        render_children(node, out, depth);
        break;

    case CMARK_NODE_HEADING:
        append_blank_line(out);
        render_children(node, out, depth);
        append_blank_line(out);
        break;

    case CMARK_NODE_PARAGRAPH:
        append_newline_if_needed(out);
        render_children(node, out, depth);
        append_blank_line(out);
        break;

    case CMARK_NODE_BLOCK_QUOTE:
        append_newline_if_needed(out);
        render_children(node, out, depth + 1);
        append_blank_line(out);
        break;

    case CMARK_NODE_LIST:
        append_newline_if_needed(out);
        render_children(node, out, depth);
        append_blank_line(out);
        break;

    case CMARK_NODE_ITEM:
        for (guint i = 0; i < depth; i++) {
            g_string_append(out, "  ");
        }
        if (node_has_children(node)) {
            render_children(node, out, depth + 1);
        }
        append_newline_if_needed(out);
        break;

    case CMARK_NODE_CODE_BLOCK:
        append_blank_line(out);
        append_text(out, cmark_node_get_literal(node));
        append_blank_line(out);
        break;

    case CMARK_NODE_THEMATIC_BREAK:
        append_blank_line(out);
        g_string_append(out, "----------");
        append_blank_line(out);
        break;

    case CMARK_NODE_TEXT:
    case CMARK_NODE_CODE:
    case CMARK_NODE_HTML_INLINE:
        append_text(out, cmark_node_get_literal(node));
        break;

    case CMARK_NODE_SOFTBREAK:
    case CMARK_NODE_LINEBREAK:
        g_string_append_c(out, '\n');
        break;

    case CMARK_NODE_EMPH:
    case CMARK_NODE_STRONG:
        render_children(node, out, depth);
        break;

    case CMARK_NODE_LINK:
        render_link(node, out, depth);
        break;

    case CMARK_NODE_IMAGE:
        render_image(node, out);
        break;

    case CMARK_NODE_HTML_BLOCK:
        break;

    default:
        render_children(node, out, depth);
        break;
    }
}

static char *
remove_frontmatter_if_needed(const char *markdown, gboolean hide_frontmatter)
{
    const char *start = markdown != NULL ? markdown : "";
    const char *line = NULL;

    if (!hide_frontmatter) {
        return g_strdup(start);
    }

    if (!g_str_has_prefix(start, "---\n") && !g_str_has_prefix(start, "+++\n")) {
        return g_strdup(start);
    }

    const char delimiter = start[0];
    line = start + 4;

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

    return g_strdup(start);
}

char *
lmme_markdown_to_preview_text(const char *markdown, gboolean hide_frontmatter)
{
    g_autofree char *input = remove_frontmatter_if_needed(markdown, hide_frontmatter);
    cmark_node *doc = cmark_parse_document(input, strlen(input), CMARK_OPT_DEFAULT);
    GString *out = g_string_new(NULL);
    char *result = NULL;

    if (doc == NULL) {
        return g_strdup("");
    }

    render_node(doc, out, 0);
    cmark_node_free(doc);

    result = g_string_free(out, FALSE);
    char *stripped = g_strstrip(result);
    char *copy = g_strdup(stripped);
    g_free(result);
    return copy;
}
