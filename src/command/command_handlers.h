#ifndef LMME_COMMAND_COMMAND_HANDLERS_H
#define LMME_COMMAND_COMMAND_HANDLERS_H

#include <gio/gio.h>

#include "command/command.h"

typedef struct _LmmeApp LmmeApp;

typedef enum {
    LMME_COMMAND_DOMAIN_NONE = 0,
    LMME_COMMAND_DOMAIN_FILE,
    LMME_COMMAND_DOMAIN_WORKSPACE,
    LMME_COMMAND_DOMAIN_DOCUMENT,
    LMME_COMMAND_DOMAIN_EDITOR,
    LMME_COMMAND_DOMAIN_VIEW
} LmmeCommandDomain;

LmmeCommandDomain lmme_command_handler_domain(LmmeCommandHandler handler);

gboolean lmme_command_handle_file(LmmeCommandHandler handler,
                                  GSimpleAction *action,
                                  GVariant *parameter,
                                  LmmeApp *app);
gboolean lmme_command_handle_workspace(LmmeCommandHandler handler,
                                       GSimpleAction *action,
                                       GVariant *parameter,
                                       LmmeApp *app);
gboolean lmme_command_handle_document(LmmeCommandHandler handler,
                                      GSimpleAction *action,
                                      GVariant *parameter,
                                      LmmeApp *app);
gboolean lmme_command_handle_editor(LmmeCommandHandler handler,
                                    GSimpleAction *action,
                                    GVariant *parameter,
                                    LmmeApp *app);
gboolean lmme_command_handle_view(LmmeCommandHandler handler,
                                  GSimpleAction *action,
                                  GVariant *parameter,
                                  LmmeApp *app);

#endif
