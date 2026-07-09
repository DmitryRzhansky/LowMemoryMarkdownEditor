#ifndef LMME_COMMAND_COMMAND_H
#define LMME_COMMAND_COMMAND_H

#include <glib.h>

typedef enum {
    LMME_COMMAND_CATEGORY_FILE,
    LMME_COMMAND_CATEGORY_EDIT,
    LMME_COMMAND_CATEGORY_VIEW,
    LMME_COMMAND_CATEGORY_BUFFER,
    LMME_COMMAND_CATEGORY_WORKSPACE,
    LMME_COMMAND_CATEGORY_HELP
} LmmeCommandCategory;

typedef struct {
    const char *id;
    const char *action_name;
    const char *label;
    LmmeCommandCategory category;
    const char *default_accel;
} LmmeCommandDef;

#endif
