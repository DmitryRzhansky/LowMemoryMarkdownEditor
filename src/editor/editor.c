#include "editor/editor.h"

#include "infra/util.h"

static GtkCssProvider *editor_font_provider = NULL;
static gboolean editor_font_provider_added = FALSE;

static GtkSourceStyleScheme *
find_dark_style_scheme(void)
{
    static const char *const preferred_scheme_ids[] = {
        "Adwaita-dark",
        "oblivion",
    };
    GtkSourceStyleSchemeManager *manager = gtk_source_style_scheme_manager_get_default();

    for (guint i = 0; i < G_N_ELEMENTS(preferred_scheme_ids); i++) {
        GtkSourceStyleScheme *scheme =
            gtk_source_style_scheme_manager_get_scheme(manager, preferred_scheme_ids[i]);

        if (scheme != NULL) {
            return scheme;
        }
    }
    return NULL;
}

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
    GtkSourceStyleScheme *scheme = find_dark_style_scheme();

    gtk_widget_add_css_class(view, "editor-view");

    if (scheme != NULL) {
        gtk_source_buffer_set_style_scheme(buffer, scheme);
    }

    gtk_source_view_set_show_line_numbers(GTK_SOURCE_VIEW(view), config->line_numbers);
    gtk_source_view_set_tab_width(GTK_SOURCE_VIEW(view), 4);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view), 12);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(view), 12);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(view), 8);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(view), 8);
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

guint
lmme_editor_word_count(GtkTextBuffer *buffer)
{
    g_autofree char *text = lmme_editor_dup_text(buffer);
    return lmme_word_count(text);
}
