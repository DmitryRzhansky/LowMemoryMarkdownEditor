#include "command/command_registry.h"

#include <gio/gio.h>

#define COMMAND(command_id, action, text, group, accel, command_handler) \
    {command_id, action, text, text, group, {accel, NULL}, NULL, command_handler, 0}
#define COMMAND_NO_ACCEL(command_id, action, text, group, command_handler) \
    {command_id, action, text, text, group, {NULL}, NULL, command_handler, 0}

static const LmmeCommandDef command_defs[] = {
    COMMAND("file.open_workspace", "open", "Open Folder", LMME_COMMAND_CATEGORY_FILE, "<Ctrl>O", LMME_COMMAND_HANDLER_OPEN),
    COMMAND("file.save", "save", "Save", LMME_COMMAND_CATEGORY_FILE, "<Ctrl>S", LMME_COMMAND_HANDLER_SAVE),
    COMMAND_NO_ACCEL("file.quit", "quit", "Quit", LMME_COMMAND_CATEGORY_FILE, LMME_COMMAND_HANDLER_QUIT),
    COMMAND("file.new_markdown", "new-file", "New File", LMME_COMMAND_CATEGORY_FILE, "<Ctrl>N", LMME_COMMAND_HANDLER_NEW_FILE),
    COMMAND("file.new_folder", "new-folder", "New Folder", LMME_COMMAND_CATEGORY_FILE, "<Ctrl><Shift>N", LMME_COMMAND_HANDLER_NEW_FOLDER),
    COMMAND_NO_ACCEL("file.rename", "rename", "Rename", LMME_COMMAND_CATEGORY_FILE, LMME_COMMAND_HANDLER_RENAME),
    COMMAND_NO_ACCEL("file.delete", "delete", "Delete", LMME_COMMAND_CATEGORY_FILE, LMME_COMMAND_HANDLER_DELETE),
    COMMAND_NO_ACCEL("view.toggle_sidebar", "toggle-sidebar", "Toggle Sidebar", LMME_COMMAND_CATEGORY_VIEW, LMME_COMMAND_HANDLER_TOGGLE_SIDEBAR),
    COMMAND("view.toggle_preview", "toggle-preview", "Toggle Editable Preview", LMME_COMMAND_CATEGORY_VIEW, "<Ctrl>P", LMME_COMMAND_HANDLER_TOGGLE_PREVIEW),
    {"view.zoom_in", "zoom-in", "Zoom In", "Zoom In", LMME_COMMAND_CATEGORY_VIEW,
     {"<Ctrl>plus", "<Ctrl>equal", "<Ctrl><Shift>plus", "<Ctrl><Shift>equal", "<Ctrl>KP_Add", NULL},
     NULL, LMME_COMMAND_HANDLER_ZOOM_IN, 0},
    {"view.zoom_out", "zoom-out", "Zoom Out", "Zoom Out", LMME_COMMAND_CATEGORY_VIEW,
     {"<Ctrl>minus", "<Ctrl>KP_Subtract", NULL}, NULL, LMME_COMMAND_HANDLER_ZOOM_OUT, 0},
    {"view.zoom_reset", "zoom-reset", "Reset Zoom", "Reset Zoom", LMME_COMMAND_CATEGORY_VIEW,
     {"<Ctrl>0", "<Ctrl>KP_0", NULL}, NULL, LMME_COMMAND_HANDLER_ZOOM_RESET, 0},
    COMMAND("view.focus_mode", "focus-mode", "Focus Mode", LMME_COMMAND_CATEGORY_VIEW, "F11", LMME_COMMAND_HANDLER_FOCUS_MODE),
    COMMAND("buffer.close", "close-tab", "Close Tab", LMME_COMMAND_CATEGORY_BUFFER, "<Ctrl>W", LMME_COMMAND_HANDLER_CLOSE_TAB),
    COMMAND_NO_ACCEL("buffer.close_others", "close-other-tabs", "Close Other Tabs", LMME_COMMAND_CATEGORY_BUFFER, LMME_COMMAND_HANDLER_CLOSE_OTHER_TABS),
    COMMAND_NO_ACCEL("buffer.close_right", "close-tabs-right", "Close Tabs to the Right", LMME_COMMAND_CATEGORY_BUFFER, LMME_COMMAND_HANDLER_CLOSE_TABS_RIGHT),
    COMMAND_NO_ACCEL("buffer.close_left", "close-tabs-left", "Close Tabs to the Left", LMME_COMMAND_CATEGORY_BUFFER, LMME_COMMAND_HANDLER_CLOSE_TABS_LEFT),
    COMMAND_NO_ACCEL("buffer.close_all", "close-all-tabs", "Close All Tabs", LMME_COMMAND_CATEGORY_BUFFER, LMME_COMMAND_HANDLER_CLOSE_ALL_TABS),
    COMMAND("buffer.next", "next-tab", "Next Tab", LMME_COMMAND_CATEGORY_BUFFER, "<Ctrl>Tab", LMME_COMMAND_HANDLER_NEXT_TAB),
    COMMAND("buffer.previous", "previous-tab", "Previous Tab", LMME_COMMAND_CATEGORY_BUFFER, "<Ctrl><Shift>Tab", LMME_COMMAND_HANDLER_PREVIOUS_TAB),
    COMMAND_NO_ACCEL("buffer.copy_relative_path", "copy-relative-path", "Copy Relative Path", LMME_COMMAND_CATEGORY_BUFFER, LMME_COMMAND_HANDLER_COPY_RELATIVE_PATH),
    COMMAND_NO_ACCEL("buffer.copy_full_path", "copy-full-path", "Copy Full Path", LMME_COMMAND_CATEGORY_BUFFER, LMME_COMMAND_HANDLER_COPY_FULL_PATH),
    COMMAND("edit.undo", "undo", "Undo", LMME_COMMAND_CATEGORY_EDIT, "<Ctrl>Z", LMME_COMMAND_HANDLER_UNDO),
    COMMAND("edit.redo", "redo", "Redo", LMME_COMMAND_CATEGORY_EDIT, "<Ctrl><Shift>Z", LMME_COMMAND_HANDLER_REDO),
    COMMAND_NO_ACCEL("edit.cut", "cut", "Cut", LMME_COMMAND_CATEGORY_EDIT, LMME_COMMAND_HANDLER_CUT),
    COMMAND_NO_ACCEL("edit.copy", "copy", "Copy", LMME_COMMAND_CATEGORY_EDIT, LMME_COMMAND_HANDLER_COPY),
    COMMAND_NO_ACCEL("edit.paste", "paste", "Paste", LMME_COMMAND_CATEGORY_EDIT, LMME_COMMAND_HANDLER_PASTE),
    COMMAND("edit.find", "find", "Find", LMME_COMMAND_CATEGORY_EDIT, "<Ctrl>F", LMME_COMMAND_HANDLER_SHOW_FIND),
    COMMAND("edit.replace", "replace", "Replace", LMME_COMMAND_CATEGORY_EDIT, "<Ctrl>H", LMME_COMMAND_HANDLER_SHOW_REPLACE),
    COMMAND_NO_ACCEL("search.find_next", "find-next", "Find Next", LMME_COMMAND_CATEGORY_EDIT, LMME_COMMAND_HANDLER_FIND_NEXT),
    COMMAND_NO_ACCEL("search.find_previous", "find-previous", "Find Previous", LMME_COMMAND_CATEGORY_EDIT, LMME_COMMAND_HANDLER_FIND_PREVIOUS),
    COMMAND_NO_ACCEL("search.replace_current", "replace-current", "Replace Current", LMME_COMMAND_CATEGORY_EDIT, LMME_COMMAND_HANDLER_REPLACE_CURRENT),
    COMMAND_NO_ACCEL("search.replace_all", "replace-all", "Replace All", LMME_COMMAND_CATEGORY_EDIT, LMME_COMMAND_HANDLER_REPLACE_ALL),
    COMMAND_NO_ACCEL("search.close", "search-close", "Close Search", LMME_COMMAND_CATEGORY_EDIT, LMME_COMMAND_HANDLER_SEARCH_CLOSE),
    COMMAND("editor.bold", "bold", "Bold", LMME_COMMAND_CATEGORY_EDIT, "<Ctrl>B", LMME_COMMAND_HANDLER_BOLD),
    COMMAND("editor.italic", "italic", "Italic", LMME_COMMAND_CATEGORY_EDIT, "<Ctrl>I", LMME_COMMAND_HANDLER_ITALIC),
    COMMAND("editor.insert_link", "insert-link", "Insert Link", LMME_COMMAND_CATEGORY_EDIT, "<Ctrl>K", LMME_COMMAND_HANDLER_INSERT_LINK),
    COMMAND_NO_ACCEL("editor.insert_image", "insert-image", "Insert Image", LMME_COMMAND_CATEGORY_EDIT, LMME_COMMAND_HANDLER_INSERT_IMAGE),
    COMMAND_NO_ACCEL("help.about", "about", "About", LMME_COMMAND_CATEGORY_HELP, LMME_COMMAND_HANDLER_ABOUT),
    COMMAND_NO_ACCEL("tree.open", "tree-open", "Open", LMME_COMMAND_CATEGORY_WORKSPACE, LMME_COMMAND_HANDLER_TREE_OPEN),
    COMMAND_NO_ACCEL("tree.close_tab", "tree-close-tab", "Close Tab", LMME_COMMAND_CATEGORY_WORKSPACE, LMME_COMMAND_HANDLER_TREE_CLOSE_TAB),
    COMMAND_NO_ACCEL("tree.new_file", "tree-new-file", "New Markdown File", LMME_COMMAND_CATEGORY_WORKSPACE, LMME_COMMAND_HANDLER_TREE_NEW_FILE),
    COMMAND_NO_ACCEL("tree.new_folder", "tree-new-folder", "New Folder", LMME_COMMAND_CATEGORY_WORKSPACE, LMME_COMMAND_HANDLER_TREE_NEW_FOLDER),
    COMMAND_NO_ACCEL("tree.rename", "tree-rename", "Rename", LMME_COMMAND_CATEGORY_WORKSPACE, LMME_COMMAND_HANDLER_TREE_RENAME),
    COMMAND_NO_ACCEL("tree.delete", "tree-delete", "Delete", LMME_COMMAND_CATEGORY_WORKSPACE, LMME_COMMAND_HANDLER_TREE_DELETE),
    COMMAND_NO_ACCEL("tree.copy_relative_path", "tree-copy-relative-path", "Copy Relative Path", LMME_COMMAND_CATEGORY_WORKSPACE, LMME_COMMAND_HANDLER_TREE_COPY_RELATIVE_PATH),
    COMMAND_NO_ACCEL("tree.copy_full_path", "tree-copy-full-path", "Copy Full Path", LMME_COMMAND_CATEGORY_WORKSPACE, LMME_COMMAND_HANDLER_TREE_COPY_FULL_PATH),
    COMMAND_NO_ACCEL("tree.copy_markdown_image_link", "tree-copy-markdown-image-link", "Copy Markdown Image Link", LMME_COMMAND_CATEGORY_WORKSPACE, LMME_COMMAND_HANDLER_TREE_COPY_IMAGE_LINK),
    COMMAND_NO_ACCEL("tree.open_containing_folder", "tree-open-containing-folder", "Open Containing Folder", LMME_COMMAND_CATEGORY_WORKSPACE, LMME_COMMAND_HANDLER_TREE_OPEN_CONTAINING_FOLDER),
};

const LmmeCommandDef *
lmme_command_registry_get_all(gsize *out_count)
{
    if (out_count != NULL) {
        *out_count = G_N_ELEMENTS(command_defs);
    }
    return command_defs;
}

const LmmeCommandDef *
lmme_command_registry_find(const char *id)
{
    if (id == NULL) {
        return NULL;
    }
    for (gsize i = 0; i < G_N_ELEMENTS(command_defs); i++) {
        if (g_strcmp0(command_defs[i].id, id) == 0) {
            return &command_defs[i];
        }
    }
    return NULL;
}

const LmmeCommandDef *
lmme_command_registry_find_action(const char *action_name)
{
    if (action_name == NULL) {
        return NULL;
    }
    for (gsize i = 0; i < G_N_ELEMENTS(command_defs); i++) {
        if (g_strcmp0(command_defs[i].action_name, action_name) == 0) {
            return &command_defs[i];
        }
    }
    return NULL;
}

gboolean
lmme_command_registry_validate(GError **error)
{
    g_autoptr(GHashTable) ids = g_hash_table_new(g_str_hash, g_str_equal);
    g_autoptr(GHashTable) actions = g_hash_table_new(g_str_hash, g_str_equal);
    g_autoptr(GHashTable) accelerators = g_hash_table_new(g_str_hash, g_str_equal);

    for (gsize i = 0; i < G_N_ELEMENTS(command_defs); i++) {
        const LmmeCommandDef *command = &command_defs[i];
        if (command->id == NULL || command->action_name == NULL ||
            command->handler >= LMME_COMMAND_HANDLER_COUNT ||
            g_hash_table_contains(ids, command->id) ||
            g_hash_table_contains(actions, command->action_name)) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Invalid or duplicate command definition.");
            return FALSE;
        }
        g_hash_table_add(ids, (gpointer)command->id);
        g_hash_table_add(actions, (gpointer)command->action_name);
        for (guint accel_index = 0; accel_index < G_N_ELEMENTS(command->default_accels); accel_index++) {
            const char *accel = command->default_accels[accel_index];
            if (accel == NULL) {
                break;
            }
            if (g_hash_table_contains(accelerators, accel)) {
                g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Duplicate default command accelerator.");
                return FALSE;
            }
            g_hash_table_add(accelerators, (gpointer)accel);
        }
    }
    return TRUE;
}
