#include "level.h"
#include "lsmtree.h"
#include "../../include/sem_lock.h"

enum it_status{
	IT_INIT, IT_NODATA, IT_FLYING,  IT_READDONE, IT_FINISH
};

enum it_return_status{
	IT_R_NODATA, IT_R_FLYING, IT_R_READNEED, IT_R_NOSPACE, IT_R_SUCCESS
};

typedef struct iterator{
	uint32_t run_idx; //index of run in level
	uint32_t level_idx;
	uint32_t offset; //nxt key offset in run
	char *buffer; //run data
	char status;
	fdriver_lock_t mtx;
}iter;

iter *iter_init(level *, KEYT prefix);
int iter_read_next_data(iter *,KEYT prefix);
int iter_copy_next(iter *, KEYT prefix, char *ptr, int *offset, int max);
int iter_free(iter *);
