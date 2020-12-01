#include "lsmtree.h"
#include "compaction.h"
#include "lsmtree_transaction.h"
extern my_tm _tm;
extern lsmtree LSM;

enum {
	LOGICALDEFRAG, //make LSM have a level
	PHYSICALDEFRAG //make physcially sequential address;
};

static void __lsm_check_logical_defrag();
static void __lsm_check_physical_defrag();
uint32_t lsm_defragmentation(request *const req){
	switch(req->offset){
		case PHYSICALDEFRAG:
		case LOGICALDEFRAG:
			compaction_wait_jobs();
			compaction_force();
			__lsm_check_logical_defrag();
			if(req->offset==LOGICALDEFRAG){
				break;
			}
			__lsm_check_physical_defrag();
		default:
			printf("Not defined type!!!");
			abort();
			break;
	}
}

static void __lsm_check_logical_defrag(){
	for(int i=0; i<LSM.LEVELN-1; i++){
		if(LSM.disk[i]->n_num){
			printf("level %d has entry!!!\n", i);
			abort();
		}
	}
	return;
}

#define BULKSET 128
typedef struct temp_read_st{

}temp_read_st;
static void __lsm_check_physical_defrag(){
	level *t=LSM.disk[LSM.LEVELN-1];
	run_t *run_set[BULKSET+1];
	run_t *now;
	char *run_data_set[BULKSET+1];
	int idx=0;

	uint32_t max_level_num=t->m_num;
	lev_iter *iter=LSM.lop->get_iter(t, t->start, t->end);
	for(uint32_t i=0; i<max_level/BULSET+(max_level%BULKSET?1:0); i++){
		idx=0;
		while((now=LSM.lop->iter_nxt(iter))){
			run_data_set[idx++]=now;
		}
	}
}
