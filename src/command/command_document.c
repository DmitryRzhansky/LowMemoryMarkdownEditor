#include "command/command_handlers.h"

#include <gtk/gtk.h>

#include "app/app.h"
#include "document/document.h"
#include "document/tabs.h"

static void
action_close_other_tabs(LmmeApp *app)
{
    if (app->tab_context_document != NULL) {
        (void)lmme_tabs_close_other_tabs(app, app->tab_context_document);
    }
}

static void
action_close_tabs_right(LmmeApp *app)
{
    if (app->tab_context_document != NULL) {
        (void)lmme_tabs_close_tabs_to_right(app, app->tab_context_document);
    }
}

static void
action_close_tabs_left(LmmeApp *app)
{
    if (app->tab_context_document != NULL) {
        (void)lmme_tabs_close_tabs_to_left(app, app->tab_context_document);
    }
}

static void
action_copy_relative_path(LmmeApp *app)
{
    if (app->tab_context_document != NULL &&
        app->tab_context_document->relative_path != NULL) {
        gdk_clipboard_set_text(gtk_widget_get_clipboard(app->window),
                               app->tab_context_document->relative_path);
    }
}

static void
action_copy_full_path(LmmeApp *app)
{
    if (app->tab_context_document != NULL &&
        app->tab_context_document->path != NULL) {
        gdk_clipboard_set_text(gtk_widget_get_clipboard(app->window),
                               app->tab_context_document->path);
    }
}

gboolean
lmme_command_handle_document(LmmeCommandHandler handler,
                             GSimpleAction *action,
                             GVariant *parameter,
                             LmmeApp *app)
{
    (void)action;
    (void)parameter;

    switch ((int)handler) {
    case LMME_COMMAND_HANDLER_CLOSE_TAB:
        lmme_tabs_close_active(app);
        return TRUE;
    case LMME_COMMAND_HANDLER_CLOSE_OTHER_TABS:
        action_close_other_tabs(app);
        return TRUE;
    case LMME_COMMAND_HANDLER_CLOSE_TABS_RIGHT:
        action_close_tabs_right(app);
        return TRUE;
    case LMME_COMMAND_HANDLER_CLOSE_TABS_LEFT:
        action_close_tabs_left(app);
        return TRUE;
    case LMME_COMMAND_HANDLER_CLOSE_ALL_TABS:
        (void)lmme_tabs_close_all(app);
        return TRUE;
    case LMME_COMMAND_HANDLER_COPY_RELATIVE_PATH:
        action_copy_relative_path(app);
        return TRUE;
    case LMME_COMMAND_HANDLER_COPY_FULL_PATH:
        action_copy_full_path(app);
        return TRUE;
    case LMME_COMMAND_HANDLER_NEXT_TAB:
        gtk_notebook_next_page(GTK_NOTEBOOK(app->notebook));
        return TRUE;
    case LMME_COMMAND_HANDLER_PREVIOUS_TAB:
        gtk_notebook_prev_page(GTK_NOTEBOOK(app->notebook));
        return TRUE;
    default:
        return FALSE;
    }
}
