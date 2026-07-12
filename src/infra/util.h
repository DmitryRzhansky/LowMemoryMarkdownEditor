#ifndef LMME_INFRA_UTIL_H
#define LMME_INFRA_UTIL_H

#include <glib.h>

typedef enum {
    LMME_FILE_KIND_OTHER = 0,
    LMME_FILE_KIND_DIRECTORY,
    LMME_FILE_KIND_MARKDOWN,
    LMME_FILE_KIND_IMAGE
} LmmeFileKind;

gboolean lmme_path_has_markdown_extension(const char *path);
gboolean lmme_path_has_image_extension(const char *path);
gboolean lmme_path_is_hidden_basename(const char *name);
gboolean lmme_path_should_skip_dir(const char *name);
LmmeFileKind lmme_file_kind_from_path(const char *path, gboolean is_dir, gboolean show_images);

/* Every char * return in this header is caller-owned and may be freed with g_free(). */
char *lmme_path_join(const char *first, const char *second);
char *lmme_path_relative_to(const char *root, const char *path);
gboolean lmme_path_is_inside(const char *root, const char *path);
char *lmme_path_basename_without_extension(const char *path);

char *lmme_slugify(const char *text, gsize max_len);
char *lmme_generate_image_filename(const char *directory,
                                   const char *current_markdown_path,
                                   const char *extension,
                                   GDateTime *now);

/* name is required; error may be NULL. */
gboolean lmme_validate_basename(const char *name, GError **error);
guint lmme_word_count(const char *text);
char *lmme_hash_path(const char *path);

#endif
