#ifndef LMME_FEATURES_IMAGE_INSERT_TEST_H
#define LMME_FEATURES_IMAGE_INSERT_TEST_H

#include <glib.h>

typedef enum {
    LMME_IMAGE_INSERT_PREPARING,
    LMME_IMAGE_INSERT_FILE_CREATED,
    LMME_IMAGE_INSERT_COMMITTED,
    LMME_IMAGE_INSERT_FINISHED
} LmmeImageInsertState;

gboolean lmme_image_insert_should_rollback_destination(LmmeImageInsertState state,
                                                       gboolean destination_created_by_request);
void lmme_image_insert_request_mark_file_created(LmmeImageInsertState *state,
                                                 gboolean *destination_created_by_request,
                                                 gboolean destination_existed_before);
void lmme_image_insert_request_mark_committed(LmmeImageInsertState *state);
void lmme_image_insert_rollback_destination_if_needed(LmmeImageInsertState *state,
                                                      gboolean *destination_created_by_request,
                                                      const char *destination_path);

#endif
