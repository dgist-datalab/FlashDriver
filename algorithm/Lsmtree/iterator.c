#include "iterator.h"
#include "nocpy.h"
#include <stdio.h>
#include <stdlib.h>
#include "../../include/settings.h"

extern lsmtree LSM;

iter *iter_init(level *lev, KEYT prefix){
	run_t *target_run=LSM.lop->find_run(lev,prefix);
	if(target_run!=NULL){
		return NULL;
	}
	iter *res=(iter*)malloc(sizeof(iter));
	res->offset=-1;
	res->run_idx=LSM.lop->run_idx(lev,target_run);
	res->status=IT_INIT;
	res->level_idx=lev->idx;
	fdriver_lock_init(&res->mtx,1);
	return res;
}

int iter_copy_next(iter *it, KEYT prefix, char *ptr, int *_offset, int max){
	if(!it || it->status==IT_FINISH) return IT_R_NODATA;
	if(!fdriver_try_lock(&it->mtx)) return IT_R_FLYING;
	fdriver_unlock(&it->mtx);

	if(it->offset==-1){
		it->offset=LSM.lop->find_idx_lower_bound(it->buffer,prefix);
	}
	KEYT target;
	LSM.lop->run_key_at(it->buffer,it->offset,&target);

	if(!target.key) return IT_R_NODATA;
	if(!KEYPREFIXCHECK(target,prefix)){
		it->status=IT_FINISH;
		return IT_R_NODATA;
	}
	
	int offset=*_offset;
	if(offset+1+target.len>=max) return IT_R_NOSPACE;

	memcpy(ptr,&target.len,sizeof(uint8_t));
	memcpy(ptr,target.key,target.len);
	
	*_offset=offset+1+target.len;

	//success
	if(LSM.lop->is_last_key(it->buffer,++it->offset)) return IT_R_READNEED;
	return IT_R_SUCCESS;
}

int iter_free(iter *it){
	fdriver_destroy(&it->mtx);
	free(it->buffer);
	free(it);
	return 1;
}

typedef struct iter_params{
	value_set *target;
	fdriver_lock_t *pmtx;
	iterator *it;
	char **data;
}iter_params;

void *iter_end_req(algo_req *req){
	iter_params *params=(iter_params*)req->params;
	iterator *it=params->it;
	if(!ISNOCPY(LSM.setup_values)){
		memcpy(*params->data,params->target->value, PAGESIZE);
	}
	it->status=IT_READDONE;
	fdriver_unlock(params->pmtx);

	free(req);
	return NULL;
}

int iter_read_next_data(iter *it, KEYT prefix){
	run_t *target=LSM.lop->get_run_idx(LSM.disk[it->level_idx],it->run_idx);
	if(it->status!=IT_INIT && KEYCMP(prefix,target->key)<0){
		it->status=IT_FINISH;
		return IT_R_NODATA;
	}
	else if(it->status==IT_INIT){
		it->buffer=(char*)malloc(PAGESIZE);
	}

	if(it->level_idx>LSM.LEVELCACHING){
		algo_req *iter_req=(algo_req *)malloc(sizeof(algo_req));
		iter_params *params=(iter_params*)malloc(sizeof(iter_params));
		params->target=inf_get_valueset(NULL,FS_MALLOC_R,PAGESIZE);
		params->pmtx=&it->mtx;
		params->data=&it->buffer;
		params->it=it;

		iter_req->parents=NULL;
		iter_req->type=HEADERR;
		iter_req->params=(void *)params;
		iter_req->end_req=iter_end_req;
		
		it->status=IT_FLYING;
		fdriver_lock(&it->mtx);
		if(ISNOCPY(LSM.setup_values)){
			char *table=(char*)nocpy_pick(target->pbn);
			memcpy(it->buffer,table,PAGESIZE);
		}
		LSM.li->read(target->pbn,PAGESIZE,params->target,ASYNC,iter_req);	
	}
	else{
		memcpy(it->buffer,target->level_caching_data,PAGESIZE);
	}
	it->run_idx++;

	return IT_R_SUCCESS;
}
