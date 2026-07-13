#include "command/command_actions.h"

#include "command/command_handlers.h"
#include "command/command_registry.h"
#include "command/command_enabled.h"
#include "command/command_actions_test.h"

#include "app/app.h"

#ifdef LMME_TESTING
static guint command_actions_test_refresh_count = 0;
#endif

static gboolean
command_actions_refresh_idle_cb(gpointer user_data)
{
    LmmeApp *app = user_data;

    if (app != NULL) {
        app->command_refresh_source_id = 0;
        lmme_command_actions_refresh(app);
    }
    return G_SOURCE_REMOVE;
}

void
lmme_command_actions_cancel_refresh(LmmeApp *app)
{
    if (app == NULL || app->command_refresh_source_id == 0) {
        return;
    }
    g_source_remove(app->command_refresh_source_id);
    app->command_refresh_source_id = 0;
}

void
lmme_command_actions_request_refresh(LmmeApp *app)
{
    if (app == NULL || app->gtk_app == NULL || app->scheduling_blocked) {
        return;
    }
    if (app->command_refresh_source_id != 0) {
        return;
    }
    app->command_refresh_source_id = g_idle_add(command_actions_refresh_idle_cb, app);
}

static void
dispatch_command(const LmmeCommandDef *command,
                 GSimpleAction *action,
                 GVariant *parameter,
                 LmmeApp *app)
{
    gboolean handled = FALSE;

    switch (lmme_command_handler_domain(command->handler)) {
    case LMME_COMMAND_DOMAIN_FILE:
        handled = lmme_command_handle_file(command->handler, action, parameter, app);
        break;
    case LMME_COMMAND_DOMAIN_WORKSPACE:
        handled = lmme_command_handle_workspace(command->handler, action, parameter, app);
        break;
    case LMME_COMMAND_DOMAIN_DOCUMENT:
        handled = lmme_command_handle_document(command->handler, action, parameter, app);
        break;
    case LMME_COMMAND_DOMAIN_EDITOR:
        handled = lmme_command_handle_editor(command->handler, action, parameter, app);
        break;
    case LMME_COMMAND_DOMAIN_VIEW:
        handled = lmme_command_handle_view(command->handler, action, parameter, app);
        break;
    case LMME_COMMAND_DOMAIN_NONE:
        break;
    }

    if (!handled) {
        g_warning("No handler for command: %s", command->id);
    }
}

static void
on_registered_action_activate(GSimpleAction *action,
                              GVariant *parameter,
                              gpointer user_data)
{
    const LmmeCommandDef *command = g_object_get_data(G_OBJECT(action),
                                                       "lmme-command-definition");

    if (command != NULL) {
        dispatch_command(command, action, parameter, user_data);
    }
}

void
lmme_command_actions_register(LmmeApp *app)
{
    gsize count = 0;
    const LmmeCommandDef *commands = lmme_command_registry_get_all(&count);
    g_autoptr(GError) error = NULL;

    if (!lmme_command_registry_validate(&error)) {
        g_error("Invalid command catalog: %s",
                error != NULL ? error->message : "unknown error");
    }

    for (gsize i = 0; i < count; i++) {
        const LmmeCommandDef *command = &commands[i];
        g_autoptr(GSimpleAction) action = g_simple_action_new(command->action_name,
                                                              NULL);
        g_autofree char *detailed_action = g_strdup_printf("app.%s",
                                                           command->action_name);

        g_object_set_data(G_OBJECT(action),
                          "lmme-command-definition",
                          (gpointer)command);
        g_signal_connect(action,
                         "activate",
                         G_CALLBACK(on_registered_action_activate),
                         app);
        if (command->is_enabled != NULL) {
            g_simple_action_set_enabled(action, command->is_enabled(app));
        }
        g_action_map_add_action(G_ACTION_MAP(app->gtk_app), G_ACTION(action));
        gtk_application_set_accels_for_action(app->gtk_app,
                                              detailed_action,
                                              command->default_accels);
    }
    lmme_command_actions_refresh(app);
}

void
lmme_command_actions_refresh(LmmeApp *app)
{
    gsize count = 0;
    const LmmeCommandDef *commands = NULL;
    LmmeCommandContext context;

    if (app == NULL || app->gtk_app == NULL) {
        return;
    }

#ifdef LMME_TESTING
    command_actions_test_refresh_count++;
#endif

    commands = lmme_command_registry_get_all(&count);
    lmme_command_context_fill_from_app(&context, app);
    for (gsize i = 0; i < count; i++) {
        const LmmeCommandDef *command = &commands[i];
        GAction *action = g_action_map_lookup_action(G_ACTION_MAP(app->gtk_app),
                                                     command->action_name);
        gboolean enabled = FALSE;

        if (action == NULL) {
            continue;
        }
        if (command->is_enabled != NULL) {
            enabled = command->is_enabled(app);
        } else {
            enabled = lmme_command_enabled_for_handler(command->handler, &context);
        }
        g_simple_action_set_enabled(G_SIMPLE_ACTION(action), enabled);
    }
}

#ifdef LMME_TESTING

guint
lmme_command_actions_test_refresh_count(void)
{
    return command_actions_test_refresh_count;
}

void
lmme_command_actions_test_reset_refresh_count(void)
{
    command_actions_test_refresh_count = 0;
}

#endif
