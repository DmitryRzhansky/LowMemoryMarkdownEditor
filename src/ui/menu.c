#include "ui/menu.h"

GMenuModel *
lmme_menu_create_model(void)
{
    GMenu *bar = g_menu_new();
    GMenu *file = g_menu_new();
    GMenu *edit = g_menu_new();
    GMenu *view = g_menu_new();
    GMenu *help = g_menu_new();

    g_menu_append(file, "Open Folder", "app.open");
    g_menu_append(file, "New File", "app.new-file");
    g_menu_append(file, "New Folder", "app.new-folder");
    g_menu_append(file, "Rename", "app.rename");
    g_menu_append(file, "Delete", "app.delete");
    g_menu_append(file, "Save", "app.save");
    g_menu_append(file, "Quit", "app.quit");
    g_menu_append(edit, "Undo", "app.undo");
    g_menu_append(edit, "Redo", "app.redo");
    g_menu_append(edit, "Cut", "app.cut");
    g_menu_append(edit, "Copy", "app.copy");
    g_menu_append(edit, "Paste", "app.paste");
    g_menu_append(edit, "Find", "app.find");
    g_menu_append(edit, "Replace", "app.replace");
    g_menu_append(edit, "Bold", "app.bold");
    g_menu_append(edit, "Italic", "app.italic");
    g_menu_append(edit, "Insert Link", "app.insert-link");
    g_menu_append(edit, "Insert Image", "app.insert-image");
    g_menu_append(view, "Toggle Sidebar", "app.toggle-sidebar");
    g_menu_append(view, "Toggle Preview", "app.toggle-preview");
    g_menu_append(view, "Zoom In", "app.zoom-in");
    g_menu_append(view, "Zoom Out", "app.zoom-out");
    g_menu_append(view, "Reset Zoom", "app.zoom-reset");
    g_menu_append(view, "Focus Mode", "app.focus-mode");
    g_menu_append(help, "About", "app.about");

    g_menu_append_submenu(bar, "File", G_MENU_MODEL(file));
    g_menu_append_submenu(bar, "Edit", G_MENU_MODEL(edit));
    g_menu_append_submenu(bar, "View", G_MENU_MODEL(view));
    g_menu_append_submenu(bar, "Help", G_MENU_MODEL(help));
    g_object_unref(file);
    g_object_unref(edit);
    g_object_unref(view);
    g_object_unref(help);
    return G_MENU_MODEL(bar);
}
