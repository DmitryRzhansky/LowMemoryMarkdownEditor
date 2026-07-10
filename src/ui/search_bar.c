#include "ui/search_bar.h"

#include <gtk/gtk.h>

#include "app/app.h"
#include "document/tabs.h"

static void
set_valign_center(GtkWidget *widget)
{
    gtk_widget_set_valign(widget, GTK_ALIGN_CENTER);
}

static gboolean
on_search_entry_key_pressed(GtkEventControllerKey *controller,
                            guint keyval,
                            guint keycode,
                            GdkModifierType state,
                            gpointer user_data)
{
    LmmeApp *app = user_data;
    (void)controller;
    (void)keycode;

    if (keyval != GDK_KEY_Return && keyval != GDK_KEY_KP_Enter) {
        return FALSE;
    }
    g_action_group_activate_action(G_ACTION_GROUP(app->gtk_app),
                                   (state & GDK_SHIFT_MASK) != 0 ? "find-previous" : "find-next",
                                   NULL);
    return TRUE;
}

GtkWidget *
lmme_search_bar_create(LmmeApp *app)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *find_button = gtk_button_new_with_label("Find");
    GtkEventController *find_keys = NULL;
    GtkEventController *replace_keys = NULL;

    gtk_widget_add_css_class(box, "searchbar");
    app->find_entry = gtk_entry_new();
    app->replace_entry = gtk_entry_new();
    app->replace_button = gtk_button_new_with_label("Replace");
    gtk_widget_set_size_request(app->find_entry, 180, -1);
    gtk_widget_set_size_request(app->replace_entry, 180, -1);
    set_valign_center(app->find_entry);
    set_valign_center(find_button);
    set_valign_center(app->replace_entry);
    set_valign_center(app->replace_button);
    gtk_actionable_set_action_name(GTK_ACTIONABLE(find_button), "app.find-next");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(app->replace_button), "app.replace-current");
    gtk_box_append(GTK_BOX(box), app->find_entry);
    gtk_box_append(GTK_BOX(box), find_button);
    gtk_box_append(GTK_BOX(box), app->replace_entry);
    gtk_box_append(GTK_BOX(box), app->replace_button);
    gtk_widget_set_visible(box, FALSE);
    gtk_widget_set_visible(app->replace_entry, FALSE);
    find_keys = gtk_event_controller_key_new();
    replace_keys = gtk_event_controller_key_new();
    g_signal_connect(find_keys, "key-pressed", G_CALLBACK(on_search_entry_key_pressed), app);
    g_signal_connect(replace_keys, "key-pressed", G_CALLBACK(on_search_entry_key_pressed), app);
    gtk_widget_add_controller(app->find_entry, find_keys);
    gtk_widget_add_controller(app->replace_entry, replace_keys);
    return box;
}

gboolean
lmme_search_bar_is_visible(const LmmeApp *app)
{
    return app != NULL && app->search_bar != NULL && gtk_widget_get_visible(app->search_bar);
}

gboolean
lmme_search_bar_is_replace_mode(const LmmeApp *app)
{
    return app != NULL && app->replace_entry != NULL && gtk_widget_get_visible(app->replace_entry);
}

void
lmme_search_bar_show(LmmeApp *app, gboolean with_replace)
{
    gtk_widget_set_visible(app->search_bar, TRUE);
    gtk_widget_set_visible(app->replace_entry, with_replace);
    gtk_widget_grab_focus(app->find_entry);
}

void
lmme_search_bar_hide(LmmeApp *app)
{
    LmmeDocument *doc = NULL;

    if (!lmme_search_bar_is_visible(app)) {
        return;
    }

    gtk_widget_set_visible(app->search_bar, FALSE);
    gtk_widget_set_visible(app->replace_entry, FALSE);
    doc = lmme_tabs_get_active(app);
    if (doc != NULL) {
        gtk_widget_grab_focus(doc->source_view);
    }
}

gboolean
lmme_search_bar_handle_key_press(LmmeApp *app, guint keyval)
{
    if (keyval != GDK_KEY_Escape || !lmme_search_bar_is_visible(app)) {
        return FALSE;
    }

    lmme_search_bar_hide(app);
    return TRUE;
}
