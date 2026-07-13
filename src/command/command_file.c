#include "command/command_handlers.h"

#include <gtk/gtk.h>

#include "app/app.h"
#include "document/document.h"
#include "document/file_monitor.h"
#include "document/tabs.h"
#include "infra/dialogs.h"
#include "ui/external_conflict.h"
#include "ui/window.h"

static void
action_open(LmmeApp *app)
{
    g_autofree char *path = lmme_dialog_open_folder(GTK_WINDOW(app->window));

    if (path != NULL) {
        lmme_window_open_workspace_path(app, path);
    }
}

static void
action_save(LmmeApp *app)
{
    LmmeDocument *doc = lmme_tabs_get_active(app);
    g_autoptr(GError) error = NULL;
    LmmeDocumentSaveResult result = LMME_DOCUMENT_SAVE_COMMITTED_DURABLE;

    if (doc != NULL && doc->disk_state != LMME_DISK_STATE_NORMAL) {
        lmme_external_conflict_request(doc);
    } else {
        result = lmme_tabs_save_active(app, &error);
    }
    if (result == LMME_DOCUMENT_SAVE_NOT_COMMITTED) {
        lmme_dialog_error(GTK_WINDOW(app->window),
                          "Could not save file.",
                          error != NULL ? error->message : NULL);
    } else if (result == LMME_DOCUMENT_SAVE_COMMITTED_NOT_DURABLE) {
        lmme_dialog_error(GTK_WINDOW(app->window),
                          "File was saved, but durability could not be confirmed.",
                          error != NULL ? error->message : NULL);
    }
    lmme_window_update_status(app);
}

gboolean
lmme_command_handle_file(LmmeCommandHandler handler,
                         GSimpleAction *action,
                         GVariant *parameter,
                         LmmeApp *app)
{
    (void)action;
    (void)parameter;

    switch ((int)handler) {
    case LMME_COMMAND_HANDLER_OPEN:
        action_open(app);
        return TRUE;
    case LMME_COMMAND_HANDLER_SAVE:
        action_save(app);
        return TRUE;
    case LMME_COMMAND_HANDLER_QUIT:
        (void)lmme_app_request_shutdown(app);
        return TRUE;
    default:
        return FALSE;
    }
}
