#include <gtk/gtk.h>

#include "infra/dialogs.h"

typedef struct {
    const char *const *destructive_labels;
    guint destructive_count;
    const char *safe_label;
    const char *activate_label;
    guint attempts;
    gboolean inspected;
} DialogStyleCheck;

static GtkWidget *
find_button_with_label(GtkWidget *widget, const char *label)
{
    if (GTK_IS_BUTTON(widget) &&
        g_strcmp0(gtk_button_get_label(GTK_BUTTON(widget)), label) == 0) {
        return widget;
    }
    for (GtkWidget *child = gtk_widget_get_first_child(widget); child != NULL;
         child = gtk_widget_get_next_sibling(child)) {
        GtkWidget *match = find_button_with_label(child, label);
        if (match != NULL) {
            return match;
        }
    }
    return NULL;
}

static GtkWidget *
find_widget_with_css_class(GtkWidget *widget, const char *css_class)
{
    if (gtk_widget_has_css_class(widget, css_class)) {
        return widget;
    }
    for (GtkWidget *child = gtk_widget_get_first_child(widget); child != NULL;
         child = gtk_widget_get_next_sibling(child)) {
        GtkWidget *match = find_widget_with_css_class(child, css_class);
        if (match != NULL) {
            return match;
        }
    }
    return NULL;
}

static gboolean
inspect_and_activate_dialog(gpointer user_data)
{
    DialogStyleCheck *check = user_data;
    GListModel *windows = gtk_window_get_toplevels();

    check->attempts++;
    for (guint i = 0; i < g_list_model_get_n_items(windows); i++) {
        g_autoptr(GtkWindow) window = g_list_model_get_item(windows, i);
        GtkWidget *widget = GTK_WIDGET(window);
        GtkWidget *activate = NULL;

        if (!gtk_widget_get_visible(widget) ||
            !gtk_widget_has_css_class(widget, "lmme-dialog")) {
            continue;
        }

        g_assert_true(gtk_widget_has_css_class(widget, "dialog"));
        g_assert_nonnull(find_widget_with_css_class(widget, "dialog-root"));
        g_assert_nonnull(find_widget_with_css_class(widget, "dialog-content"));
        g_assert_nonnull(find_widget_with_css_class(widget, "dialog-actions"));
        for (guint j = 0; j < check->destructive_count; j++) {
            GtkWidget *button = find_button_with_label(widget, check->destructive_labels[j]);
            g_assert_nonnull(button);
            g_assert_true(gtk_widget_has_css_class(button, "destructive-action"));
        }
        if (check->safe_label != NULL) {
            GtkWidget *button = find_button_with_label(widget, check->safe_label);
            g_assert_nonnull(button);
            g_assert_false(gtk_widget_has_css_class(button, "destructive-action"));
        }

        activate = find_button_with_label(widget, check->activate_label);
        g_assert_nonnull(activate);
        check->inspected = TRUE;
        g_signal_emit_by_name(activate, "clicked");
        return G_SOURCE_REMOVE;
    }

    g_assert_cmpuint(check->attempts, <, 50);
    return G_SOURCE_CONTINUE;
}

static void
schedule_dialog_check(DialogStyleCheck *check)
{
    g_idle_add(inspect_and_activate_dialog, check);
}

static void
test_dialog_semantic_action_classes(void)
{
    const char *delete_labels[] = {"Delete"};
    const char *delete_open_labels[] = {"Delete and Discard Changes"};
    const char *replace_labels[] = {"Replace"};
    const char *close_labels[] = {"Close"};
    const char *discard_local_labels[] = {"Discard Local Changes"};
    const char *discard_recovery_labels[] = {"Discard Recovery"};
    const char *conflict_labels[] = {"Overwrite Disk", "Reload from Disk"};
    DialogStyleCheck info = {.safe_label = "Close", .activate_label = "Close"};
    DialogStyleCheck prompt = {.safe_label = "Save", .activate_label = "Cancel"};
    DialogStyleCheck delete = {.destructive_labels = delete_labels,
                               .destructive_count = G_N_ELEMENTS(delete_labels),
                               .safe_label = "Cancel",
                               .activate_label = "Delete"};
    DialogStyleCheck delete_open = {.destructive_labels = delete_open_labels,
                                    .destructive_count = G_N_ELEMENTS(delete_open_labels),
                                    .safe_label = "Cancel",
                                    .activate_label = "Delete and Discard Changes"};
    DialogStyleCheck replace = {.destructive_labels = replace_labels,
                                .destructive_count = G_N_ELEMENTS(replace_labels),
                                .safe_label = "Cancel",
                                .activate_label = "Replace"};
    DialogStyleCheck close = {.destructive_labels = close_labels,
                              .destructive_count = G_N_ELEMENTS(close_labels),
                              .safe_label = "Cancel",
                              .activate_label = "Close"};
    DialogStyleCheck discard_local = {
        .destructive_labels = discard_local_labels,
        .destructive_count = G_N_ELEMENTS(discard_local_labels),
        .safe_label = "Retry",
        .activate_label = "Discard Local Changes"};
    DialogStyleCheck discard_recovery = {
        .destructive_labels = discard_recovery_labels,
        .destructive_count = G_N_ELEMENTS(discard_recovery_labels),
        .safe_label = "Restore",
        .activate_label = "Discard Recovery"};
    DialogStyleCheck conflict = {.destructive_labels = conflict_labels,
                                 .destructive_count = G_N_ELEMENTS(conflict_labels),
                                 .safe_label = "Save As",
                                 .activate_label = "Reload from Disk"};
    g_autofree char *prompt_result = NULL;

    schedule_dialog_check(&info);
    lmme_dialog_info(NULL, "Information", "Detail");
    g_assert_true(info.inspected);

    schedule_dialog_check(&prompt);
    prompt_result = lmme_dialog_prompt_text(NULL, "Prompt", "Name", "note.md");
    g_assert_null(prompt_result);
    g_assert_true(prompt.inspected);

    schedule_dialog_check(&delete);
    g_assert_true(lmme_dialog_confirm_delete(NULL, NULL));
    g_assert_true(delete.inspected);

    schedule_dialog_check(&delete_open);
    g_assert_true(lmme_dialog_confirm_delete_open_documents(NULL, 1, 2));
    g_assert_true(delete_open.inspected);

    schedule_dialog_check(&replace);
    g_assert_true(lmme_dialog_confirm_overwrite(NULL, "/tmp/note.md"));
    g_assert_true(replace.inspected);

    schedule_dialog_check(&close);
    g_assert_true(lmme_dialog_confirm_close_unsaved(NULL, "note.md"));
    g_assert_true(close.inspected);

    schedule_dialog_check(&discard_local);
    g_assert_cmpint(lmme_dialog_resolve_save_failure(NULL,
                                                     "note.md",
                                                     "Save failed",
                                                     TRUE,
                                                     TRUE,
                                                     TRUE),
                    ==,
                    LMME_SAVE_FAILURE_DISCARD_LOCAL);
    g_assert_true(discard_local.inspected);

    schedule_dialog_check(&discard_recovery);
    g_assert_cmpint(lmme_dialog_choose_recovery(NULL, "/tmp/note.md", TRUE),
                    ==,
                    LMME_RECOVERY_CHOICE_DISCARD);
    g_assert_true(discard_recovery.inspected);

    schedule_dialog_check(&conflict);
    g_assert_cmpint(lmme_dialog_external_conflict(NULL, "/tmp/note.md", TRUE, TRUE),
                    ==,
                    LMME_EXTERNAL_CONFLICT_RELOAD);
    g_assert_true(conflict.inspected);
}

int
main(int argc, char **argv)
{
    g_setenv("GTK_A11Y", "none", TRUE);
    gtk_init();
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/dialogs/semantic-action-classes", test_dialog_semantic_action_classes);
    return g_test_run();
}
