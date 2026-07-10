#include "command/command_handlers.h"

#include <gtk/gtk.h>

#include "app/app.h"
#include "document/document.h"
#include "document/tabs.h"
#include "editor/editor_ops.h"
#include "editor/editor_search.h"
#include "features/image_insert.h"
#include "ui/search_bar.h"
#include "ui/window.h"

static LmmeDocument *
active_document(LmmeApp *app)
{
    return lmme_tabs_get_active(app);
}

static void
action_undo(LmmeApp *app)
{
    LmmeDocument *doc = active_document(app);

    if (doc != NULL && gtk_text_buffer_get_can_undo(GTK_TEXT_BUFFER(doc->buffer))) {
        gtk_text_buffer_undo(GTK_TEXT_BUFFER(doc->buffer));
    }
}

static void
action_redo(LmmeApp *app)
{
    LmmeDocument *doc = active_document(app);

    if (doc != NULL && gtk_text_buffer_get_can_redo(GTK_TEXT_BUFFER(doc->buffer))) {
        gtk_text_buffer_redo(GTK_TEXT_BUFFER(doc->buffer));
    }
}

static void
action_cut(LmmeApp *app)
{
    LmmeDocument *doc = active_document(app);

    if (doc != NULL) {
        gtk_text_buffer_cut_clipboard(GTK_TEXT_BUFFER(doc->buffer),
                                      gtk_widget_get_clipboard(doc->source_view),
                                      TRUE);
    }
}

static void
action_copy(LmmeApp *app)
{
    LmmeDocument *doc = active_document(app);

    if (doc != NULL) {
        gtk_text_buffer_copy_clipboard(GTK_TEXT_BUFFER(doc->buffer),
                                       gtk_widget_get_clipboard(doc->source_view));
    }
}

static void
action_paste(LmmeApp *app)
{
    LmmeDocument *doc = active_document(app);

    if (doc != NULL) {
        gtk_text_buffer_paste_clipboard(GTK_TEXT_BUFFER(doc->buffer),
                                        gtk_widget_get_clipboard(doc->source_view),
                                        NULL,
                                        TRUE);
    }
}

static void
action_find_next(LmmeApp *app)
{
    LmmeDocument *doc = active_document(app);
    const char *needle = gtk_editable_get_text(GTK_EDITABLE(app->find_entry));

    if (doc != NULL && needle[0] != '\0' &&
        !lmme_editor_find_next(GTK_TEXT_BUFFER(doc->buffer), needle)) {
        lmme_window_set_status_error(app, "No matches");
    }
}

static void
action_find_previous(LmmeApp *app)
{
    LmmeDocument *doc = active_document(app);
    const char *needle = gtk_editable_get_text(GTK_EDITABLE(app->find_entry));

    if (doc != NULL && needle[0] != '\0' &&
        !lmme_editor_find_previous(GTK_TEXT_BUFFER(doc->buffer), needle)) {
        lmme_window_set_status_error(app, "No matches");
    }
}

static void
action_replace_current(LmmeApp *app)
{
    LmmeDocument *doc = active_document(app);
    const char *needle = gtk_editable_get_text(GTK_EDITABLE(app->find_entry));
    const char *replacement = gtk_editable_get_text(GTK_EDITABLE(app->replace_entry));

    if (doc != NULL && needle[0] != '\0' &&
        !lmme_editor_replace_current(GTK_TEXT_BUFFER(doc->buffer), needle, replacement)) {
        lmme_window_set_status_error(app, "No matches");
    }
}

static void
action_replace_all(LmmeApp *app)
{
    LmmeDocument *doc = active_document(app);
    const char *needle = gtk_editable_get_text(GTK_EDITABLE(app->find_entry));
    const char *replacement = gtk_editable_get_text(GTK_EDITABLE(app->replace_entry));
    guint count = 0;

    if (doc != NULL && needle[0] != '\0') {
        count = lmme_editor_replace_all(GTK_TEXT_BUFFER(doc->buffer), needle, replacement);
    }
    if (count == 0) {
        lmme_window_set_status_error(app, "No matches");
    } else {
        lmme_window_update_status(app);
    }
}

static void
action_wrap(LmmeApp *app, const char *prefix, const char *suffix, int cursor_back)
{
    LmmeDocument *doc = active_document(app);

    if (doc != NULL) {
        lmme_editor_wrap_selection(GTK_TEXT_BUFFER(doc->buffer),
                                   prefix,
                                   suffix,
                                   cursor_back);
    }
}

gboolean
lmme_command_handle_editor(LmmeCommandHandler handler,
                           GSimpleAction *action,
                           GVariant *parameter,
                           LmmeApp *app)
{
    (void)action;
    (void)parameter;

    switch ((int)handler) {
    case LMME_COMMAND_HANDLER_UNDO:
        action_undo(app);
        return TRUE;
    case LMME_COMMAND_HANDLER_REDO:
        action_redo(app);
        return TRUE;
    case LMME_COMMAND_HANDLER_CUT:
        action_cut(app);
        return TRUE;
    case LMME_COMMAND_HANDLER_COPY:
        action_copy(app);
        return TRUE;
    case LMME_COMMAND_HANDLER_PASTE:
        action_paste(app);
        return TRUE;
    case LMME_COMMAND_HANDLER_SHOW_FIND:
        lmme_search_bar_show(app, FALSE);
        return TRUE;
    case LMME_COMMAND_HANDLER_SHOW_REPLACE:
        lmme_search_bar_show(app, TRUE);
        return TRUE;
    case LMME_COMMAND_HANDLER_FIND_NEXT:
        action_find_next(app);
        return TRUE;
    case LMME_COMMAND_HANDLER_FIND_PREVIOUS:
        action_find_previous(app);
        return TRUE;
    case LMME_COMMAND_HANDLER_REPLACE_CURRENT:
        action_replace_current(app);
        return TRUE;
    case LMME_COMMAND_HANDLER_REPLACE_ALL:
        action_replace_all(app);
        return TRUE;
    case LMME_COMMAND_HANDLER_SEARCH_CLOSE:
        lmme_search_bar_hide(app);
        return TRUE;
    case LMME_COMMAND_HANDLER_BOLD:
        action_wrap(app, "**", "**", 2);
        return TRUE;
    case LMME_COMMAND_HANDLER_ITALIC:
        action_wrap(app, "*", "*", 1);
        return TRUE;
    case LMME_COMMAND_HANDLER_INSERT_LINK:
        action_wrap(app, "[", "]()", 3);
        return TRUE;
    case LMME_COMMAND_HANDLER_INSERT_IMAGE:
        lmme_image_insert_from_dialog(app);
        return TRUE;
    default:
        return FALSE;
    }
}
