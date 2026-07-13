#include "command/command_enabled.h"

#include <gtk/gtk.h>

#include "app/app.h"
#include "document/document.h"
#include "document/tabs.h"
#include "infra/util.h"
#include "workspace/workspace.h"

void
lmme_command_context_fill_from_app(LmmeCommandContext *context, const LmmeApp *app)
{
    LmmeDocument *active = NULL;
    GtkTextBuffer *buffer = NULL;
    GtkTextIter start;
    GtkTextIter end;

    g_return_if_fail(context != NULL);
    *context = (LmmeCommandContext){0};

    if (app == NULL) {
        return;
    }

    context->has_workspace = app->workspace != NULL;
    context->document_count = app->documents != NULL ? app->documents->len : 0U;
    context->tab_context_document_set = app->tab_context_document != NULL;
    context->selection_has_path = app->selection.path != NULL;
    context->selection_is_directory = lmme_path_context_is_directory(&app->selection);
    context->selection_is_markdown = lmme_path_context_is_markdown(&app->selection);
    context->selection_is_image = lmme_path_context_is_image(&app->selection);
    if (context->has_workspace && context->selection_has_path) {
        context->selection_is_workspace_root =
            g_strcmp0(app->selection.path, app->workspace->path) == 0;
    }

    context->tree_has_path = app->tree_context.path != NULL;
    context->tree_empty_area = app->tree_context.empty_area;
    context->tree_is_directory = lmme_path_context_is_directory(&app->tree_context);
    context->tree_is_markdown = lmme_path_context_is_markdown(&app->tree_context);
    context->tree_is_image = lmme_path_context_is_image(&app->tree_context);
    if (context->has_workspace && context->tree_has_path) {
        context->tree_is_workspace_root =
            g_strcmp0(app->tree_context.path, app->workspace->path) == 0;
        context->tree_tab_open = lmme_tabs_find_by_path(app, app->tree_context.path) != NULL;
    }

    active = lmme_tabs_get_active((LmmeApp *)app);
    context->has_active_document = active != NULL;
    if (active == NULL) {
        return;
    }

    buffer = GTK_TEXT_BUFFER(active->buffer);
    context->can_undo = gtk_text_buffer_get_can_undo(buffer);
    context->can_redo = gtk_text_buffer_get_can_redo(buffer);
    context->has_selection = gtk_text_buffer_get_selection_bounds(buffer, &start, &end);
    context->active_is_markdown_in_workspace =
        context->has_workspace &&
        lmme_path_has_markdown_extension(active->path) &&
        lmme_path_is_inside(app->workspace->path, active->path);
}

gboolean
lmme_command_enabled_for_handler(LmmeCommandHandler handler, const LmmeCommandContext *context)
{
    const LmmeCommandContext empty = {0};

    if (context == NULL) {
        context = &empty;
    }

    switch ((int)handler) {
    case LMME_COMMAND_HANDLER_OPEN:
    case LMME_COMMAND_HANDLER_QUIT:
    case LMME_COMMAND_HANDLER_TOGGLE_SIDEBAR:
    case LMME_COMMAND_HANDLER_TOGGLE_PREVIEW:
    case LMME_COMMAND_HANDLER_ZOOM_IN:
    case LMME_COMMAND_HANDLER_ZOOM_OUT:
    case LMME_COMMAND_HANDLER_ZOOM_RESET:
    case LMME_COMMAND_HANDLER_FOCUS_MODE:
    case LMME_COMMAND_HANDLER_SEARCH_CLOSE:
        return TRUE;
    case LMME_COMMAND_HANDLER_SAVE:
    case LMME_COMMAND_HANDLER_PASTE:
    case LMME_COMMAND_HANDLER_SHOW_FIND:
    case LMME_COMMAND_HANDLER_SHOW_REPLACE:
    case LMME_COMMAND_HANDLER_FIND_NEXT:
    case LMME_COMMAND_HANDLER_FIND_PREVIOUS:
    case LMME_COMMAND_HANDLER_REPLACE_CURRENT:
    case LMME_COMMAND_HANDLER_REPLACE_ALL:
        return context->has_active_document;
    case LMME_COMMAND_HANDLER_NEW_FILE:
    case LMME_COMMAND_HANDLER_NEW_FOLDER:
    case LMME_COMMAND_HANDLER_TREE_NEW_FILE:
    case LMME_COMMAND_HANDLER_TREE_NEW_FOLDER:
        return context->has_workspace;
    case LMME_COMMAND_HANDLER_RENAME:
        return context->has_workspace && context->selection_has_path &&
               (context->selection_is_directory || context->selection_is_markdown ||
                context->selection_is_image);
    case LMME_COMMAND_HANDLER_DELETE:
        return context->has_workspace && context->selection_has_path &&
               !context->selection_is_workspace_root &&
               (context->selection_is_directory || context->selection_is_markdown ||
                context->selection_is_image);
    case LMME_COMMAND_HANDLER_CLOSE_TAB:
        return context->document_count > 0U;
    case LMME_COMMAND_HANDLER_CLOSE_OTHER_TABS:
    case LMME_COMMAND_HANDLER_CLOSE_TABS_RIGHT:
    case LMME_COMMAND_HANDLER_CLOSE_TABS_LEFT:
    case LMME_COMMAND_HANDLER_COPY_RELATIVE_PATH:
    case LMME_COMMAND_HANDLER_COPY_FULL_PATH:
        return context->tab_context_document_set;
    case LMME_COMMAND_HANDLER_CLOSE_ALL_TABS:
        return context->document_count > 0U;
    case LMME_COMMAND_HANDLER_NEXT_TAB:
    case LMME_COMMAND_HANDLER_PREVIOUS_TAB:
        return context->document_count > 1U;
    case LMME_COMMAND_HANDLER_UNDO:
        return context->has_active_document && context->can_undo;
    case LMME_COMMAND_HANDLER_REDO:
        return context->has_active_document && context->can_redo;
    case LMME_COMMAND_HANDLER_CUT:
    case LMME_COMMAND_HANDLER_COPY:
        return context->has_active_document && context->has_selection;
    case LMME_COMMAND_HANDLER_BOLD:
    case LMME_COMMAND_HANDLER_ITALIC:
    case LMME_COMMAND_HANDLER_INSERT_LINK:
    case LMME_COMMAND_HANDLER_INSERT_IMAGE:
        return context->active_is_markdown_in_workspace;
    case LMME_COMMAND_HANDLER_TREE_RENAME:
        return context->has_workspace && context->tree_has_path && !context->tree_empty_area &&
               (context->tree_is_directory || context->tree_is_markdown || context->tree_is_image);
    case LMME_COMMAND_HANDLER_TREE_DELETE:
        return context->has_workspace && context->tree_has_path && !context->tree_empty_area &&
               !context->tree_is_workspace_root &&
               (context->tree_is_directory || context->tree_is_markdown || context->tree_is_image);
    case LMME_COMMAND_HANDLER_TREE_OPEN:
        return context->tree_is_markdown;
    case LMME_COMMAND_HANDLER_TREE_CLOSE_TAB:
        return context->tree_tab_open;
    case LMME_COMMAND_HANDLER_TREE_COPY_RELATIVE_PATH:
    case LMME_COMMAND_HANDLER_TREE_COPY_FULL_PATH:
        return context->tree_has_path && !context->tree_empty_area;
    case LMME_COMMAND_HANDLER_TREE_COPY_IMAGE_LINK:
        return context->tree_is_image;
    case LMME_COMMAND_HANDLER_TREE_OPEN_CONTAINING_FOLDER:
        return context->tree_has_path && !context->tree_empty_area;
    default:
        return FALSE;
    }
}

gboolean
lmme_command_enabled_for_app(LmmeCommandHandler handler, const LmmeApp *app)
{
    LmmeCommandContext context;

    lmme_command_context_fill_from_app(&context, app);
    return lmme_command_enabled_for_handler(handler, &context);
}

#define LMME_DEFINE_COMMAND_ENABLED(handler) \
    gboolean \
    lmme_command_enabled_for_handler_##handler(const LmmeApp *app) \
    { \
        return lmme_command_enabled_for_app(handler, app); \
    }

LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_OPEN);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_SAVE);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_QUIT);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_NEW_FILE);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_NEW_FOLDER);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_RENAME);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_DELETE);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_TOGGLE_SIDEBAR);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_TOGGLE_PREVIEW);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_ZOOM_IN);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_ZOOM_OUT);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_ZOOM_RESET);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_FOCUS_MODE);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_CLOSE_TAB);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_CLOSE_OTHER_TABS);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_CLOSE_TABS_RIGHT);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_CLOSE_TABS_LEFT);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_CLOSE_ALL_TABS);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_COPY_RELATIVE_PATH);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_COPY_FULL_PATH);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_NEXT_TAB);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_PREVIOUS_TAB);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_UNDO);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_REDO);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_CUT);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_COPY);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_PASTE);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_SHOW_FIND);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_SHOW_REPLACE);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_FIND_NEXT);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_FIND_PREVIOUS);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_REPLACE_CURRENT);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_REPLACE_ALL);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_SEARCH_CLOSE);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_BOLD);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_ITALIC);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_INSERT_LINK);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_INSERT_IMAGE);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_TREE_NEW_FILE);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_TREE_NEW_FOLDER);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_TREE_RENAME);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_TREE_DELETE);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_TREE_OPEN);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_TREE_CLOSE_TAB);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_TREE_COPY_RELATIVE_PATH);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_TREE_COPY_FULL_PATH);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_TREE_COPY_IMAGE_LINK);
LMME_DEFINE_COMMAND_ENABLED(LMME_COMMAND_HANDLER_TREE_OPEN_CONTAINING_FOLDER);

#undef LMME_DEFINE_COMMAND_ENABLED
