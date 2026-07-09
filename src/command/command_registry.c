#include "command/command_registry.h"

static const LmmeCommandDef command_defs[] = {
    {"file.open_workspace", "open", "Open Folder", LMME_COMMAND_CATEGORY_FILE, "<Ctrl>O"},
    {"file.save", "save", "Save", LMME_COMMAND_CATEGORY_FILE, "<Ctrl>S"},
    {"file.quit", "quit", "Quit", LMME_COMMAND_CATEGORY_FILE, NULL},
    {"file.new_markdown", "new-file", "New File", LMME_COMMAND_CATEGORY_FILE, "<Ctrl>N"},
    {"file.new_folder", "new-folder", "New Folder", LMME_COMMAND_CATEGORY_FILE, "<Ctrl><Shift>N"},
    {"file.rename", "rename", "Rename", LMME_COMMAND_CATEGORY_FILE, NULL},
    {"file.delete", "delete", "Delete", LMME_COMMAND_CATEGORY_FILE, NULL},
    {"view.toggle_sidebar", "toggle-sidebar", "Toggle Sidebar", LMME_COMMAND_CATEGORY_VIEW, NULL},
    {"view.toggle_preview", "toggle-preview", "Toggle Preview", LMME_COMMAND_CATEGORY_VIEW, "<Ctrl>P"},
    {"view.zoom_in", "zoom-in", "Zoom In", LMME_COMMAND_CATEGORY_VIEW, "<Ctrl>plus"},
    {"view.zoom_out", "zoom-out", "Zoom Out", LMME_COMMAND_CATEGORY_VIEW, "<Ctrl>minus"},
    {"view.zoom_reset", "zoom-reset", "Reset Zoom", LMME_COMMAND_CATEGORY_VIEW, "<Ctrl>0"},
    {"view.focus_mode", "focus-mode", "Focus Mode", LMME_COMMAND_CATEGORY_VIEW, "F11"},
    {"buffer.close", "close-tab", "Close Tab", LMME_COMMAND_CATEGORY_BUFFER, "<Ctrl>W"},
    {"buffer.close_others", "close-other-tabs", "Close Other Tabs", LMME_COMMAND_CATEGORY_BUFFER, NULL},
    {"buffer.close_right", "close-tabs-right", "Close Tabs to the Right", LMME_COMMAND_CATEGORY_BUFFER, NULL},
    {"buffer.close_left", "close-tabs-left", "Close Tabs to the Left", LMME_COMMAND_CATEGORY_BUFFER, NULL},
    {"buffer.close_all", "close-all-tabs", "Close All Tabs", LMME_COMMAND_CATEGORY_BUFFER, NULL},
    {"buffer.next", "next-tab", "Next Tab", LMME_COMMAND_CATEGORY_BUFFER, "<Ctrl>Tab"},
    {"buffer.previous", "previous-tab", "Previous Tab", LMME_COMMAND_CATEGORY_BUFFER, "<Ctrl><Shift>Tab"},
    {"edit.undo", "undo", "Undo", LMME_COMMAND_CATEGORY_EDIT, "<Ctrl>Z"},
    {"edit.redo", "redo", "Redo", LMME_COMMAND_CATEGORY_EDIT, "<Ctrl><Shift>Z"},
    {"edit.cut", "cut", "Cut", LMME_COMMAND_CATEGORY_EDIT, NULL},
    {"edit.copy", "copy", "Copy", LMME_COMMAND_CATEGORY_EDIT, NULL},
    {"edit.paste", "paste", "Paste", LMME_COMMAND_CATEGORY_EDIT, NULL},
    {"edit.find", "find", "Find", LMME_COMMAND_CATEGORY_EDIT, "<Ctrl>F"},
    {"edit.replace", "replace", "Replace", LMME_COMMAND_CATEGORY_EDIT, "<Ctrl>H"},
    {"editor.bold", "bold", "Bold", LMME_COMMAND_CATEGORY_EDIT, "<Ctrl>B"},
    {"editor.italic", "italic", "Italic", LMME_COMMAND_CATEGORY_EDIT, "<Ctrl>I"},
    {"editor.insert_link", "insert-link", "Insert Link", LMME_COMMAND_CATEGORY_EDIT, "<Ctrl>K"},
    {"editor.insert_image", "insert-image", "Insert Image", LMME_COMMAND_CATEGORY_EDIT, NULL},
    {"help.about", "about", "About", LMME_COMMAND_CATEGORY_HELP, NULL},
    {"tree.open", "tree-open", "Open", LMME_COMMAND_CATEGORY_WORKSPACE, NULL},
    {"tree.close_tab", "tree-close-tab", "Close Tab", LMME_COMMAND_CATEGORY_WORKSPACE, NULL},
    {"tree.new_file", "tree-new-file", "New Markdown File", LMME_COMMAND_CATEGORY_WORKSPACE, NULL},
    {"tree.new_folder", "tree-new-folder", "New Folder", LMME_COMMAND_CATEGORY_WORKSPACE, NULL},
    {"tree.rename", "tree-rename", "Rename", LMME_COMMAND_CATEGORY_WORKSPACE, NULL},
    {"tree.delete", "tree-delete", "Delete", LMME_COMMAND_CATEGORY_WORKSPACE, NULL},
    {"tree.copy_relative_path", "tree-copy-relative-path", "Copy Relative Path", LMME_COMMAND_CATEGORY_WORKSPACE, NULL},
    {"tree.copy_full_path", "tree-copy-full-path", "Copy Full Path", LMME_COMMAND_CATEGORY_WORKSPACE, NULL},
    {"tree.copy_markdown_image_link", "tree-copy-markdown-image-link", "Copy Markdown Image Link", LMME_COMMAND_CATEGORY_WORKSPACE, NULL},
    {"tree.open_containing_folder", "tree-open-containing-folder", "Open Containing Folder", LMME_COMMAND_CATEGORY_WORKSPACE, NULL},
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
