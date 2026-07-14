#define _XOPEN_SOURCE 700

#include "editor/editor_view.h"

#include "infra/util.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define LMME_EDITOR_VIEW_TYPE (lmme_editor_view_get_type())
#define LMME_EDITOR_VIEW(instance) \
    (G_TYPE_CHECK_INSTANCE_CAST((instance), LMME_EDITOR_VIEW_TYPE, LmmeEditorView))
#define LMME_IS_EDITOR_VIEW(instance) \
    (G_TYPE_CHECK_INSTANCE_TYPE((instance), LMME_EDITOR_VIEW_TYPE))

#define LMME_PREVIEW_IMAGE_MAX_ITEMS 16U
#define LMME_PREVIEW_IMAGE_MAX_WIDTH 560
#define LMME_PREVIEW_IMAGE_MAX_HEIGHT 280
#define LMME_PREVIEW_IMAGE_PIXEL_BUDGET 1250000U
#define LMME_PREVIEW_IMAGE_MAX_FILE_BYTES (64U * 1024U * 1024U)
#define LMME_PREVIEW_IMAGE_MAX_SOURCE_PIXELS 100000000U

typedef struct {
    guint offset;
    int width;
    int height;
    GdkTexture *texture;
} LmmePreviewImage;

typedef struct {
    GdkTexture *texture;
    goffset file_size;
    gint64 modified_time;
} LmmeCachedPreviewImage;

typedef struct _LmmeEditorView {
    GtkSourceView parent_instance;
    GPtrArray *preview_images;
    GHashTable *preview_image_cache;
} LmmeEditorView;

typedef struct _LmmeEditorViewClass {
    GtkSourceViewClass parent_class;
} LmmeEditorViewClass;

GType lmme_editor_view_get_type(void);

G_DEFINE_TYPE(LmmeEditorView, lmme_editor_view, GTK_SOURCE_TYPE_VIEW)

static void
preview_image_free(gpointer data)
{
    LmmePreviewImage *image = data;

    if (image == NULL) {
        return;
    }
    g_clear_object(&image->texture);
    g_free(image);
}

static void
cached_preview_image_free(gpointer data)
{
    LmmeCachedPreviewImage *image = data;

    if (image == NULL) {
        return;
    }
    g_clear_object(&image->texture);
    g_free(image);
}

static void
lmme_editor_view_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
    LmmeEditorView *self = LMME_EDITOR_VIEW(widget);
    GtkTextView *text_view = GTK_TEXT_VIEW(widget);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    int char_count = gtk_text_buffer_get_char_count(buffer);
    int widget_width = gtk_widget_get_width(widget);
    int widget_height = gtk_widget_get_height(widget);

    GTK_WIDGET_CLASS(lmme_editor_view_parent_class)->snapshot(widget, snapshot);

    for (guint i = 0; i < self->preview_images->len; i++) {
        const LmmePreviewImage *image = g_ptr_array_index(self->preview_images, i);
        GtkTextIter iter;
        GdkRectangle cursor = {0};
        int x = 0;
        int y = 0;
        int available_width = 0;
        double scale = 1.0;
        double width = 0.0;
        double height = 0.0;
        graphene_point_t origin;

        if (image->offset > (guint)char_count || image->offset > (guint)G_MAXINT) {
            continue;
        }

        gtk_text_buffer_get_iter_at_offset(buffer, &iter, (int)image->offset);
        gtk_text_view_get_cursor_locations(text_view, &iter, &cursor, NULL);
        gtk_text_view_buffer_to_window_coords(text_view,
                                              GTK_TEXT_WINDOW_WIDGET,
                                              cursor.x,
                                              cursor.y + cursor.height + 6,
                                              &x,
                                              &y);

        x = MAX(x, gtk_text_view_get_left_margin(text_view));
        available_width = widget_width - x - gtk_text_view_get_right_margin(text_view);
        if (available_width <= 0) {
            continue;
        }
        if (image->width > available_width) {
            scale = (double)available_width / (double)image->width;
        }
        width = (double)image->width * scale;
        height = (double)image->height * scale;

        if ((double)y + height < 0.0 || y >= widget_height) {
            continue;
        }

        origin = GRAPHENE_POINT_INIT((float)x, (float)y);
        gtk_snapshot_save(snapshot);
        gtk_snapshot_translate(snapshot, &origin);
        gdk_paintable_snapshot(GDK_PAINTABLE(image->texture), snapshot, width, height);
        gtk_snapshot_restore(snapshot);
    }
}

static void
lmme_editor_view_finalize(GObject *object)
{
    LmmeEditorView *self = LMME_EDITOR_VIEW(object);

    g_clear_pointer(&self->preview_images, g_ptr_array_unref);
    g_clear_pointer(&self->preview_image_cache, g_hash_table_unref);
    G_OBJECT_CLASS(lmme_editor_view_parent_class)->finalize(object);
}

static void
lmme_editor_view_class_init(LmmeEditorViewClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    object_class->finalize = lmme_editor_view_finalize;
    widget_class->snapshot = lmme_editor_view_snapshot;
}

static void
lmme_editor_view_init(LmmeEditorView *self)
{
    self->preview_images = g_ptr_array_new_with_free_func(preview_image_free);
    self->preview_image_cache =
        g_hash_table_new_full(g_str_hash, g_str_equal, g_free, cached_preview_image_free);
}

GtkWidget *
lmme_editor_view_new(GtkSourceBuffer *buffer)
{
    return g_object_new(LMME_EDITOR_VIEW_TYPE, "buffer", buffer, NULL);
}

static char *
preview_image_target(const char *markdown, const LmmePreviewRange *range)
{
    const char *range_start = NULL;
    const char *range_end = NULL;
    const char *target_start = NULL;
    g_autofree char *target = NULL;
    g_autofree char *scheme = NULL;
    char *decoded = NULL;
    gsize target_length = 0;

    if (markdown == NULL || range == NULL || range->start_offset >= range->end_offset) {
        return NULL;
    }

    range_start = g_utf8_offset_to_pointer(markdown, range->start_offset);
    range_end = g_utf8_offset_to_pointer(markdown, range->end_offset);
    if (range_end <= range_start || range_end[-1] != ')') {
        return NULL;
    }

    target_start = g_strstr_len(range_start, range_end - range_start, "](");
    if (target_start == NULL) {
        return NULL;
    }
    target_start += 2;
    target_length = (gsize)((range_end - 1) - target_start);
    target = g_strndup(target_start, target_length);
    g_strstrip(target);
    target_length = strlen(target);
    if (target_length >= 2 && target[0] == '<' && target[target_length - 1] == '>') {
        target[target_length - 1] = '\0';
        memmove(target, target + 1, target_length - 1);
    }
    if (target[0] == '\0' || target[0] == '#' || g_path_is_absolute(target)) {
        return NULL;
    }

    decoded = g_uri_unescape_string(target, NULL);
    if (decoded == NULL || decoded[0] == '\0') {
        g_free(decoded);
        return NULL;
    }
    scheme = g_uri_parse_scheme(decoded);
    if (scheme != NULL) {
        g_free(decoded);
        return NULL;
    }
    return decoded;
}

static char *
resolve_preview_image_path(const char *workspace_real,
                           const char *markdown,
                           const LmmePreviewRange *range,
                           GStatBuf *out_info)
{
    g_autofree char *target = preview_image_target(markdown, range);
    g_autofree char *candidate = NULL;
    char *resolved = NULL;

    if (workspace_real == NULL || target == NULL) {
        return NULL;
    }

    candidate = g_build_filename(workspace_real, target, NULL);
    resolved = realpath(candidate, NULL);
    if (resolved == NULL || !lmme_path_is_inside(workspace_real, resolved) ||
        !lmme_path_has_image_extension(resolved) || g_stat(resolved, out_info) != 0 ||
        !S_ISREG(out_info->st_mode) || out_info->st_size < 0 ||
        (guint64)out_info->st_size > LMME_PREVIEW_IMAGE_MAX_FILE_BYTES) {
        free(resolved);
        return NULL;
    }
    return resolved;
}

static LmmeCachedPreviewImage *
load_cached_preview_image(LmmeEditorView *self, const char *path, const GStatBuf *info)
{
    LmmeCachedPreviewImage *cached = g_hash_table_lookup(self->preview_image_cache, path);
    gint64 modified_time = (gint64)info->st_mtime;
    goffset file_size = (goffset)info->st_size;

    if (cached != NULL &&
        (cached->modified_time != modified_time || cached->file_size != file_size)) {
        g_hash_table_remove(self->preview_image_cache, path);
        cached = NULL;
    }
    if (cached == NULL) {
        g_autoptr(GError) error = NULL;
        g_autoptr(GdkPixbuf) pixbuf = NULL;
        int source_width = 0;
        int source_height = 0;
        guint64 source_pixels = 0;

        if (gdk_pixbuf_get_file_info(path, &source_width, &source_height) == NULL ||
            source_width <= 0 || source_height <= 0) {
            return NULL;
        }
        source_pixels = (guint64)source_width * (guint64)source_height;
        if (source_pixels > LMME_PREVIEW_IMAGE_MAX_SOURCE_PIXELS) {
            return NULL;
        }

        pixbuf = gdk_pixbuf_new_from_file_at_scale(path,
                                                   LMME_PREVIEW_IMAGE_MAX_WIDTH,
                                                   LMME_PREVIEW_IMAGE_MAX_HEIGHT,
                                                   TRUE,
                                                   &error);

        if (pixbuf == NULL) {
            return NULL;
        }
        cached = g_new0(LmmeCachedPreviewImage, 1);
        cached->texture = gdk_texture_new_for_pixbuf(pixbuf);
        cached->file_size = file_size;
        cached->modified_time = modified_time;
        g_hash_table_insert(self->preview_image_cache, g_strdup(path), cached);
    }
    return cached;
}

static gboolean
remove_unused_cache_entry(gpointer key, gpointer value, gpointer user_data)
{
    GHashTable *used_paths = user_data;
    (void)value;

    return !g_hash_table_contains(used_paths, key);
}

static void
apply_image_display_spacing(GtkTextBuffer *buffer, const LmmePreviewRange *range)
{
    GtkTextTag *tag = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer),
                                                "lmme-preview-image-display");
    GtkTextIter start;
    GtkTextIter end;
    int char_count = gtk_text_buffer_get_char_count(buffer);

    if (tag == NULL || range->end_offset > (guint)char_count ||
        range->end_offset > (guint)G_MAXINT) {
        return;
    }
    gtk_text_buffer_get_iter_at_offset(buffer, &start, (int)range->start_offset);
    gtk_text_buffer_get_iter_at_offset(buffer, &end, (int)range->end_offset);
    gtk_text_buffer_apply_tag(buffer, tag, &start, &end);
}

void
lmme_editor_view_update_preview_images(GtkWidget *view,
                                       GtkTextBuffer *buffer,
                                       const char *markdown,
                                       const GPtrArray *ranges,
                                       const char *workspace_root)
{
    LmmeEditorView *self = NULL;
    g_autofree char *workspace_real = NULL;
    g_autoptr(GHashTable) used_paths = NULL;
    g_autoptr(GHashTable) counted_paths = NULL;
    guint64 used_pixels = 0;

    if (!LMME_IS_EDITOR_VIEW(view)) {
        return;
    }
    self = LMME_EDITOR_VIEW(view);
    g_ptr_array_set_size(self->preview_images, 0);

    if (buffer == NULL || markdown == NULL || ranges == NULL || workspace_root == NULL) {
        g_hash_table_remove_all(self->preview_image_cache);
        gtk_widget_queue_draw(view);
        return;
    }

    workspace_real = realpath(workspace_root, NULL);
    if (workspace_real == NULL) {
        g_hash_table_remove_all(self->preview_image_cache);
        gtk_widget_queue_draw(view);
        return;
    }

    used_paths = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    counted_paths = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    for (guint i = 0;
         i < ranges->len && self->preview_images->len < LMME_PREVIEW_IMAGE_MAX_ITEMS;
         i++) {
        const LmmePreviewRange *range = g_ptr_array_index(ranges, i);
        GStatBuf info = {0};
        g_autofree char *path = NULL;
        LmmeCachedPreviewImage *cached = NULL;
        guint64 pixels = 0;
        LmmePreviewImage *image = NULL;

        if (range->kind != LMME_PREVIEW_RANGE_IMAGE) {
            continue;
        }
        path = resolve_preview_image_path(workspace_real, markdown, range, &info);
        if (path == NULL) {
            continue;
        }
        cached = load_cached_preview_image(self, path, &info);
        if (cached == NULL) {
            continue;
        }

        if (!g_hash_table_contains(counted_paths, path)) {
            pixels = (guint64)gdk_texture_get_width(cached->texture) *
                     (guint64)gdk_texture_get_height(cached->texture);
            if (used_pixels + pixels > LMME_PREVIEW_IMAGE_PIXEL_BUDGET) {
                continue;
            }
            used_pixels += pixels;
            g_hash_table_add(counted_paths, g_strdup(path));
            g_hash_table_add(used_paths, g_strdup(path));
        }

        image = g_new0(LmmePreviewImage, 1);
        image->offset = range->start_offset;
        image->width = gdk_texture_get_width(cached->texture);
        image->height = gdk_texture_get_height(cached->texture);
        image->texture = g_object_ref(cached->texture);
        g_ptr_array_add(self->preview_images, image);
        apply_image_display_spacing(buffer, range);
    }

    g_hash_table_foreach_remove(self->preview_image_cache,
                                remove_unused_cache_entry,
                                used_paths);
    gtk_widget_queue_draw(view);
}

void
lmme_editor_view_clear_preview_images(GtkWidget *view, gboolean discard_cache)
{
    LmmeEditorView *self = NULL;

    if (!LMME_IS_EDITOR_VIEW(view)) {
        return;
    }
    self = LMME_EDITOR_VIEW(view);
    g_ptr_array_set_size(self->preview_images, 0);
    if (discard_cache) {
        g_hash_table_remove_all(self->preview_image_cache);
    }
    gtk_widget_queue_draw(view);
}

guint
lmme_editor_view_preview_image_count(GtkWidget *view)
{
    if (!LMME_IS_EDITOR_VIEW(view)) {
        return 0;
    }
    return LMME_EDITOR_VIEW(view)->preview_images->len;
}
