#include "ui/toolbar.h"

static GtkWidget *
make_icon_button(const char *icon_name, const char *tooltip, const char *action_name)
{
    GtkWidget *button = gtk_button_new();
    GtkWidget *image = gtk_image_new_from_icon_name(icon_name);

    gtk_image_set_pixel_size(GTK_IMAGE(image), 18);
    gtk_button_set_child(GTK_BUTTON(button), image);
    gtk_actionable_set_action_name(GTK_ACTIONABLE(button), action_name);
    gtk_widget_set_tooltip_text(button, tooltip);
    gtk_widget_add_css_class(button, "toolbar-icon-button");
    return button;
}

GtkWidget *
lmme_toolbar_create(void)
{
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

    gtk_widget_add_css_class(toolbar, "toolbar");
    gtk_box_append(GTK_BOX(toolbar), make_icon_button("document-open-symbolic", "Open Workspace", "app.open"));
    gtk_box_append(GTK_BOX(toolbar), make_icon_button("document-new-symbolic", "New Markdown File", "app.new-file"));
    gtk_box_append(GTK_BOX(toolbar), make_icon_button("folder-new-symbolic", "New Folder", "app.new-folder"));
    gtk_box_append(GTK_BOX(toolbar), make_icon_button("document-save-symbolic", "Save", "app.save"));
    gtk_box_append(GTK_BOX(toolbar), make_icon_button("view-preview-symbolic", "Toggle Editable Preview", "app.toggle-preview"));
    gtk_box_append(GTK_BOX(toolbar), make_icon_button("image-x-generic-symbolic", "Insert Image", "app.insert-image"));
    gtk_box_append(GTK_BOX(toolbar), make_icon_button("edit-find-symbolic", "Find", "app.find"));
    gtk_box_append(GTK_BOX(toolbar), make_icon_button("edit-find-replace-symbolic", "Replace", "app.replace"));
    return toolbar;
}
