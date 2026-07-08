#include "editor.h"

#include "util.h"

static GtkCssProvider *editor_font_provider = NULL;
static gboolean editor_font_provider_added = FALSE;

static int
clamp_int(int value, int min, int max)
{
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static char *
css_escape_string(const char *value)
{
    GString *escaped = g_string_new(NULL);
    const char *source = value;

    if (source == NULL || source[0] == '\0') {
        source = "monospace";
    }

    for (const char *cursor = source; *cursor != '\0'; cursor++) {
        if (*cursor == '\\' || *cursor == '"') {
            g_string_append_c(escaped, '\\');
        }
        g_string_append_c(escaped, *cursor);
    }

    return g_string_free(escaped, FALSE);
}

void
lmme_editor_apply_font_css(const LmmeConfig *config)
{
    const char *family = "monospace";
    int font_size = LMME_EDITOR_FONT_SIZE_DEFAULT;
    g_autofree char *escaped_family = NULL;
    g_autofree char *css = NULL;

    if (config == NULL) {
        return;
    }

    if (config->font_family != NULL && config->font_family[0] != '\0') {
        family = config->font_family;
    }

    font_size = clamp_int(config->font_size,
                          LMME_EDITOR_FONT_SIZE_MIN,
                          LMME_EDITOR_FONT_SIZE_MAX);
    escaped_family = css_escape_string(family);
    css = g_strdup_printf(".editor-view,.editor-view text{font-family:\"%s\";font-size:%dpx;}",
                          escaped_family,
                          font_size);

    if (editor_font_provider == NULL) {
        editor_font_provider = gtk_css_provider_new();
    }

    gtk_css_provider_load_from_string(editor_font_provider, css);

    if (!editor_font_provider_added) {
        GdkDisplay *display = gdk_display_get_default();

        if (display != NULL) {
            gtk_style_context_add_provider_for_display(display,
                                                       GTK_STYLE_PROVIDER(editor_font_provider),
                                                       GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
            editor_font_provider_added = TRUE;
        }
    }
}

GtkWidget *
lmme_editor_create_view(GtkSourceBuffer **out_buffer, const LmmeConfig *config)
{
    GtkSourceLanguageManager *manager = gtk_source_language_manager_get_default();
    GtkSourceLanguage *language = gtk_source_language_manager_get_language(manager, "markdown");
    GtkSourceBuffer *buffer = language != NULL ? gtk_source_buffer_new_with_language(language) : gtk_source_buffer_new(NULL);
    GtkWidget *view = gtk_source_view_new_with_buffer(buffer);

    gtk_widget_add_css_class(view, "editor-view");

    gtk_source_view_set_show_line_numbers(GTK_SOURCE_VIEW(view), config->line_numbers);
    gtk_source_view_set_tab_width(GTK_SOURCE_VIEW(view), 4);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view), 14);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(view), 14);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(view), 10);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(view), 10);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), config->word_wrap ? GTK_WRAP_WORD_CHAR : GTK_WRAP_NONE);
    gtk_widget_set_hexpand(view, TRUE);
    gtk_widget_set_vexpand(view, TRUE);

    if (out_buffer != NULL) {
        *out_buffer = buffer;
    } else {
        g_object_unref(buffer);
    }

    return view;
}

static gboolean
on_editor_zoom_key_pressed(GtkEventControllerKey *controller,
                           guint keyval,
                           guint keycode,
                           GdkModifierType state,
                           gpointer user_data)
{
    GActionGroup *actions = user_data;
    (void)controller;
    (void)keycode;

    if ((state & GDK_CONTROL_MASK) == 0) {
        return FALSE;
    }

    if (keyval == GDK_KEY_equal || keyval == GDK_KEY_plus || keyval == GDK_KEY_KP_Add) {
        g_action_group_activate_action(actions, "zoom-in", NULL);
        return TRUE;
    }

    if (keyval == GDK_KEY_minus || keyval == GDK_KEY_underscore || keyval == GDK_KEY_KP_Subtract) {
        g_action_group_activate_action(actions, "zoom-out", NULL);
        return TRUE;
    }

    if (keyval == GDK_KEY_0 || keyval == GDK_KEY_KP_0) {
        g_action_group_activate_action(actions, "zoom-reset", NULL);
        return TRUE;
    }

    return FALSE;
}

void
lmme_editor_setup_zoom_keys(GtkWidget *view, GActionGroup *action_group)
{
    GtkEventController *key = gtk_event_controller_key_new();

    if (view == NULL || action_group == NULL) {
        return;
    }

    g_signal_connect(key, "key-pressed", G_CALLBACK(on_editor_zoom_key_pressed), action_group);
    gtk_widget_add_controller(view, key);
}

char *
lmme_editor_dup_text(GtkTextBuffer *buffer)
{
    GtkTextIter start;
    GtkTextIter end;
    gtk_text_buffer_get_start_iter(buffer, &start);
    gtk_text_buffer_get_end_iter(buffer, &end);
    return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
}

void
lmme_editor_get_cursor(GtkTextBuffer *buffer, int *line, int *column)
{
    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_mark(buffer, &iter, gtk_text_buffer_get_insert(buffer));
    if (line != NULL) {
        *line = gtk_text_iter_get_line(&iter) + 1;
    }
    if (column != NULL) {
        *column = gtk_text_iter_get_line_offset(&iter) + 1;
    }
}

void
lmme_editor_insert_text_at_cursor(GtkTextBuffer *buffer, const char *text)
{
    gtk_text_buffer_insert_at_cursor(buffer, text, -1);
}

void
lmme_editor_wrap_selection(GtkTextBuffer *buffer,
                           const char *prefix,
                           const char *suffix,
                           int cursor_offset_when_empty)
{
    GtkTextIter start;
    GtkTextIter end;

    if (gtk_text_buffer_get_selection_bounds(buffer, &start, &end)) {
        g_autofree char *selected = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
        g_autofree char *wrapped = g_strdup_printf("%s%s%s", prefix, selected, suffix);
        gtk_text_buffer_begin_user_action(buffer);
        gtk_text_buffer_delete(buffer, &start, &end);
        gtk_text_buffer_insert(buffer, &start, wrapped, -1);
        gtk_text_buffer_end_user_action(buffer);
    } else {
        GtkTextIter insert_iter;
        GtkTextIter cursor_iter;
        g_autofree char *text = g_strdup_printf("%s%s", prefix, suffix);
        gtk_text_buffer_get_iter_at_mark(buffer, &insert_iter, gtk_text_buffer_get_insert(buffer));
        gtk_text_buffer_begin_user_action(buffer);
        gtk_text_buffer_insert(buffer, &insert_iter, text, -1);
        gtk_text_buffer_get_iter_at_mark(buffer, &cursor_iter, gtk_text_buffer_get_insert(buffer));
        gtk_text_iter_backward_chars(&cursor_iter, cursor_offset_when_empty);
        gtk_text_buffer_place_cursor(buffer, &cursor_iter);
        gtk_text_buffer_end_user_action(buffer);
    }
}

gboolean
lmme_editor_find(GtkTextBuffer *buffer, const char *needle, gboolean from_cursor)
{
    GtkTextIter start;
    GtkTextIter match_start;
    GtkTextIter match_end;

    if (needle == NULL || needle[0] == '\0') {
        return FALSE;
    }

    if (from_cursor) {
        gtk_text_buffer_get_iter_at_mark(buffer, &start, gtk_text_buffer_get_insert(buffer));
    } else {
        gtk_text_buffer_get_start_iter(buffer, &start);
    }

    if (!gtk_text_iter_forward_search(&start, needle, GTK_TEXT_SEARCH_TEXT_ONLY, &match_start, &match_end, NULL)) {
        gtk_text_buffer_get_start_iter(buffer, &start);
        if (!gtk_text_iter_forward_search(&start, needle, GTK_TEXT_SEARCH_TEXT_ONLY, &match_start, &match_end, NULL)) {
            return FALSE;
        }
    }

    gtk_text_buffer_select_range(buffer, &match_start, &match_end);
    return TRUE;
}

gboolean
lmme_editor_replace_current(GtkTextBuffer *buffer, const char *needle, const char *replacement)
{
    GtkTextIter start;
    GtkTextIter end;
    g_autofree char *selected = NULL;

    if (needle == NULL || needle[0] == '\0') {
        return FALSE;
    }

    if (!gtk_text_buffer_get_selection_bounds(buffer, &start, &end)) {
        return lmme_editor_find(buffer, needle, TRUE);
    }

    selected = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    if (g_strcmp0(selected, needle) != 0) {
        return lmme_editor_find(buffer, needle, TRUE);
    }

    gtk_text_buffer_begin_user_action(buffer);
    gtk_text_buffer_delete(buffer, &start, &end);
    gtk_text_buffer_insert(buffer, &start, replacement != NULL ? replacement : "", -1);
    gtk_text_buffer_end_user_action(buffer);

    return TRUE;
}

guint
lmme_editor_word_count(GtkTextBuffer *buffer)
{
    g_autofree char *text = lmme_editor_dup_text(buffer);
    return lmme_word_count(text);
}
