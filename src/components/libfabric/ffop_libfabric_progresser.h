#ifndef _FFOP_LIBFABRIC_PROGRESSER_H_
#define _FFOP_LIBFABRIC_PROGRESSER_H_

#include "ffop_libfabric.h"
#include "ffop.h"

int ffop_libfabric_progresser_init();
int ffop_libfabric_progresser_finalize();

int ffop_libfabric_progresser_track(ffop_t * op, MPI_Request req);
int ffop_libfabric_progresser_progress(ffop_t ** ready_list);


#endif /* _FFOP_LIBFABRIC_PROGRESSER_H_ */

