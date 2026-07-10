#include "infra/dialogs.h"

typedef struct {
    GMainLoop *loop;
    int response;
} DialogWait;

typedef struct {
    GtkWidget *window;
    GtkWidget *content;
    GtkWidget *buttons;
} DialogShell;

static const char *dialog_wait_key = "lmme-dialog-wait";
static const char *dialog_response_key = "lmme-dialog-response";

static gboolean
on_dialog_close_request(GtkWindow *window, gpointer user_data)
{
    DialogWait *wait = g_object_get_data(G_OBJECT(window), dialog_wait_key);
    (void)user_data;
    if (wait != NULL) {
        wait->response = GTK_RESPONSE_CANCEL;
        g_main_loop_quit(wait->loop);
    }
    return TRUE;
}

static void
on_response_button_clicked(GtkButton *button, gpointer user_data)
{
    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(button));
    DialogWait *wait = root != NULL ? g_object_get_data(G_OBJECT(root), dialog_wait_key) : NULL;
    (void)user_data;
    if (wait == NULL) {
        return;
    }
    wait->response = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), dialog_response_key));
    g_main_loop_quit(wait->loop);
}

static DialogShell
dialog_shell_new(GtkWindow *parent, const char *title)
{
    DialogShell shell = {0};
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);

    shell.window = gtk_window_new();
    shell.content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    shell.buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_window_set_title(GTK_WINDOW(shell.window), title);
    gtk_window_set_modal(GTK_WINDOW(shell.window), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(shell.window), FALSE);
    if (parent != NULL) {
        gtk_window_set_transient_for(GTK_WINDOW(shell.window), parent);
    }
    gtk_widget_set_margin_top(root, 12);
    gtk_widget_set_margin_bottom(root, 12);
    gtk_widget_set_margin_start(root, 12);
    gtk_widget_set_margin_end(root, 12);
    gtk_widget_set_halign(shell.buttons, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(root), shell.content);
    gtk_box_append(GTK_BOX(root), shell.buttons);
    gtk_window_set_child(GTK_WINDOW(shell.window), root);
    g_signal_connect(shell.window, "close-request", G_CALLBACK(on_dialog_close_request), NULL);
    return shell;
}

static void
dialog_add_button(DialogShell *shell, const char *label, int response)
{
    GtkWidget *button = gtk_button_new_with_label(label);
    g_object_set_data(G_OBJECT(button), dialog_response_key, GINT_TO_POINTER(response));
    g_signal_connect(button, "clicked", G_CALLBACK(on_response_button_clicked), NULL);
    gtk_box_append(GTK_BOX(shell->buttons), button);
}

static int
dialog_run(DialogShell *shell)
{
    DialogWait wait = {0};

    wait.loop = g_main_loop_new(NULL, FALSE);
    wait.response = GTK_RESPONSE_CANCEL;
    g_object_set_data(G_OBJECT(shell->window), dialog_wait_key, &wait);
    gtk_window_present(GTK_WINDOW(shell->window));
    g_main_loop_run(wait.loop);
    g_object_set_data(G_OBJECT(shell->window), dialog_wait_key, NULL);
    g_main_loop_unref(wait.loop);
    gtk_widget_set_visible(shell->window, FALSE);
    return wait.response;
}

static void
dialog_shell_destroy(DialogShell *shell)
{
    gtk_window_destroy(GTK_WINDOW(shell->window));
    shell->window = NULL;
}

static GtkWidget *
dialog_label(const char *text)
{
    GtkWidget *label = gtk_label_new(text != NULL ? text : "");
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    return label;
}

static void
dialog_message(GtkWindow *parent, const char *title, const char *message, const char *detail)
{
    DialogShell shell = dialog_shell_new(parent, title);
    gtk_box_append(GTK_BOX(shell.content), dialog_label(message));
    if (detail != NULL && detail[0] != '\0') {
        gtk_box_append(GTK_BOX(shell.content), dialog_label(detail));
    }
    dialog_add_button(&shell, "Close", GTK_RESPONSE_CLOSE);
    (void)dialog_run(&shell);
    dialog_shell_destroy(&shell);
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
    DialogShell shell = dialog_shell_new(parent, title);
    GtkWidget *entry = gtk_entry_new();
    char *result = NULL;

    gtk_box_append(GTK_BOX(shell.content), dialog_label(label_text));
    gtk_editable_set_text(GTK_EDITABLE(entry), initial_text != NULL ? initial_text : "");
    gtk_widget_set_size_request(entry, 360, -1);
    gtk_box_append(GTK_BOX(shell.content), entry);
    dialog_add_button(&shell, "Cancel", GTK_RESPONSE_CANCEL);
    dialog_add_button(&shell, "Save", GTK_RESPONSE_ACCEPT);
    if (dialog_run(&shell) == GTK_RESPONSE_ACCEPT) {
        result = g_strdup(gtk_editable_get_text(GTK_EDITABLE(entry)));
    }
    dialog_shell_destroy(&shell);
    return result;
}

gboolean
lmme_dialog_confirm_delete(GtkWindow *parent, gboolean *dont_show_again)
{
    DialogShell shell = dialog_shell_new(parent, "Delete");
    GtkWidget *check = gtk_check_button_new_with_label("Do not show this again");
    gboolean confirmed = FALSE;

    gtk_box_append(GTK_BOX(shell.content),
                   dialog_label("This action permanently deletes the selected item."));
    gtk_box_append(GTK_BOX(shell.content), check);
    dialog_add_button(&shell, "Cancel", GTK_RESPONSE_CANCEL);
    dialog_add_button(&shell, "Delete", GTK_RESPONSE_ACCEPT);
    confirmed = dialog_run(&shell) == GTK_RESPONSE_ACCEPT;
    if (dont_show_again != NULL) {
        *dont_show_again = gtk_check_button_get_active(GTK_CHECK_BUTTON(check));
    }
    dialog_shell_destroy(&shell);
    return confirmed;
}

gboolean
lmme_dialog_confirm_delete_open_documents(GtkWindow *parent,
                                          guint modified_count,
                                          guint total_count)
{
    g_autofree char *message = g_strdup_printf(
        "This permanently deletes %u open file%s. %u contain%s unsaved changes.",
        total_count,
        total_count == 1 ? "" : "s",
        modified_count,
        modified_count == 1 ? "s" : "");
    DialogShell shell = dialog_shell_new(parent, "Delete Open Files");
    gtk_box_append(GTK_BOX(shell.content), dialog_label(message));
    dialog_add_button(&shell, "Cancel", GTK_RESPONSE_CANCEL);
    dialog_add_button(&shell, "Delete and Discard Changes", GTK_RESPONSE_ACCEPT);
    gboolean confirmed = dialog_run(&shell) == GTK_RESPONSE_ACCEPT;
    dialog_shell_destroy(&shell);
    return confirmed;
}

typedef enum {
    FILE_DIALOG_OPEN,
    FILE_DIALOG_SELECT_FOLDER,
    FILE_DIALOG_SAVE
} FileDialogMode;

#if GTK_CHECK_VERSION(4, 10, 0)
typedef struct {
    GMainLoop *loop;
    FileDialogMode mode;
    GFile *file;
} FileDialogWait;

static void
on_file_dialog_finished(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source_object);
    FileDialogWait *wait = user_data;
    g_autoptr(GError) error = NULL;

    switch (wait->mode) {
    case FILE_DIALOG_SELECT_FOLDER:
        wait->file = gtk_file_dialog_select_folder_finish(dialog, result, &error);
        break;
    case FILE_DIALOG_SAVE:
        wait->file = gtk_file_dialog_save_finish(dialog, result, &error);
        break;
    case FILE_DIALOG_OPEN:
    default:
        wait->file = gtk_file_dialog_open_finish(dialog, result, &error);
        break;
    }
    g_main_loop_quit(wait->loop);
}

static char *
run_file_dialog(GtkWindow *parent,
                const char *title,
                FileDialogMode mode,
                GtkFileFilter *filter,
                const char *suggested_path)
{
    g_autoptr(GtkFileDialog) dialog = gtk_file_dialog_new();
    FileDialogWait wait = {0};
    char *path = NULL;

    gtk_file_dialog_set_title(dialog, title);
    gtk_file_dialog_set_modal(dialog, TRUE);
    if (filter != NULL) {
        g_autoptr(GListStore) filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
        g_list_store_append(filters, filter);
        gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
        gtk_file_dialog_set_default_filter(dialog, filter);
    }
    if (suggested_path != NULL) {
        g_autoptr(GFile) suggested = g_file_new_for_path(suggested_path);
        gtk_file_dialog_set_initial_file(dialog, suggested);
    }

    wait.loop = g_main_loop_new(NULL, FALSE);
    wait.mode = mode;
    switch (mode) {
    case FILE_DIALOG_SELECT_FOLDER:
        gtk_file_dialog_select_folder(dialog, parent, NULL, on_file_dialog_finished, &wait);
        break;
    case FILE_DIALOG_SAVE:
        gtk_file_dialog_save(dialog, parent, NULL, on_file_dialog_finished, &wait);
        break;
    case FILE_DIALOG_OPEN:
    default:
        gtk_file_dialog_open(dialog, parent, NULL, on_file_dialog_finished, &wait);
        break;
    }
    g_main_loop_run(wait.loop);
    g_main_loop_unref(wait.loop);
    if (wait.file != NULL) {
        path = g_file_get_path(wait.file);
        g_object_unref(wait.file);
    }
    return path;
}
#else
typedef struct {
    GMainLoop *loop;
    int response;
} NativeDialogWait;

static void
on_native_response(GtkNativeDialog *native, int response_id, gpointer user_data)
{
    NativeDialogWait *wait = user_data;
    (void)native;
    wait->response = response_id;
    g_main_loop_quit(wait->loop);
}

static char *
run_file_dialog(GtkWindow *parent,
                const char *title,
                FileDialogMode mode,
                GtkFileFilter *filter,
                const char *suggested_path)
{
    GtkFileChooserAction action = mode == FILE_DIALOG_SELECT_FOLDER
                                      ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER
                                      : mode == FILE_DIALOG_SAVE ? GTK_FILE_CHOOSER_ACTION_SAVE
                                                                 : GTK_FILE_CHOOSER_ACTION_OPEN;
    GtkFileChooserNative *native = gtk_file_chooser_native_new(title,
                                                               parent,
                                                               action,
                                                               mode == FILE_DIALOG_OPEN ? "Open" : "Save",
                                                               "Cancel");
    NativeDialogWait wait = {0};
    char *path = NULL;

    if (filter != NULL) {
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native), filter);
    }
    if (suggested_path != NULL) {
        g_autoptr(GFile) suggested = g_file_new_for_path(suggested_path);
        gtk_file_chooser_set_file(GTK_FILE_CHOOSER(native), suggested, NULL);
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
#endif

char *
lmme_dialog_open_folder(GtkWindow *parent)
{
    return run_file_dialog(parent, "Open Workspace", FILE_DIALOG_SELECT_FOLDER, NULL, NULL);
}

char *
lmme_dialog_open_image(GtkWindow *parent)
{
    g_autoptr(GtkFileFilter) filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Images");
    gtk_file_filter_add_pattern(filter, "*.png");
    gtk_file_filter_add_pattern(filter, "*.jpg");
    gtk_file_filter_add_pattern(filter, "*.jpeg");
    gtk_file_filter_add_pattern(filter, "*.webp");
    gtk_file_filter_add_pattern(filter, "*.gif");
    return run_file_dialog(parent, "Insert Image", FILE_DIALOG_OPEN, filter, NULL);
}

char *
lmme_dialog_save_markdown(GtkWindow *parent, const char *suggested_path)
{
    g_autoptr(GtkFileFilter) filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Markdown files");
    gtk_file_filter_add_pattern(filter, "*.md");
    gtk_file_filter_add_pattern(filter, "*.markdown");
    return run_file_dialog(parent, "Save Markdown File", FILE_DIALOG_SAVE, filter, suggested_path);
}

gboolean
lmme_dialog_confirm_overwrite(GtkWindow *parent, const char *path)
{
    g_autofree char *base = g_path_get_basename(path != NULL ? path : "file");
    g_autofree char *message = g_strdup_printf("%s already exists. Replace it?", base);
    DialogShell shell = dialog_shell_new(parent, "Replace File");
    gtk_box_append(GTK_BOX(shell.content), dialog_label(message));
    dialog_add_button(&shell, "Cancel", GTK_RESPONSE_CANCEL);
    dialog_add_button(&shell, "Replace", GTK_RESPONSE_ACCEPT);
    gboolean confirmed = dialog_run(&shell) == GTK_RESPONSE_ACCEPT;
    dialog_shell_destroy(&shell);
    return confirmed;
}

gboolean
lmme_dialog_confirm_close_unsaved(GtkWindow *parent, const char *filename)
{
    g_autofree char *message = g_strdup_printf("Could not save %s. Close anyway?",
                                               filename != NULL ? filename : "file");
    DialogShell shell = dialog_shell_new(parent, "Close");
    gtk_box_append(GTK_BOX(shell.content), dialog_label(message));
    dialog_add_button(&shell, "Cancel", GTK_RESPONSE_CANCEL);
    dialog_add_button(&shell, "Close", GTK_RESPONSE_ACCEPT);
    gboolean confirmed = dialog_run(&shell) == GTK_RESPONSE_ACCEPT;
    dialog_shell_destroy(&shell);
    return confirmed;
}

LmmeSaveFailureChoice
lmme_dialog_resolve_save_failure(GtkWindow *parent,
                                 const char *filename,
                                 const char *detail,
                                 gboolean allow_retry,
                                 gboolean allow_keep_recovery)
{
    enum {
        RESPONSE_RETRY = 1,
        RESPONSE_KEEP_RECOVERY = 2
    };
    g_autofree char *message = g_strdup_printf("Could not safely close %s.",
                                               filename != NULL ? filename : "file");
    DialogShell shell = dialog_shell_new(parent, "Unsaved Changes");
    int response;

    gtk_box_append(GTK_BOX(shell.content), dialog_label(message));
    if (detail != NULL && detail[0] != '\0') {
        gtk_box_append(GTK_BOX(shell.content), dialog_label(detail));
    }
    dialog_add_button(&shell, "Cancel", GTK_RESPONSE_CANCEL);
    if (allow_retry) {
        dialog_add_button(&shell, "Retry", RESPONSE_RETRY);
    }
    if (allow_keep_recovery) {
        dialog_add_button(&shell, "Close and Keep Recovery", RESPONSE_KEEP_RECOVERY);
    }
    response = dialog_run(&shell);
    dialog_shell_destroy(&shell);
    if (response == RESPONSE_RETRY) {
        return LMME_SAVE_FAILURE_RETRY;
    }
    if (response == RESPONSE_KEEP_RECOVERY) {
        return LMME_SAVE_FAILURE_KEEP_RECOVERY;
    }
    return LMME_SAVE_FAILURE_CANCEL;
}

LmmeRecoveryChoice
lmme_dialog_choose_recovery(GtkWindow *parent,
                            const char *original_path,
                            gboolean original_changed)
{
    enum {
        RESPONSE_RESTORE = 1,
        RESPONSE_DISCARD = 2
    };
    g_autofree char *message = g_strdup_printf("Unsaved recovery data was found for:\n%s",
                                               original_path != NULL ? original_path : "Unknown file");
    DialogShell shell = dialog_shell_new(parent, "Recover Unsaved Changes");
    int response;

    gtk_box_append(GTK_BOX(shell.content), dialog_label(message));
    if (original_changed) {
        gtk_box_append(GTK_BOX(shell.content),
                       dialog_label("The original file is missing or changed. Restoring keeps the buffer in conflict state."));
    }
    dialog_add_button(&shell, "Later", GTK_RESPONSE_CANCEL);
    dialog_add_button(&shell, "Discard Recovery", RESPONSE_DISCARD);
    dialog_add_button(&shell, "Restore", RESPONSE_RESTORE);
    response = dialog_run(&shell);
    dialog_shell_destroy(&shell);
    if (response == RESPONSE_RESTORE) {
        return LMME_RECOVERY_CHOICE_RESTORE;
    }
    if (response == RESPONSE_DISCARD) {
        return LMME_RECOVERY_CHOICE_DISCARD;
    }
    return LMME_RECOVERY_CHOICE_LATER;
}

LmmeExternalConflictChoice
lmme_dialog_external_conflict(GtkWindow *parent,
                              const char *path,
                              gboolean file_exists)
{
    enum {
        RESPONSE_RELOAD = 1,
        RESPONSE_SAVE_AS = 2,
        RESPONSE_OVERWRITE = 3
    };
    g_autofree char *message = g_strdup_printf(
        file_exists ? "File changed outside the editor:\n%s"
                    : "File was deleted outside the editor:\n%s",
        path != NULL ? path : "Unknown file");
    DialogShell shell = dialog_shell_new(parent, "External File Conflict");
    int response;

    gtk_box_append(GTK_BOX(shell.content), dialog_label(message));
    dialog_add_button(&shell, "Keep Local Changes", GTK_RESPONSE_CANCEL);
    dialog_add_button(&shell, "Save As", RESPONSE_SAVE_AS);
    dialog_add_button(&shell, "Overwrite Disk", RESPONSE_OVERWRITE);
    if (file_exists) {
        dialog_add_button(&shell, "Reload from Disk", RESPONSE_RELOAD);
    }
    response = dialog_run(&shell);
    dialog_shell_destroy(&shell);
    if (response == RESPONSE_RELOAD) {
        return LMME_EXTERNAL_CONFLICT_RELOAD;
    }
    if (response == RESPONSE_SAVE_AS) {
        return LMME_EXTERNAL_CONFLICT_SAVE_AS;
    }
    if (response == RESPONSE_OVERWRITE) {
        return LMME_EXTERNAL_CONFLICT_OVERWRITE;
    }
    return LMME_EXTERNAL_CONFLICT_KEEP_LOCAL;
}
