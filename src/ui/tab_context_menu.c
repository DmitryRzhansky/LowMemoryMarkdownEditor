#include "ui/tab_context_menu.h"

#include "app/app.h"
#include "command/command_actions.h"
#include "document/document.h"

static GtkWidget *tab_context_popover;
static GMenuModel *tab_context_menu_model;

static GMenuModel *
create_tab_context_menu_model(void)
{
    GMenu *menu = g_menu_new();
    GMenu *close_section = g_menu_new();
    GMenu *path_section = g_menu_new();

    g_menu_append(close_section, "Close Tab", "app.close-tab");
    g_menu_append(close_section, "Close Other Tabs", "app.close-other-tabs");
    g_menu_append(close_section, "Close Tabs to the Right", "app.close-tabs-right");
    g_menu_append(close_section, "Close Tabs to the Left", "app.close-tabs-left");
    g_menu_append(close_section, "Close All Tabs", "app.close-all-tabs");
    g_menu_append(path_section, "Copy Relative Path", "app.copy-relative-path");
    g_menu_append(path_section, "Copy Full Path", "app.copy-full-path");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(close_section));
    g_menu_append_section(menu, NULL, G_MENU_MODEL(path_section));

    g_object_unref(close_section);
    g_object_unref(path_section);
    return G_MENU_MODEL(menu);
}

static GMenuModel *
get_tab_context_menu_model(void)
{
    if (tab_context_menu_model == NULL) {
        tab_context_menu_model = create_tab_context_menu_model();
    }

    return tab_context_menu_model;
}

static GtkWidget *
get_tab_context_popover(void)
{
    if (tab_context_popover == NULL) {
        tab_context_popover = gtk_popover_menu_new_from_model(get_tab_context_menu_model());
        gtk_popover_set_has_arrow(GTK_POPOVER(tab_context_popover), FALSE);
        gtk_popover_set_autohide(GTK_POPOVER(tab_context_popover), TRUE);
    }

    return tab_context_popover;
}

static void
on_tab_right_click(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data)
{
    LmmeDocument *doc = user_data;
    LmmeApp *app = doc != NULL ? doc->app : NULL;
    GtkNotebook *notebook = NULL;
    GtkWidget *popover = NULL;
    GtkWidget *anchor = NULL;
    int page = -1;
    GdkRectangle rect;
    graphene_point_t src_point;
    graphene_point_t dest_point;

    (void)gesture;

    if (doc == NULL || app == NULL || app->notebook == NULL || app->root_box == NULL || app->window == NULL ||
        doc->scroller == NULL || doc->tab_box == NULL || n_press != 1) {
        return;
    }

    notebook = GTK_NOTEBOOK(app->notebook);
    page = gtk_notebook_page_num(notebook, doc->scroller);
    if (page < 0) {
        return;
    }

    gtk_notebook_set_current_page(notebook, page);
    app->tab_context_document = doc;
    lmme_command_actions_refresh(app);
    popover = get_tab_context_popover();
    anchor = app->root_box;

    if (gtk_widget_get_parent(popover) != anchor) {
        if (gtk_widget_get_parent(popover) != NULL) {
            gtk_widget_unparent(popover);
        }
        gtk_widget_set_parent(popover, anchor);
    }
    if (gtk_widget_get_visible(popover)) {
        gtk_popover_popdown(GTK_POPOVER(popover));
    }

    graphene_point_init(&src_point, (float)x, (float)y);
    if (!gtk_widget_compute_point(doc->tab_box, anchor, &src_point, &dest_point)) {
        return;
    }

    rect.x = (int)dest_point.x;
    rect.y = (int)dest_point.y;
    rect.width = 1;
    rect.height = 1;
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
    gtk_popover_popup(GTK_POPOVER(popover));
}

void
lmme_tab_context_menu_attach(LmmeDocument *doc, GtkWidget *tab_box)
{
    GtkGesture *gesture = NULL;

    if (doc == NULL || tab_box == NULL) {
        return;
    }

    gesture = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_SECONDARY);
    g_signal_connect(gesture, "pressed", G_CALLBACK(on_tab_right_click), doc);
    gtk_widget_add_controller(tab_box, GTK_EVENT_CONTROLLER(gesture));
}
