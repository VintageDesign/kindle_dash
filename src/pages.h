#ifndef KD_PAGES_H
#define KD_PAGES_H

#include "page.h"

/* Build the page list from [app] pages= in cfg. Returns a GPtrArray of Page*. */
GPtrArray *pages_load(GKeyFile *cfg);

#endif
