#ifndef LMME_COMMAND_COMMAND_REGISTRY_H
#define LMME_COMMAND_COMMAND_REGISTRY_H

#include "command/command.h"

const LmmeCommandDef *lmme_command_registry_get_all(gsize *out_count);
const LmmeCommandDef *lmme_command_registry_find(const char *id);
const LmmeCommandDef *lmme_command_registry_find_action(const char *action_name);
gboolean lmme_command_registry_validate(GError **error);

#endif
