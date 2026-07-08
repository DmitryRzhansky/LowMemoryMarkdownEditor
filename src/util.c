#include "util.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#include <glib/gstdio.h>

static gboolean
lmme_ascii_equal_ci(const char *a, const char *b)
{
    return g_ascii_strcasecmp(a, b) == 0;
}

gboolean
lmme_path_has_markdown_extension(const char *path)
{
    const char *dot = path != NULL ? strrchr(path, '.') : NULL;
    return dot != NULL && (lmme_ascii_equal_ci(dot, ".md") || lmme_ascii_equal_ci(dot, ".markdown"));
}

gboolean
lmme_path_has_image_extension(const char *path)
{
    const char *dot = path != NULL ? strrchr(path, '.') : NULL;
    return dot != NULL &&
           (lmme_ascii_equal_ci(dot, ".png") ||
            lmme_ascii_equal_ci(dot, ".jpg") ||
            lmme_ascii_equal_ci(dot, ".jpeg") ||
            lmme_ascii_equal_ci(dot, ".webp") ||
            lmme_ascii_equal_ci(dot, ".gif"));
}

gboolean
lmme_path_is_hidden_basename(const char *name)
{
    return name != NULL && name[0] == '.' && name[1] != '\0';
}

gboolean
lmme_path_should_skip_dir(const char *name)
{
    if (name == NULL) {
        return TRUE;
    }

    return g_strcmp0(name, ".git") == 0 ||
           g_strcmp0(name, "node_modules") == 0 ||
           g_strcmp0(name, "build") == 0 ||
           g_strcmp0(name, ".cache") == 0;
}

LmmeFileKind
lmme_file_kind_from_path(const char *path, gboolean is_dir, gboolean show_images)
{
    if (is_dir) {
        return LMME_FILE_KIND_DIRECTORY;
    }
    if (lmme_path_has_markdown_extension(path)) {
        return LMME_FILE_KIND_MARKDOWN;
    }
    if (show_images && lmme_path_has_image_extension(path)) {
        return LMME_FILE_KIND_IMAGE;
    }
    return LMME_FILE_KIND_OTHER;
}

char *
lmme_path_join(const char *first, const char *second)
{
    return g_build_filename(first, second, NULL);
}

char *
lmme_path_relative_to(const char *root, const char *path)
{
    g_autofree char *root_canon = g_canonicalize_filename(root, NULL);
    g_autofree char *path_canon = g_canonicalize_filename(path, NULL);
    const gsize root_len = strlen(root_canon);

    if (g_strcmp0(root_canon, path_canon) == 0) {
        return g_strdup(".");
    }

    if (g_str_has_prefix(path_canon, root_canon) && path_canon[root_len] == G_DIR_SEPARATOR) {
        return g_strdup(path_canon + root_len + 1);
    }

    return g_path_get_basename(path);
}

gboolean
lmme_path_is_inside(const char *root, const char *path)
{
    g_autofree char *root_canon = g_canonicalize_filename(root, NULL);
    g_autofree char *path_canon = g_canonicalize_filename(path, NULL);
    const gsize root_len = strlen(root_canon);

    return g_strcmp0(root_canon, path_canon) == 0 ||
           (g_str_has_prefix(path_canon, root_canon) && path_canon[root_len] == G_DIR_SEPARATOR);
}

char *
lmme_path_basename_without_extension(const char *path)
{
    g_autofree char *base = g_path_get_basename(path != NULL ? path : "note");
    char *dot = strrchr(base, '.');

    if (dot != NULL) {
        *dot = '\0';
    }

    return g_strdup(base[0] != '\0' ? base : "note");
}

char *
lmme_slugify(const char *text, gsize max_len)
{
    GString *out = g_string_new(NULL);
    gboolean previous_dash = FALSE;
    const char *p = text != NULL ? text : "note";

    while (*p != '\0' && out->len < max_len) {
        gunichar ch = g_utf8_get_char_validated(p, -1);

        if (ch == (gunichar)-1 || ch == (gunichar)-2) {
            ch = '-';
            p++;
        } else {
            p = g_utf8_next_char(p);
        }

        if (ch >= 'A' && ch <= 'Z') {
            ch = (gunichar)g_ascii_tolower((gchar)ch);
        }

        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
            g_string_append_c(out, (gchar)ch);
            previous_dash = FALSE;
        } else if (!previous_dash && out->len > 0) {
            g_string_append_c(out, '-');
            previous_dash = TRUE;
        }
    }

    while (out->len > 0 && out->str[out->len - 1] == '-') {
        g_string_truncate(out, out->len - 1);
    }

    if (out->len == 0) {
        g_string_append(out, "note");
    }

    return g_string_free(out, FALSE);
}

char *
lmme_generate_image_filename(const char *directory,
                             const char *current_markdown_path,
                             const char *extension,
                             GDateTime *now)
{
    g_autofree char *stamp = g_date_time_format(now, "%Y-%m-%d-%H%M%S");
    g_autofree char *basename = lmme_path_basename_without_extension(current_markdown_path);
    g_autofree char *slug = lmme_slugify(basename, 48);
    const char *ext = extension != NULL && extension[0] == '.' ? extension + 1 : extension;

    if (ext == NULL || ext[0] == '\0') {
        ext = "png";
    }

    for (guint i = 1; i < 1000; i++) {
        g_autofree char *filename = g_strdup_printf("%s-%s-%03u.%s", stamp, slug, i, ext);
        g_autofree char *candidate = g_build_filename(directory, filename, NULL);
        if (!g_file_test(candidate, G_FILE_TEST_EXISTS)) {
            return g_strdup(filename);
        }
    }

    return g_strdup_printf("%s-%s-%u.%s", stamp, slug, g_random_int(), ext);
}

gboolean
lmme_validate_basename(const char *name, GError **error)
{
    g_autofree char *copy = g_strdup(name != NULL ? name : "");

    if (g_strstrip(copy)[0] == '\0') {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Name must not be empty.");
        return FALSE;
    }

    if (strchr(name, G_DIR_SEPARATOR) != NULL || strchr(name, '/') != NULL) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Name must not contain a slash.");
        return FALSE;
    }

    return TRUE;
}

guint
lmme_word_count(const char *text)
{
    guint count = 0;
    gboolean in_word = FALSE;
    const char *p = text != NULL ? text : "";

    while (*p != '\0') {
        gunichar ch = g_utf8_get_char_validated(p, -1);
        if (ch == (gunichar)-1 || ch == (gunichar)-2) {
            ch = ' ';
            p++;
        } else {
            p = g_utf8_next_char(p);
        }

        if (g_unichar_isspace(ch)) {
            in_word = FALSE;
        } else if (!in_word) {
            count++;
            in_word = TRUE;
        }
    }

    return count;
}

char *
lmme_hash_path(const char *path)
{
    return g_compute_checksum_for_string(G_CHECKSUM_SHA256, path != NULL ? path : "", -1);
}
