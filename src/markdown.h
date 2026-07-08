#ifndef LMME_MARKDOWN_H
#define LMME_MARKDOWN_H

#include <glib.h>

char *lmme_markdown_to_preview_text(const char *markdown, gboolean hide_frontmatter);

#endif
