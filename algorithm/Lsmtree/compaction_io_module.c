#include "compaction.h"
#include "lsmtree_scheduling.h"
#include "lsmtree.h"
#include "nocpy.h"
#ifdef KVSSD
extern KEYT key_min, key_max;
#endif
extern lsmtree LSM;

void compaction_data_write(leveling_node* lnode){	
	value_set **data_sets=skiplist_make_valueset(lnode->mem,LSM.disk[0],&lnode->start,&lnode->end);
	if(LSM.comp_opt==PIPE){
		lsm_io_sched_push(SCHED_FLUSH,(void*)data_sets);//make flush background job
		return;
	}
	for(int i=0; data_sets[i]!=NULL; i++){
		LSM.last_level_comp_term++;
		algo_req *lsm_req=(algo_req*)malloc(sizeof(algo_req));
		lsm_params *params=(lsm_params*)malloc(sizeof(lsm_params));
		params->lsm_type=DATAW;
		params->value=data_sets[i];
		lsm_req->parents=NULL;
		lsm_req->params=(void*)params;
		lsm_req->end_req=lsm_end_req;
		lsm_req->rapid=true;
		lsm_req->type=DATAW;
		if(params->value->dmatag==-1){
			abort();
		}
		LSM.li->write(CONVPPA(data_sets[i]->ppa),PAGESIZE,params->value,ASYNC,lsm_req);
	}
	free(data_sets);
}

void compaction_htable_write_insert(level *target,run_t *entry,bool isbg){
#ifdef KVSSD
	uint32_t ppa=getPPA(HEADER,key_min,true);
#else
	uint32_t ppa=getPPA(HEADER,0,true);//set ppa;
#endif
	
	entry->pbn=ppa;


	if(LSM.nocpy){
		nocpy_copy_from_change((char*)entry->cpt_data->sets,ppa);
		entry->cpt_data->sets=NULL;
	}
	LSM.lop->insert(target,entry);
	if(isbg){
		if(LSM.comp_opt==PIPE)
			compaction_bg_htable_write(entry->pbn,entry->cpt_data,entry->key);
		else
			abort();
	}else{
		compaction_htable_write(entry->pbn,entry->cpt_data,entry->key);
	}
	LSM.lop->release_run(entry);
}

uint32_t compaction_htable_write(ppa_t ppa,htable *input, KEYT lpa){
	algo_req *areq=(algo_req*)malloc(sizeof(algo_req));
	lsm_params *params=(lsm_params*)malloc(sizeof(lsm_params));
	areq->parents=NULL;
	areq->rapid=false;
	params->lsm_type=HEADERW;
	if(input->origin){
		params->value=input->origin;
	}else{
		params->value=inf_get_valueset(NULL,FS_MALLOC_W,PAGESIZE);
	}


	params->htable_ptr=input;
	areq->end_req=lsm_end_req;
	areq->params=(void*)params;
	areq->type=HEADERW;
	params->ppa=ppa;
	
	LSM.li->write(ppa,PAGESIZE,params->value,ASYNC,areq);
	//printf("%u\n",ppa);
	return ppa;
}

void compaction_htable_read(run_t *ent,PTR* value){
	algo_req *areq=(algo_req*)malloc(sizeof(algo_req));
	lsm_params *params=(lsm_params*)malloc(sizeof(lsm_params));

	params->lsm_type=HEADERR;
	//valueset_assign
	params->value=inf_get_valueset(NULL,FS_MALLOC_R,PAGESIZE);
	params->target=value;
	params->ppa=ent->pbn;
	areq->parents=NULL;
	areq->end_req=lsm_end_req;
	areq->params=(void*)params;
	areq->type_lower=0;
	areq->rapid=false;
	areq->type=HEADERR;

	if(LSM.nocpy) ent->cpt_data->nocpy_table=nocpy_pick(ent->pbn);
	//printf("R %u\n",ent->pbn);
	LSM.li->read(ent->pbn,PAGESIZE,params->value,ASYNC,areq);
	return;
}
void compaction_bg_htable_bulkread(run_t **r,fdriver_lock_t **locks){
	void **argv=(void**)malloc(sizeof(void*)*2);
	argv[0]=(void*)r;
	argv[1]=(void*)locks;
	lsm_io_sched_push(SCHED_HREAD,(void*)argv);
}


uint32_t compaction_bg_htable_write(ppa_t ppa,htable *input, KEYT lpa){
	algo_req *areq=(algo_req*)malloc(sizeof(algo_req));
	lsm_params *params=(lsm_params*)malloc(sizeof(lsm_params));
	areq->parents=NULL;
	areq->rapid=false;
	params->lsm_type=HEADERW;
	if(input->origin){
	//	printf("can't be - %s:%d\n",__FILE__,__LINE__);
		params->value=input->origin;
	}else{
		//this logic should process when the memtable's header write
		params->value=inf_get_valueset(NULL,FS_MALLOC_W,PAGESIZE);
	}

	params->htable_ptr=input;
	areq->end_req=lsm_end_req;
	areq->params=(void*)params;
	areq->type=HEADERW;
	params->ppa=ppa;
	
	lsm_io_sched_push(SCHED_HWRITE,(void*)areq);
	return ppa;
}
/*
extern uint32_t hw_comp_read_cnt;
void *hw_end_req(struct algo_req *const req){
	lsm_params *params=(lsm_params*)req->params;
	switch(req->type){
		case HEADERR:
			hw_comp_read_cnt++;
			break;
		case HEADERW:
			break;
	}
	free(params);
	free(req);
	return NULL;
}

uint32_t compaction_htable_hw_read(run_t *ent){
	ent->cpt_data=htable_assign(NULL,true);
	ent->cpt_data->iscached=0;
	if(!ent->iscompactioning) ent->iscompactioning=COMP;

	algo_req *areq=(algo_req*)malloc(sizeof(algo_req));
	lsm_params *params=(lsm_params*)malloc(sizeof(lsm_params));

	params->lsm_type=HEADERR;
	//valueset_assign
	params->value=inf_get_valueset(NULL,FS_MALLOC_R,PAGESIZE);
	params->ppa=ent->pbn;
	areq->parents=NULL;
	areq->end_req=hw_end_req;
	areq->params=(void*)params;
	areq->type_lower=0;
	areq->rapid=false;
	areq->type=HEADERR;

	LSM.li->read(ent->pbn,PAGESIZE,params->value,ASYNC,areq);

	return ent->cpt_data->origin->dmatag;
}*/
