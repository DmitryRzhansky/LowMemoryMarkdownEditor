#include "command/command_shortcuts.h"

#include "app/app.h"

void
lmme_command_shortcuts_apply(LmmeApp *app)
{
    gtk_application_set_accels_for_action(app->gtk_app, "app.open", (const char *[]){"<Ctrl>O", NULL});
    gtk_application_set_accels_for_action(app->gtk_app, "app.save", (const char *[]){"<Ctrl>S", NULL});
    gtk_application_set_accels_for_action(app->gtk_app, "app.new-file", (const char *[]){"<Ctrl>N", NULL});
    gtk_application_set_accels_for_action(app->gtk_app, "app.new-folder", (const char *[]){"<Ctrl><Shift>N", NULL});
    gtk_application_set_accels_for_action(app->gtk_app, "app.close-tab", (const char *[]){"<Ctrl>W", NULL});
    gtk_application_set_accels_for_action(app->gtk_app, "app.undo", (const char *[]){"<Ctrl>Z", NULL});
    gtk_application_set_accels_for_action(app->gtk_app, "app.redo", (const char *[]){"<Ctrl><Shift>Z", NULL});
    gtk_application_set_accels_for_action(app->gtk_app, "app.find", (const char *[]){"<Ctrl>F", NULL});
    gtk_application_set_accels_for_action(app->gtk_app, "app.replace", (const char *[]){"<Ctrl>H", NULL});
    gtk_application_set_accels_for_action(app->gtk_app, "app.bold", (const char *[]){"<Ctrl>B", NULL});
    gtk_application_set_accels_for_action(app->gtk_app, "app.italic", (const char *[]){"<Ctrl>I", NULL});
    gtk_application_set_accels_for_action(app->gtk_app, "app.insert-link", (const char *[]){"<Ctrl>K", NULL});
    gtk_application_set_accels_for_action(app->gtk_app, "app.toggle-preview", (const char *[]){"<Ctrl>P", NULL});
    gtk_application_set_accels_for_action(app->gtk_app, "app.zoom-in", (const char *[]){"<Ctrl>plus", "<Ctrl>equal", "<Ctrl><Shift>plus", "<Ctrl><Shift>equal", "<Ctrl>KP_Add", NULL});
    gtk_application_set_accels_for_action(app->gtk_app, "app.zoom-out", (const char *[]){"<Ctrl>minus", "<Ctrl>KP_Subtract", NULL});
    gtk_application_set_accels_for_action(app->gtk_app, "app.zoom-reset", (const char *[]){"<Ctrl>0", "<Ctrl>KP_0", NULL});
    gtk_application_set_accels_for_action(app->gtk_app, "app.focus-mode", (const char *[]){"F11", NULL});
    gtk_application_set_accels_for_action(app->gtk_app, "app.next-tab", (const char *[]){"<Ctrl>Tab", NULL});
    gtk_application_set_accels_for_action(app->gtk_app, "app.previous-tab", (const char *[]){"<Ctrl><Shift>Tab", NULL});
}
