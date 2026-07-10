#define _GNU_SOURCE

#include "features/image_insert.h"

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
    GCancellable *cancellable;
} LmmeImageInsertRequest;

static void
image_insert_request_free(LmmeImageInsertRequest *request)
{
    if (request == NULL) {
        return;
    }
    g_clear_object(&request->cancellable);
    g_free(request);
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
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not inspect img folder.");
        return FALSE;
    } else if (g_mkdir(dir, 0700) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not create img folder.");
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

gboolean
lmme_image_insert_for_document(LmmeDocument *doc, const char *source_path)
{
    LmmeApp *app = doc != NULL ? doc->app : NULL;
    GtkWindow *parent = app != NULL && app->window != NULL ? GTK_WINDOW(app->window) : NULL;
    g_autofree char *img_dir = NULL;
    g_autofree char *filename = NULL;
    g_autofree char *dest_path = NULL;
    g_autoptr(GDateTime) now = NULL;
    g_autoptr(GFile) source = NULL;
    g_autoptr(GFile) dest = NULL;
    g_autoptr(GError) error = NULL;

    if (doc == NULL || app == NULL ||
        app->workspace == NULL ||
        !lmme_path_has_markdown_extension(doc->path) ||
        !lmme_path_is_inside(app->workspace->path, doc->path)) {
        lmme_dialog_error(parent, "Could not insert image.", "Open a Markdown file in a workspace first.");
        return FALSE;
    }
    if (!lmme_path_has_image_extension(source_path)) {
        lmme_dialog_error(GTK_WINDOW(app->window), "Unsupported image format.", NULL);
        return FALSE;
    }
    if (!ensure_img_dir(app, &img_dir, &error)) {
        lmme_dialog_error(GTK_WINDOW(app->window), "Could not create img folder.", error != NULL ? error->message : NULL);
        return FALSE;
    }

    now = g_date_time_new_now_local();
    filename = lmme_generate_image_filename(img_dir, doc->path, image_extension_for_path(source_path), now);
    dest_path = g_build_filename(img_dir, filename, NULL);
    source = g_file_new_for_path(source_path);
    dest = g_file_new_for_path(dest_path);

    if (!g_file_copy(source, dest, G_FILE_COPY_NONE, NULL, NULL, NULL, &error)) {
        lmme_dialog_error(GTK_WINDOW(app->window), "Could not copy image.", error != NULL ? error->message : NULL);
        return FALSE;
    }

    if (!insert_link_for_dest(doc, dest_path)) {
        g_file_delete(dest, NULL, NULL);
        return FALSE;
    }
    return TRUE;
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

static gboolean
save_texture_to_img(LmmeDocument *doc, GdkTexture *texture)
{
    LmmeApp *app = doc->app;
    g_autofree char *img_dir = NULL;
    g_autofree char *filename = NULL;
    g_autofree char *dest_path = NULL;
    g_autoptr(GDateTime) now = NULL;
    g_autoptr(GError) error = NULL;

    if (app->workspace == NULL ||
        !lmme_path_has_markdown_extension(doc->path) ||
        !lmme_path_is_inside(app->workspace->path, doc->path)) {
        lmme_dialog_error(GTK_WINDOW(app->window), "Could not insert image.", "Open a Markdown file in a workspace first.");
        return FALSE;
    }

    if (!ensure_img_dir(app, &img_dir, &error)) {
        lmme_dialog_error(GTK_WINDOW(app->window), "Could not create img folder.", error != NULL ? error->message : NULL);
        return FALSE;
    }

    now = g_date_time_new_now_local();
    filename = lmme_generate_image_filename(img_dir, doc->path, "png", now);
    dest_path = g_build_filename(img_dir, filename, NULL);

    if (!gdk_texture_save_to_png(texture, dest_path)) {
        lmme_dialog_error(GTK_WINDOW(app->window), "Could not save image.", NULL);
        return FALSE;
    }

    if (!insert_link_for_dest(doc, dest_path)) {
        g_unlink(dest_path);
        return FALSE;
    }
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
        image_insert_request_free(request);
        return;
    }

    if (doc != NULL && doc->clipboard_cancellable == request->cancellable &&
        !g_cancellable_is_cancelled(request->cancellable)) {
        g_clear_object(&doc->clipboard_cancellable);
        save_texture_to_img(doc, texture);
    }
    g_object_unref(texture);
    image_insert_request_free(request);
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

    if (doc->clipboard_cancellable != NULL) {
        g_cancellable_cancel(doc->clipboard_cancellable);
        g_clear_object(&doc->clipboard_cancellable);
    }
    doc->clipboard_cancellable = g_cancellable_new();
    request = g_new0(LmmeImageInsertRequest, 1);
    request->app = doc->app;
    request->document_id = doc->id;
    request->cancellable = g_object_ref(doc->clipboard_cancellable);
    gdk_clipboard_read_texture_async(clipboard,
                                     doc->clipboard_cancellable,
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
