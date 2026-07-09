#include "infra/dialogs.h"

typedef struct {
    GMainLoop *loop;
    int response;
} DialogWait;

static void
on_dialog_response(GtkDialog *dialog, int response_id, gpointer user_data)
{
    DialogWait *wait = user_data;
    wait->response = response_id;
    g_main_loop_quit(wait->loop);
    gtk_widget_hide(GTK_WIDGET(dialog));
}

static void
on_native_response(GtkNativeDialog *native, int response_id, gpointer user_data)
{
    DialogWait *wait = user_data;
    (void)native;
    wait->response = response_id;
    g_main_loop_quit(wait->loop);
}

static int
run_dialog_blocking(GtkDialog *dialog)
{
    DialogWait wait = {0};
    wait.loop = g_main_loop_new(NULL, FALSE);
    wait.response = GTK_RESPONSE_CANCEL;

    g_signal_connect(dialog, "response", G_CALLBACK(on_dialog_response), &wait);
    gtk_widget_show(GTK_WIDGET(dialog));
    g_main_loop_run(wait.loop);
    g_main_loop_unref(wait.loop);

    return wait.response;
}

static void
dialog_message(GtkWindow *parent, const char *title, const char *message, const char *detail)
{
    GtkWidget *dialog = gtk_dialog_new_with_buttons(title,
                                                    parent,
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "Close",
                                                    GTK_RESPONSE_CLOSE,
                                                    NULL);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *label = gtk_label_new(message);

    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_widget_set_margin_top(label, 12);
    gtk_widget_set_margin_bottom(label, 6);
    gtk_widget_set_margin_start(label, 12);
    gtk_widget_set_margin_end(label, 12);
    gtk_box_append(GTK_BOX(box), label);

    if (detail != NULL && detail[0] != '\0') {
        GtkWidget *detail_label = gtk_label_new(detail);
        gtk_label_set_wrap(GTK_LABEL(detail_label), TRUE);
        gtk_widget_set_margin_bottom(detail_label, 12);
        gtk_widget_set_margin_start(detail_label, 12);
        gtk_widget_set_margin_end(detail_label, 12);
        gtk_box_append(GTK_BOX(box), detail_label);
    }

    (void)run_dialog_blocking(GTK_DIALOG(dialog));
    gtk_window_destroy(GTK_WINDOW(dialog));
}

void
lmme_dialog_error(GtkWindow *parent, const char *message, const char *detail)
{
    dialog_message(parent, "Error", message, detail);
}

void
lmme_dialog_info(GtkWindow *parent, const char *message, const char *detail)
{
    dialog_message(parent, "LowMemoryMarkdownEditor", message, detail);
}

char *
lmme_dialog_prompt_text(GtkWindow *parent,
                        const char *title,
                        const char *label_text,
                        const char *initial_text)
{
    GtkWidget *dialog = gtk_dialog_new_with_buttons(title,
                                                    parent,
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "Cancel",
                                                    GTK_RESPONSE_CANCEL,
                                                    "Save",
                                                    GTK_RESPONSE_ACCEPT,
                                                    NULL);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *label = gtk_label_new(label_text);
    GtkWidget *entry = gtk_entry_new();
    char *result = NULL;

    gtk_widget_set_margin_top(label, 12);
    gtk_widget_set_margin_start(label, 12);
    gtk_widget_set_margin_end(label, 12);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_box_append(GTK_BOX(box), label);

    gtk_editable_set_text(GTK_EDITABLE(entry), initial_text != NULL ? initial_text : "");
    gtk_widget_set_margin_top(entry, 6);
    gtk_widget_set_margin_bottom(entry, 12);
    gtk_widget_set_margin_start(entry, 12);
    gtk_widget_set_margin_end(entry, 12);
    gtk_box_append(GTK_BOX(box), entry);

    if (run_dialog_blocking(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        result = g_strdup(gtk_editable_get_text(GTK_EDITABLE(entry)));
    }

    gtk_window_destroy(GTK_WINDOW(dialog));
    return result;
}

gboolean
lmme_dialog_confirm_delete(GtkWindow *parent, gboolean *dont_show_again)
{
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Delete",
                                                    parent,
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "Cancel",
                                                    GTK_RESPONSE_CANCEL,
                                                    "Delete",
                                                    GTK_RESPONSE_ACCEPT,
                                                    NULL);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *label = gtk_label_new("This action permanently deletes the selected item.");
    GtkWidget *check = gtk_check_button_new_with_label("Do not show this again");
    gboolean confirmed = FALSE;

    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_widget_set_margin_top(label, 12);
    gtk_widget_set_margin_start(label, 12);
    gtk_widget_set_margin_end(label, 12);
    gtk_box_append(GTK_BOX(box), label);

    gtk_widget_set_margin_top(check, 8);
    gtk_widget_set_margin_bottom(check, 12);
    gtk_widget_set_margin_start(check, 12);
    gtk_widget_set_margin_end(check, 12);
    gtk_box_append(GTK_BOX(box), check);

    confirmed = run_dialog_blocking(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT;
    if (dont_show_again != NULL) {
        *dont_show_again = gtk_check_button_get_active(GTK_CHECK_BUTTON(check));
    }

    gtk_window_destroy(GTK_WINDOW(dialog));
    return confirmed;
}

static char *
run_file_chooser(GtkWindow *parent, const char *title, GtkFileChooserAction action, GtkFileFilter *filter)
{
    GtkFileChooserNative *native = gtk_file_chooser_native_new(title,
                                                               parent,
                                                               action,
                                                               action == GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER ? "Open" : "Insert",
                                                               "Cancel");
    DialogWait wait = {0};
    char *path = NULL;

    if (filter != NULL) {
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native), filter);
    }

    wait.loop = g_main_loop_new(NULL, FALSE);
    wait.response = GTK_RESPONSE_CANCEL;
    g_signal_connect(native, "response", G_CALLBACK(on_native_response), &wait);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(native));
    g_main_loop_run(wait.loop);
    g_main_loop_unref(wait.loop);

    if (wait.response == GTK_RESPONSE_ACCEPT) {
        GFile *file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(native));
        if (file != NULL) {
            path = g_file_get_path(file);
            g_object_unref(file);
        }
    }

    g_object_unref(native);
    return path;
}

char *
lmme_dialog_open_folder(GtkWindow *parent)
{
    return run_file_chooser(parent, "Open Workspace", GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, NULL);
}

char *
lmme_dialog_open_image(GtkWindow *parent)
{
    GtkFileFilter *filter = gtk_file_filter_new();
    char *path = NULL;
    gtk_file_filter_set_name(filter, "Images");
    gtk_file_filter_add_pattern(filter, "*.png");
    gtk_file_filter_add_pattern(filter, "*.jpg");
    gtk_file_filter_add_pattern(filter, "*.jpeg");
    gtk_file_filter_add_pattern(filter, "*.webp");
    gtk_file_filter_add_pattern(filter, "*.gif");

    path = run_file_chooser(parent, "Insert Image", GTK_FILE_CHOOSER_ACTION_OPEN, filter);
    g_object_unref(filter);
    return path;
}

gboolean
lmme_dialog_confirm_close_unsaved(GtkWindow *parent, const char *filename)
{
    g_autofree char *message = g_strdup_printf("Could not save %s. Close anyway?", filename != NULL ? filename : "file");
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Close",
                                                    parent,
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "Cancel",
                                                    GTK_RESPONSE_CANCEL,
                                                    "Close",
                                                    GTK_RESPONSE_ACCEPT,
                                                    NULL);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *label = gtk_label_new(message);
    gboolean close_anyway = FALSE;

    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_widget_set_margin_top(label, 12);
    gtk_widget_set_margin_bottom(label, 12);
    gtk_widget_set_margin_start(label, 12);
    gtk_widget_set_margin_end(label, 12);
    gtk_box_append(GTK_BOX(box), label);

    close_anyway = run_dialog_blocking(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT;
    gtk_window_destroy(GTK_WINDOW(dialog));
    return close_anyway;
}
