#define _GNU_SOURCE

#include "features/image_insert.h"
#include "features/image_insert_test.h"

#include "app/app.h"
#include "document/tabs.h"
#include "editor/editor.h"
#include "infra/dialogs.h"
#include "infra/util.h"
#include "ui/window.h"
#include "workspace/workspace.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#include <glib/gstdio.h>

typedef struct {
    LmmeApp *app;
    guint64 document_id;
    char *workspace_path;
    char *destination_path;
    GCancellable *cancellable;
    LmmeImageInsertState state;
    gboolean destination_created_by_request;
} LmmeImageInsertRequest;

typedef struct {
    char *destination_path;
    guint8 *pixels;
    gsize byte_count;
    gsize stride;
    int width;
    int height;
} LmmePngSave;

static guint8
unpremultiply_channel(guint8 channel, guint8 alpha)
{
    if (alpha == 0) {
        return 0;
    }
    if (alpha == 255) {
        return channel;
    }
    return (guint8)MIN(255U,
                       ((guint)channel * 255U + (guint)alpha / 2U) / (guint)alpha);
}

static guint8 *
texture_download_argb(GdkTexture *texture,
                      gsize *out_byte_count,
                      gsize *out_stride)
{
    int width = gdk_texture_get_width(texture);
    int height = gdk_texture_get_height(texture);
    gsize stride = 0;
    gsize byte_count = 0;
    guint8 *pixels = NULL;

    if (width <= 0 || height <= 0 || (gsize)width > G_MAXSIZE / 4U) {
        return NULL;
    }
    stride = (gsize)width * 4U;
    if ((gsize)height > G_MAXSIZE / stride) {
        return NULL;
    }
    byte_count = stride * (gsize)height;
    pixels = g_malloc(byte_count);

    gdk_texture_download(texture, pixels, stride);
    *out_byte_count = byte_count;
    *out_stride = stride;
    return pixels;
}

static void
convert_argb_to_rgba(guint8 *pixels, gsize byte_count)
{
    for (gsize offset = 0; offset < byte_count; offset += 4U) {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
        guint8 blue = pixels[offset];
        guint8 green = pixels[offset + 1U];
        guint8 red = pixels[offset + 2U];
        guint8 alpha = pixels[offset + 3U];
#else
        guint8 alpha = pixels[offset];
        guint8 red = pixels[offset + 1U];
        guint8 green = pixels[offset + 2U];
        guint8 blue = pixels[offset + 3U];
#endif

        if (alpha == 0) {
            pixels[offset] = 0;
            pixels[offset + 1U] = 0;
            pixels[offset + 2U] = 0;
        } else if (alpha == 255) {
            pixels[offset] = red;
            pixels[offset + 1U] = green;
            pixels[offset + 2U] = blue;
        } else {
            pixels[offset] = unpremultiply_channel(red, alpha);
            pixels[offset + 1U] = unpremultiply_channel(green, alpha);
            pixels[offset + 2U] = unpremultiply_channel(blue, alpha);
        }
        pixels[offset + 3U] = alpha;
    }
}

static void
image_insert_request_free(LmmeImageInsertRequest *request)
{
    if (request == NULL) {
        return;
    }
    g_clear_object(&request->cancellable);
    g_free(request->workspace_path);
    g_free(request->destination_path);
    g_free(request);
}

gboolean
lmme_image_insert_should_rollback_destination(LmmeImageInsertState state,
                                              gboolean destination_created_by_request)
{
    return state == LMME_IMAGE_INSERT_FILE_CREATED && destination_created_by_request;
}

void
lmme_image_insert_request_mark_file_created(LmmeImageInsertState *state,
                                            gboolean *destination_created_by_request,
                                            gboolean destination_existed_before)
{
    g_return_if_fail(state != NULL);
    g_return_if_fail(destination_created_by_request != NULL);

    *state = LMME_IMAGE_INSERT_FILE_CREATED;
    *destination_created_by_request = !destination_existed_before;
}

void
lmme_image_insert_request_mark_committed(LmmeImageInsertState *state)
{
    g_return_if_fail(state != NULL);
    *state = LMME_IMAGE_INSERT_COMMITTED;
}

void
lmme_image_insert_rollback_destination_if_needed(LmmeImageInsertState *state,
                                                 gboolean *destination_created_by_request,
                                                 const char *destination_path)
{
    g_return_if_fail(state != NULL);
    g_return_if_fail(destination_created_by_request != NULL);

    if (!lmme_image_insert_should_rollback_destination(*state, *destination_created_by_request)) {
        return;
    }
    if (destination_path != NULL) {
        g_unlink(destination_path);
    }
    *destination_created_by_request = FALSE;
    *state = LMME_IMAGE_INSERT_FINISHED;
}

static void
png_save_free(LmmePngSave *save)
{
    if (save == NULL) {
        return;
    }
    g_free(save->destination_path);
    g_free(save->pixels);
    g_free(save);
}

static void
png_save_thread(GTask *task,
                gpointer source_object,
                gpointer task_data,
                GCancellable *cancellable)
{
    LmmePngSave *save = task_data;
    g_autoptr(GdkPixbuf) pixbuf = NULL;
    g_autoptr(GFile) destination = NULL;
    g_autoptr(GFileOutputStream) stream = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(GError) close_error = NULL;
    gboolean created = FALSE;
    gboolean saved = FALSE;

    (void)source_object;
    if (g_task_return_error_if_cancelled(task)) {
        return;
    }

    convert_argb_to_rgba(save->pixels, save->byte_count);
    pixbuf = gdk_pixbuf_new_from_data(save->pixels,
                                     GDK_COLORSPACE_RGB,
                                     TRUE,
                                     8,
                                     save->width,
                                     save->height,
                                     (int)save->stride,
                                     NULL,
                                     NULL);
    if (pixbuf == NULL) {
        g_task_return_new_error(task,
                                G_IO_ERROR,
                                G_IO_ERROR_FAILED,
                                "Could not prepare clipboard image pixels.");
        return;
    }

    destination = g_file_new_for_path(save->destination_path);
    stream = g_file_create(destination,
                           G_FILE_CREATE_NONE,
                           cancellable,
                           &error);
    if (stream == NULL) {
        g_task_return_error(task, g_steal_pointer(&error));
        return;
    }
    created = TRUE;
    saved = gdk_pixbuf_save_to_streamv(pixbuf,
                                       G_OUTPUT_STREAM(stream),
                                       "png",
                                       NULL,
                                       NULL,
                                       cancellable,
                                       &error);
    if (!g_output_stream_close(G_OUTPUT_STREAM(stream), NULL, &close_error)) {
        saved = FALSE;
        if (error == NULL) {
            error = g_steal_pointer(&close_error);
        }
    }
    if (saved && g_cancellable_is_cancelled(cancellable)) {
        saved = FALSE;
        g_clear_error(&error);
        g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_CANCELLED, "Operation was cancelled");
    }
    if (!saved) {
        if (created) {
            (void)g_file_delete(destination, NULL, NULL);
        }
        if (error == NULL) {
            g_set_error_literal(&error,
                                G_IO_ERROR,
                                G_IO_ERROR_FAILED,
                                "Could not encode clipboard image.");
        }
        g_task_return_error(task, g_steal_pointer(&error));
        return;
    }
    g_task_return_boolean(task, TRUE);
}

void
lmme_image_texture_save_png_async(GdkTexture *texture,
                                  const char *destination_path,
                                  GCancellable *cancellable,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    GTask *task = NULL;
    LmmePngSave *save = NULL;

    g_return_if_fail(GDK_IS_TEXTURE(texture));
    g_return_if_fail(destination_path != NULL);

    task = g_task_new(texture, cancellable, callback, user_data);
    g_task_set_check_cancellable(task, FALSE);
    save = g_new0(LmmePngSave, 1);
    save->destination_path = g_strdup(destination_path);
    save->width = gdk_texture_get_width(texture);
    save->height = gdk_texture_get_height(texture);
    g_task_set_task_data(task, save, (GDestroyNotify)png_save_free);

    save->pixels = texture_download_argb(texture,
                                         &save->byte_count,
                                         &save->stride);
    if (save->pixels == NULL || save->stride > G_MAXINT) {
        g_task_return_new_error(task,
                                G_IO_ERROR,
                                G_IO_ERROR_FAILED,
                                "Could not read clipboard image pixels.");
        g_object_unref(task);
        return;
    }
    g_task_run_in_thread(task, png_save_thread);
    g_object_unref(task);
}

gboolean
lmme_image_texture_save_png_finish(GdkTexture *texture,
                                   GAsyncResult *result,
                                   GError **error)
{
    g_return_val_if_fail(g_task_is_valid(result, texture), FALSE);
    return g_task_propagate_boolean(G_TASK(result), error);
}

static const char *
image_extension_for_path(const char *path)
{
    const char *dot = path != NULL ? strrchr(path, '.') : NULL;
    return dot != NULL ? dot + 1 : "png";
}

static gboolean
ensure_img_dir(LmmeApp *app, char **out_dir, GError **error)
{
    g_autofree char *dir = NULL;
    struct stat info;

    if (app->workspace == NULL) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Open a workspace first.");
        return FALSE;
    }

    dir = g_build_filename(app->workspace->path, "img", NULL);
    if (!lmme_workspace_validate_target_parent(app->workspace, dir, error)) {
        return FALSE;
    }
    if (g_lstat(dir, &info) == 0) {
        if (!S_ISDIR(info.st_mode) || S_ISLNK(info.st_mode)) {
            g_set_error_literal(error,
                                G_FILE_ERROR,
                                G_FILE_ERROR_PERM,
                                "The workspace img path is not a safe directory.");
            return FALSE;
        }
    } else if (errno != ENOENT) {
        g_set_error(error,
                    G_FILE_ERROR,
                    (gint)g_file_error_from_errno(errno),
                    "Could not inspect img folder.");
        return FALSE;
    } else if (g_mkdir(dir, 0700) != 0) {
        g_set_error(error,
                    G_FILE_ERROR,
                    (gint)g_file_error_from_errno(errno),
                    "Could not create img folder.");
        return FALSE;
    }

    *out_dir = g_steal_pointer(&dir);
    return TRUE;
}

char *
lmme_image_markdown_link(const char *workspace_path, const char *destination_path)
{
    g_autofree char *relative = NULL;

    if (workspace_path == NULL || destination_path == NULL) {
        return NULL;
    }
    relative = lmme_path_relative_to(workspace_path, destination_path);
    return g_strdup_printf("![](%s)", relative);
}

static gboolean
insert_link_for_dest(LmmeDocument *doc, const char *dest_path)
{
    g_autofree char *link = NULL;

    if (doc == NULL || doc->app == NULL || doc->app->workspace == NULL ||
        lmme_tabs_find_by_id(doc->app, doc->id) != doc) {
        return FALSE;
    }
    link = lmme_image_markdown_link(doc->app->workspace->path, dest_path);
    if (link == NULL) {
        return FALSE;
    }
    lmme_editor_insert_text_at_cursor(GTK_TEXT_BUFFER(doc->buffer), link);
    lmme_window_refresh_tree_directory(doc->app, doc->app->workspace->path);
    return TRUE;
}

static gboolean
image_insert_document_is_valid(LmmeDocument *doc, GtkWindow *parent)
{
    LmmeApp *app = doc != NULL ? doc->app : NULL;

    if (doc == NULL || app == NULL || app->workspace == NULL ||
        !lmme_path_has_markdown_extension(doc->path) ||
        !lmme_path_is_inside(app->workspace->path, doc->path)) {
        lmme_dialog_error(parent, "Could not insert image.", "Open a Markdown file in a workspace first.");
        return FALSE;
    }
    return TRUE;
}

static gboolean
image_insert_request_prepare_destination(LmmeImageInsertRequest *request,
                                         LmmeDocument *doc,
                                         const char *extension,
                                         GError **error)
{
    LmmeApp *app = doc->app;
    g_autofree char *img_dir = NULL;
    g_autofree char *filename = NULL;
    g_autoptr(GDateTime) now = NULL;

    g_return_val_if_fail(request != NULL, FALSE);
    g_return_val_if_fail(doc != NULL, FALSE);
    g_return_val_if_fail(extension != NULL, FALSE);

    if (!ensure_img_dir(app, &img_dir, error)) {
        return FALSE;
    }

    now = g_date_time_new_now_local();
    filename = lmme_generate_image_filename(img_dir, doc->path, extension, now);
    g_free(request->destination_path);
    request->destination_path = g_build_filename(img_dir, filename, NULL);
    return TRUE;
}

static gboolean
image_insert_request_commit_link(LmmeImageInsertRequest *request, LmmeDocument *doc)
{
    g_return_val_if_fail(request != NULL, FALSE);

    if (!insert_link_for_dest(doc, request->destination_path)) {
        return FALSE;
    }
    lmme_image_insert_request_mark_committed(&request->state);
    return TRUE;
}

static gboolean
image_insert_request_is_current(LmmeDocument *doc, const LmmeImageInsertRequest *request)
{
    return doc != NULL &&
           request != NULL &&
           request->cancellable != NULL &&
           doc->image_insert_cancellable == request->cancellable &&
           !g_cancellable_is_cancelled(request->cancellable);
}

static void
image_insert_request_clear_document_cancellable(LmmeDocument *doc,
                                                const LmmeImageInsertRequest *request)
{
    if (doc != NULL && request != NULL && request->cancellable != NULL &&
        doc->image_insert_cancellable == request->cancellable) {
        g_clear_object(&doc->image_insert_cancellable);
    }
}

static LmmeImageInsertRequest *
image_insert_request_begin_async(LmmeDocument *doc)
{
    LmmeImageInsertRequest *request = NULL;

    g_return_val_if_fail(doc != NULL, NULL);

    if (doc->image_insert_cancellable != NULL) {
        g_cancellable_cancel(doc->image_insert_cancellable);
        g_clear_object(&doc->image_insert_cancellable);
    }
    doc->image_insert_cancellable = g_cancellable_new();

    request = g_new0(LmmeImageInsertRequest, 1);
    request->app = doc->app;
    request->document_id = doc->id;
    request->workspace_path = g_strdup(doc->app->workspace->path);
    request->cancellable = g_object_ref(doc->image_insert_cancellable);
    request->state = LMME_IMAGE_INSERT_PREPARING;
    return request;
}

static gboolean
image_insert_copy_source_for_request(LmmeDocument *doc,
                                     LmmeImageInsertRequest *request,
                                     const char *source_path,
                                     GtkWindow *parent,
                                     GError **error)
{
    g_autoptr(GFile) source = NULL;
    g_autoptr(GFile) dest = NULL;
    gboolean destination_existed = FALSE;

    g_return_val_if_fail(doc != NULL, FALSE);
    g_return_val_if_fail(request != NULL, FALSE);
    g_return_val_if_fail(source_path != NULL, FALSE);

    if (!image_insert_document_is_valid(doc, parent)) {
        return FALSE;
    }
    if (!lmme_path_has_image_extension(source_path)) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Unsupported image format.");
        return FALSE;
    }
    if (!image_insert_request_prepare_destination(request,
                                                  doc,
                                                  image_extension_for_path(source_path),
                                                  error)) {
        return FALSE;
    }

    destination_existed = g_file_test(request->destination_path, G_FILE_TEST_EXISTS);
    source = g_file_new_for_path(source_path);
    dest = g_file_new_for_path(request->destination_path);
    if (!g_file_copy(source, dest, G_FILE_COPY_NONE, NULL, NULL, NULL, error)) {
        return FALSE;
    }

    lmme_image_insert_request_mark_file_created(&request->state,
                                                &request->destination_created_by_request,
                                                destination_existed);
    return TRUE;
}

static gboolean
image_insert_finish_copy_request(LmmeDocument *doc,
                               LmmeImageInsertRequest *request,
                               GtkWindow *parent)
{
    g_return_val_if_fail(doc != NULL, FALSE);
    g_return_val_if_fail(request != NULL, FALSE);

    if (!image_insert_request_commit_link(request, doc)) {
        lmme_image_insert_rollback_destination_if_needed(&request->state,
                                                         &request->destination_created_by_request,
                                                         request->destination_path);
        return FALSE;
    }
    request->state = LMME_IMAGE_INSERT_FINISHED;
    (void)parent;
    return TRUE;
}

gboolean
lmme_image_insert_for_document(LmmeDocument *doc, const char *source_path)
{
    LmmeApp *app = doc != NULL ? doc->app : NULL;
    GtkWindow *parent = app != NULL && app->window != NULL ? GTK_WINDOW(app->window) : NULL;
    g_autoptr(GError) error = NULL;
    LmmeImageInsertRequest request = {
        .app = app,
        .state = LMME_IMAGE_INSERT_PREPARING,
    };
    gboolean success = FALSE;

    if (doc == NULL) {
        lmme_dialog_error(parent, "Could not insert image.", "Open a Markdown file in a workspace first.");
        return FALSE;
    }

    request.document_id = doc->id;
    request.workspace_path = g_strdup(app->workspace != NULL ? app->workspace->path : NULL);

    if (!image_insert_copy_source_for_request(doc, &request, source_path, parent, &error)) {
        if (error != NULL && g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_INVAL)) {
            lmme_dialog_error(parent, "Unsupported image format.", NULL);
        } else if (error != NULL && g_strstr_len(error->message, -1, "img folder") != NULL) {
            lmme_dialog_error(parent, "Could not create img folder.", error->message);
        } else if (error != NULL) {
            lmme_dialog_error(parent, "Could not copy image.", error->message);
        }
        lmme_image_insert_rollback_destination_if_needed(&request.state,
                                                         &request.destination_created_by_request,
                                                         request.destination_path);
        g_free(request.workspace_path);
        g_free(request.destination_path);
        return FALSE;
    }

    success = image_insert_finish_copy_request(doc, &request, parent);
    g_free(request.workspace_path);
    g_free(request.destination_path);
    return success;
}

gboolean
lmme_image_insert_from_file(LmmeApp *app, const char *source_path)
{
    return lmme_image_insert_for_document(lmme_tabs_get_active(app), source_path);
}

void
lmme_image_insert_from_dialog(LmmeApp *app)
{
    g_autofree char *path = lmme_dialog_open_image(GTK_WINDOW(app->window));
    if (path != NULL) {
        lmme_image_insert_from_file(app, path);
    }
}

static void
on_texture_saved(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    LmmeImageInsertRequest *request = user_data;
    LmmeDocument *doc = lmme_tabs_find_by_id(request->app, request->document_id);
    g_autoptr(GError) error = NULL;
    gboolean saved = lmme_image_texture_save_png_finish(GDK_TEXTURE(source_object),
                                                        result,
                                                        &error);
    gboolean is_current = image_insert_request_is_current(doc, request);

    if (saved) {
        lmme_image_insert_request_mark_file_created(&request->state,
                                                    &request->destination_created_by_request,
                                                    FALSE);
    }

    if (saved && is_current) {
        if (!image_insert_request_commit_link(request, doc)) {
            lmme_image_insert_rollback_destination_if_needed(&request->state,
                                                               &request->destination_created_by_request,
                                                               request->destination_path);
        } else {
            request->state = LMME_IMAGE_INSERT_FINISHED;
        }
    } else if (saved) {
        lmme_image_insert_rollback_destination_if_needed(&request->state,
                                                         &request->destination_created_by_request,
                                                         request->destination_path);
    } else if (error == NULL || !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        lmme_dialog_error(GTK_WINDOW(request->app->window),
                          "Could not save image.",
                          error != NULL ? error->message : NULL);
    }

    image_insert_request_clear_document_cancellable(doc, request);
    image_insert_request_free(request);
}

static gboolean
save_texture_to_img_async(LmmeDocument *doc,
                          GdkTexture *texture,
                          LmmeImageInsertRequest *request)
{
    LmmeApp *app = doc->app;
    g_autoptr(GError) error = NULL;

    if (!image_insert_document_is_valid(doc, GTK_WINDOW(app->window))) {
        return FALSE;
    }
    if (!image_insert_request_prepare_destination(request, doc, "png", &error)) {
        lmme_dialog_error(GTK_WINDOW(app->window),
                          "Could not create img folder.",
                          error != NULL ? error->message : NULL);
        return FALSE;
    }

    lmme_image_texture_save_png_async(texture,
                                      request->destination_path,
                                      request->cancellable,
                                      on_texture_saved,
                                      request);
    return TRUE;
}

static void
on_clipboard_texture_ready(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    LmmeImageInsertRequest *request = user_data;
    GdkClipboard *clipboard = GDK_CLIPBOARD(source_object);
    g_autoptr(GError) error = NULL;
    GdkTexture *texture = gdk_clipboard_read_texture_finish(clipboard, result, &error);
    LmmeDocument *doc = lmme_tabs_find_by_id(request->app, request->document_id);

    if (texture == NULL) {
        if (error == NULL || !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            lmme_dialog_error(GTK_WINDOW(request->app->window), "Clipboard does not contain an image.", NULL);
        }
        image_insert_request_clear_document_cancellable(doc, request);
        image_insert_request_free(request);
        return;
    }

    if (doc != NULL && image_insert_request_is_current(doc, request)) {
        if (save_texture_to_img_async(doc, texture, request)) {
            request = NULL;
        } else {
            image_insert_request_clear_document_cancellable(doc, request);
        }
    }
    g_object_unref(texture);
    if (request != NULL) {
        image_insert_request_clear_document_cancellable(doc, request);
        image_insert_request_free(request);
    }
}

static gboolean
on_editor_key_pressed(GtkEventControllerKey *controller,
                      guint keyval,
                      guint keycode,
                      GdkModifierType state,
                      gpointer user_data)
{
    LmmeDocument *doc = user_data;
    (void)controller;
    (void)keycode;

    if (keyval != GDK_KEY_v || (state & GDK_CONTROL_MASK) == 0) {
        return FALSE;
    }

    GdkClipboard *clipboard = gtk_widget_get_clipboard(doc->source_view);
    GdkContentFormats *formats = gdk_clipboard_get_formats(clipboard);
    LmmeImageInsertRequest *request = NULL;

    if (!gdk_content_formats_contain_gtype(formats, GDK_TYPE_TEXTURE)) {
        return FALSE;
    }

    request = image_insert_request_begin_async(doc);
    if (request == NULL) {
        return FALSE;
    }

    gdk_clipboard_read_texture_async(clipboard,
                                     doc->image_insert_cancellable,
                                     on_clipboard_texture_ready,
                                     request);
    return TRUE;
}

static gboolean
on_drop(GtkDropTarget *target,
        const GValue *value,
        double x,
        double y,
        gpointer user_data)
{
    LmmeDocument *doc = user_data;
    (void)target;
    (void)x;
    (void)y;

    if (!G_VALUE_HOLDS(value, G_TYPE_FILE)) {
        return FALSE;
    }

    GFile *file = g_value_get_object(value);
    g_autofree char *path = g_file_get_path(file);
    if (path == NULL) {
        return FALSE;
    }

    return lmme_image_insert_for_document(doc, path);
}

void
lmme_image_insert_setup_for_document(LmmeDocument *doc)
{
    GtkEventController *key = gtk_event_controller_key_new();
    GtkDropTarget *drop = gtk_drop_target_new(G_TYPE_FILE, GDK_ACTION_COPY);

    g_signal_connect(key, "key-pressed", G_CALLBACK(on_editor_key_pressed), doc);
    gtk_widget_add_controller(doc->source_view, key);

    g_signal_connect(drop, "drop", G_CALLBACK(on_drop), doc);
    gtk_widget_add_controller(doc->source_view, GTK_EVENT_CONTROLLER(drop));
}
