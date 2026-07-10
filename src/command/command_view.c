#include "command/command_handlers.h"

#include <gtk/gtk.h>

#include "app/app.h"
#include "editor/editor.h"
#include "infra/dialogs.h"
#include "ui/window.h"

static int
clamp_editor_font_size(int font_size)
{
    if (font_size < LMME_EDITOR_FONT_SIZE_MIN) {
        return LMME_EDITOR_FONT_SIZE_MIN;
    }
    if (font_size > LMME_EDITOR_FONT_SIZE_MAX) {
        return LMME_EDITOR_FONT_SIZE_MAX;
    }
    return font_size;
}

static void
set_editor_font_size(LmmeApp *app, int font_size)
{
    font_size = clamp_editor_font_size(font_size);
    if (app->config.font_size == font_size) {
        return;
    }
    app->config.font_size = font_size;
    lmme_editor_apply_font_css(&app->config);
    lmme_window_update_status(app);
}

static void
action_focus_mode(LmmeApp *app)
{
    app->focus_mode = !app->focus_mode;
    gtk_widget_set_visible(app->sidebar, !app->focus_mode);
    gtk_widget_set_visible(app->toolbar,
                           !app->focus_mode && app->config.show_toolbar);
}

gboolean
lmme_command_handle_view(LmmeCommandHandler handler,
                         GSimpleAction *action,
                         GVariant *parameter,
                         LmmeApp *app)
{
    (void)action;
    (void)parameter;

    switch ((int)handler) {
    case LMME_COMMAND_HANDLER_TOGGLE_SIDEBAR:
        gtk_widget_set_visible(app->sidebar,
                               !gtk_widget_get_visible(app->sidebar));
        return TRUE;
    case LMME_COMMAND_HANDLER_TOGGLE_PREVIEW:
        lmme_window_toggle_preview(app);
        return TRUE;
    case LMME_COMMAND_HANDLER_ZOOM_IN:
        set_editor_font_size(app, app->config.font_size + 1);
        return TRUE;
    case LMME_COMMAND_HANDLER_ZOOM_OUT:
        set_editor_font_size(app, app->config.font_size - 1);
        return TRUE;
    case LMME_COMMAND_HANDLER_ZOOM_RESET:
        set_editor_font_size(app, LMME_EDITOR_FONT_SIZE_DEFAULT);
        return TRUE;
    case LMME_COMMAND_HANDLER_FOCUS_MODE:
        action_focus_mode(app);
        return TRUE;
    case LMME_COMMAND_HANDLER_ABOUT:
        lmme_dialog_info(GTK_WINDOW(app->window),
                         "LowMemoryMarkdownEditor",
                         "Small Linux-only GTK Markdown editor for local folders.");
        return TRUE;
    default:
        return FALSE;
    }
}
