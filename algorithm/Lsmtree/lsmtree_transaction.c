#include "lsmtree_transaction.h"
#include "transaction_table.h"
#include "page.h"
#include "lsmtree.h"
#include "nocpy.h"
#include "../../interface/interface.h"
#include "../../interface/koo_hg_inf.h"
#include "../../interface/koo_inf.h"
#include "../../bench/bench.h"
#include "variable.h"
#include "nocpy.h"
#include "level.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "transaction_table.h"

extern lsmtree LSM;
extern lmi LMI;
extern KEYT debug_key;
extern MeasureTime write_opt_time2[10];

//uint32_t debug_target_tid;
my_tm _tm;

bool range_delete_flag;
typedef struct transaction_write_params{
	value_set *value;
	fdriver_lock_t *lock;
}t_params;

typedef struct transaction_commit_params{
	uint32_t total_num;
	uint32_t done_num;
}t_cparams;

void* transaction_end_req(algo_req * const);
inline uint32_t __transaction_get(request *const);
ppa_t get_log_PPA(uint32_t type);
uint32_t gc_log();

static int temp_tid;

void checking_table_nonfull(){
	for(uint32_t i=0; i<_tm.ttb->full; i++){
		if(_tm.ttb->etr[i].status!=EMPTY){
			if(_tm.ttb->etr[i].status==NONFULLCOMPACTION){
				transaction_table_print(_tm.ttb, false);
				printf("??????\n");
				abort();
			}	
		}
	}
}

uint32_t transaction_init(uint32_t cached_size){
	uint32_t cached_entry_num=cached_size/PAGESIZE;
	//uint32_t max_KP_buffer_num=(7*K)/(DEFKEYLENGTH+4);
	uint32_t target_KP_buffer=4;

	transaction_table_init(&_tm.ttb, PAGESIZE, 0);
	cached_entry_num-=(target_KP_buffer*4096)/PAGESIZE;
	if(((int)cached_entry_num)<=0){
		printf("[WRANINIG!!]memory calculated miss!, memory log will be 0 %s:%d\n", __FILE__, __LINE__);
		cached_entry_num=0;
	}
	//uint32_t memory_log_num=cached_entry_num < _tm.ttb->full ? cached_entry_num : _tm.ttb->full;
	uint32_t memory_log_num=128;
	memory_log_num=cached_entry_num==0?2:memory_log_num;
	_tm.mem_log=memory_log_init(memory_log_num, transaction_evicted_write_entry);
	//_tm.mem_log=memory_log_init(2, transaction_evicted_write_entry);
	_tm.commit_KP=skiplist_init();
	//_tm.commit_etr=list_init();

	fdriver_mutex_init(&_tm.table_lock);
	_tm.t_pm.target=NULL;
	_tm.t_pm.active=NULL;
	_tm.t_pm.reserve=NULL;
	_tm.last_table=UINT_MAX;

	uint32_t table_size=(PAGESIZE/TABLE_ENTRY_SZ) * (2*DEFKEYLENGTH + TABLE_ENTRY_SZ);
	printf("\t|TRANSACTION WBM size :%u pages\n", (target_KP_buffer*4096)/PAGESIZE);
	printf("\t|TRANSACTION memor_log size :%u pages\n", memory_log_num);
	printf("\t|TRANSACTION table memory size :%u pages\n", table_size/PAGESIZE);
	return (cached_entry_num-memory_log_num-2-table_size/PAGESIZE) >  cached_size ? 0 : (cached_entry_num-memory_log_num-2-table_size/PAGESIZE);
}

uint32_t transaction_destroy(){
	transaction_table_destroy(_tm.ttb);
	return 1;
}

uint32_t transaction_start(request * const req){
	fdriver_lock(&_tm.table_lock);
	if(transaction_table_add_new(_tm.ttb, req->tid, 0)==UINT_MAX){
		printf("%s:%d can't add table!\n", __FILE__, __LINE__);
		abort();
	}
	fdriver_unlock(&_tm.table_lock);
	req->end_req(req);
	return 1;
}

uint32_t __trans_write(char *data, value_set *value, ppa_t ppa, uint32_t type, request *const req, bool sync){
	if(data && value){
		printf("%s:%d can't be!\n", __FILE__,__LINE__);
		abort();
	}
	//write log
	t_params *tp=(t_params*)malloc(sizeof(t_params));
	algo_req *areq=(algo_req*)malloc(sizeof(algo_req));
	tp->value=data? inf_get_valueset(data, FS_MALLOC_W, PAGESIZE) : value;
	if(ISNOCPY(LSM.setup_values)){
		nocpy_copy_from(tp->value->value, ppa);
	}
	//printf("transaction write ppa:%u\n", ppa);
	areq->params=(void*)tp;
	areq->end_req=transaction_end_req;
	areq->type=type;
	areq->parents=req;
	fdriver_lock_t lock;
	if(sync){
		fdriver_lock_init(&lock, 0);
		tp->lock=&lock;
	}
	else{
		tp->lock=NULL;
	}
	LSM.li->write(ppa, PAGESIZE, tp->value, ASYNC, areq);
	if(sync){
		fdriver_lock(&lock);
		fdriver_destroy(&lock);
	}
	return 1;
}

typedef struct commit_log_read{
	volatile bool isdone;
	transaction_entry *etr;
	value_set *value;
	char *data;
}cml;

void *insert_KP_to_skip(map_entry);

void transaction_force_compaction(){
	if(!compaction_wait_job_number()){
		skiplist *committing_skip=_tm.commit_KP;
		if(committing_skip->size==0){
			goto out;
		}
	//	list *committing_etr=_tm.commit_etr;
	//	printf("force commiting list size:%u\n",committing_etr->size);
		_tm.commit_KP=skiplist_init();
		//_tm.commit_etr=list_init();
		LMI.force_compaction_req_cnt++;
		bench_custom_start(write_opt_time2, 7);
		compaction_send_creq_by_skip(committing_skip, NULL, false);
		bench_custom_A(write_opt_time2, 7);

		//printf("jobs!! %u\n", compaction_wait_job_number());
	}
out:
	return;
}

static inline void __make_committed_etr_to_compaction(request *req, uint32_t tid, bool istimecheck){
	uint32_t ppa;
	if(istimecheck){
		bench_custom_start(write_opt_time2,1);
	}
	fdriver_lock(&_tm.table_lock);
	value_set *table_data=transaction_table_get_data(_tm.ttb);
	if(memory_log_usable(_tm.mem_log)){
		ppa=_tm.last_table;
		if(ppa!=UINT32_MAX){
			memory_log_lock(_tm.mem_log);
			if(ISMEMPPA(ppa)){
				memory_log_delete(_tm.mem_log, ppa);	
				memory_log_unlock(_tm.mem_log);
			}
			else{
				memory_log_unlock(_tm.mem_log);
				transaction_invalidate_PPA(LOG, ppa);	
			}
		}
		_tm.last_table=memory_log_insert(_tm.mem_log, (transaction_entry*)&_tm.last_table, -1, table_data->value);
		inf_free_valueset(table_data, FS_MALLOC_W);
	}
	else{
		ppa=get_log_PPA(TABLEW);
		__trans_write(NULL, table_data, ppa, TABLEW, req, false);
		if(_tm.last_table!=UINT_MAX)
			transaction_invalidate_PPA(LOG, _tm.last_table);
		_tm.last_table=ppa;
	}
	fdriver_unlock(&_tm.table_lock);
	if(istimecheck){
		bench_custom_A(write_opt_time2,1);
	}

	if(istimecheck){
		bench_custom_start(write_opt_time2,2);
	}
	if(!_tm.mem_log){
		leveling_node *lnode;
		while((lnode=transaction_get_comp_target(_tm.commit_KP, tid))){
			compaction_assign(NULL,lnode,false);
		}
		return;
	}
	list* temp_list=list_init();
	cml *temp_cml;
	char *cached_data;
	transaction_entry *etr;
	while((etr=transaction_table_get_comp_target(_tm.ttb, tid))){
		temp_cml=(cml*)malloc(sizeof(cml));
		temp_cml->isdone=false;
		temp_cml->etr=etr;
		memory_log_lock(_tm.mem_log);
		if(ISMEMPPA(etr->ptr.physical_pointer)){
			cached_data=memory_log_get(_tm.mem_log, etr->ptr.physical_pointer);
			temp_cml->data=(char*)malloc(PAGESIZE);
			memcpy(temp_cml->data, cached_data, PAGESIZE);
			temp_cml->isdone=true;
			memory_log_unlock(_tm.mem_log);
		}	
		else{
			transaction_table_print(_tm.ttb, false);
			abort();
			memory_log_unlock(_tm.mem_log);
			temp_cml->value=inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
			algo_req *treq=(algo_req*)malloc(sizeof(algo_req));
			treq->end_req=transaction_end_req;
			treq->type=LOGR;
			treq->parents=NULL;
			treq->params=(void*)temp_cml;
			LSM.li->read(etr->ptr.physical_pointer, PAGESIZE, temp_cml->value, ASYNC, treq);
		}
		list_insert(temp_list, (void*)temp_cml);
	}

	li_node *now, *nxt;
	for_each_list_node_safe(temp_list, now, nxt){
		temp_cml=(cml*)now->data;
		while(!temp_cml->isdone){}
		//uint32_t number_of_kp=*(uint16_t*)(temp_cml->data);
		/*

			I'm not sure this code is unnecessary

		if(_tm.commit_KP->size && METAFLUSHTRYCHECK(*_tm.commit_KP, number_of_kp)){
			skiplist *committing_skip=_tm.commit_KP;
			list *committing_etr=_tm.commit_etr;
		
			printf("before_insert compaction target num :%u\n", committing_skip->size);

			_tm.commit_KP=skiplist_init();
			_tm.commit_etr=list_init();

			compaction_send_creq_by_skip(committing_skip, committing_etr, false);		
		}*/
		
	//	printf("temp_cml->etr:%u\n", temp_cml->etr->tid);
		//list_insert(_tm.commit_etr, temp_cml->etr);

		
		LSM.lop->checking_each_key(temp_cml->data, insert_KP_to_skip);

		transaction_table_clear(_tm.ttb, temp_cml->etr, NULL);
		free(temp_cml->data);
		free(temp_cml);
		list_delete_node(temp_list, now);
	}

	if(memory_log_isfull(_tm.mem_log)){
		transaction_force_compaction();
	}
	list_free(temp_list);
	if(istimecheck){
		bench_custom_A(write_opt_time2,2);
	}
}

uint32_t transaction_set(request *const req){
	static uint32_t prev_tid=0;
	if(prev_tid==req->tid){
	
	}
	else{
		//printf("aaaaa set tid: %u\n", req->tid);
	}
	prev_tid=req->tid;
/*
	bench_custom_start(write_opt_time2, 3);
	if(memory_log_isfull(_tm.mem_log)){
		transaction_force_compaction();
		compaction_wait_jobs();
	}
	bench_custom_A(write_opt_time2, 3);
*/
	transaction_entry *etr;
	fdriver_lock(&_tm.table_lock);

#ifdef CHECKINGDATA
	//if(req->type!=FS_DELETE_T){
	//	map_crc_insert(req->key, req->value->value, req->value->length);
	//}
#endif

	bool is_changed_status;
	uint32_t flushed_tid_list[20];
	bench_custom_start(write_opt_time2, 0);
	flushed_tid_list[0]=UINT32_MAX;

	value_set* log=transaction_table_insert_cache(_tm.ttb,req->tid, req->key, req->value, req->type !=FS_DELETE_T, &etr, &is_changed_status, flushed_tid_list);
	fdriver_unlock(&_tm.table_lock);
	bench_custom_A(write_opt_time2, 0);

	if(is_changed_status){
		for(int i=0; flushed_tid_list[i]!=UINT32_MAX; i++){
			__make_committed_etr_to_compaction(req, flushed_tid_list[i], true);
		}
	}



	req->value=NULL;
	//	printf("req->seq :%u\n",req->seq);

	if(!log){
		req->end_req(req);
		return 1;
	}

	if(memory_log_usable(_tm.mem_log)){
		etr->ptr.physical_pointer=memory_log_insert(_tm.mem_log, etr, -1, log->value);
		inf_free_valueset(log, FS_MALLOC_W);
		req->end_req(req);
		return 1;
	}
	
	abort();
	ppa_t ppa=get_log_PPA(LOGW);
	__trans_write(NULL, log, ppa, LOGW , NULL, false);
	etr->status=LOGGED;
	etr->ptr.physical_pointer=ppa;
	req->end_req(req);
	return 1;
}

uint32_t transaction_range_delete(request *const req){
	//printf(":::::::::::range delete tid: %u, size:%u\n", req->tid, req->offset);

	if(memory_log_isfull(_tm.mem_log)){
		transaction_force_compaction();
		compaction_wait_jobs();
	}

	transaction_entry *etr;
	range_delete_flag=true;
	KEYT key;
	KEYT copied_key;
	kvssd_cpy_key(&copied_key, &req->key);
	for(uint32_t i=0; i<req->offset; i++){
		if(i==0){
			key=req->key;
		}else{
			kvssd_cpy_key(&key, &copied_key);
			uint64_t temp=*(uint64_t*)&key.key[key.len-sizeof(uint64_t)];
			temp=Swap8Bytes(temp);
			temp+=i;
			*(uint64_t*)&key.key[key.len-sizeof(uint64_t)]=Swap8Bytes(temp);
		}

		fdriver_lock(&_tm.table_lock);
		bool is_changed_status;
		uint32_t flushed_tid_list[20];
		flushed_tid_list[0]=UINT32_MAX;
		value_set* log=transaction_table_insert_cache(_tm.ttb,req->tid, key, NULL, false, &etr, &is_changed_status, flushed_tid_list);
		fdriver_unlock(&_tm.table_lock);

		if(is_changed_status){
			for(int i=0; flushed_tid_list[i]!=UINT32_MAX; i++){
				__make_committed_etr_to_compaction(req, flushed_tid_list[i], false);
			}
		}
		/*
		printf("range_delete!\n");
		transaction_table_print(_tm.ttb,false);*/

		if(!log){
			continue;
		}

		if(memory_log_usable(_tm.mem_log)){
			etr->ptr.physical_pointer=memory_log_insert(_tm.mem_log, etr, -1, log->value);
			//LSM.lop->header_print(log->value);
			inf_free_valueset(log, FS_MALLOC_W);
			continue;
		}
		abort();
		ppa_t ppa=get_log_PPA(LOGW);
		__trans_write(NULL, log, ppa, LOGW , NULL, false);
		etr->status=LOGGED;
		etr->ptr.physical_pointer=ppa;
	}
	kvssd_free_key_content(&copied_key);
	req->end_req(req);
	return 1;
}


uint32_t transaction_commit(request *const req){
	//printf("commit req->tid:%u\n", req->tid);
	/*
	if(req->tid==3446799){
		printf("target commit tid:!\n");
		transaction_table_print(_tm.ttb, false);
	}*/
	if(memory_log_isfull(_tm.mem_log)){
		compaction_wait_jobs();
	}
	if(!transaction_table_checking_commitable(_tm.ttb, req->tid)){
		transaction_table_clear_all(_tm.ttb, req->tid);
		req->end_req(req);
		return 0;
	}

	if(!lsm_rwlock_is_clean()){
		if(!inf_assign_try(req)){
			printf("Plz more retry_q ... to many commit before read done!!\n");
			abort();
		}
		//printf("retry tid:%u\n", req->tid);
		return 0;
	}

/*
	static uint32_t transaction_tid=0;
	if(transaction_tid==0){

	}
	else if(transaction_tid > req->tid){
	//	transaction_table_sanity_check();
		printf("------------------transaction_tid: %u, now tid:%u\n", transaction_tid, req->tid);
		if(debug_target_tid==0)
			debug_target_tid=req->tid;
	}
	else if(transaction_tid==req->tid){
		printf("transaction_tid: %u, now tid:%u\n", transaction_tid, req->tid);
		abort();
	}
	printf("transaction_tid: %u, now tid:%u\n", transaction_tid, req->tid);
	transaction_tid=req->tid;
	*/

	//printf("log_now before: %u\n", _tm.mem_log->now);

	ppa_t ppa;
	t_cparams *cparams=NULL;
	if(!memory_log_usable(_tm.mem_log)){
		cparams=(t_cparams*)malloc(sizeof(t_cparams));
		req->params=(void*)cparams;
		cparams->total_num=0;
		cparams->done_num=0;
	}

	bool is_wait_commit=false;
	transaction_entry *etr;
	uint32_t flushed_tid_list[20];
	flushed_tid_list[0]=UINT32_MAX;
	if(req->tid==44){
		printf("break!\n");
	}
	fdriver_lock(&_tm.table_lock);
	value_set *log=transaction_table_force_write(_tm.ttb, req->tid, &etr, &is_wait_commit, flushed_tid_list);
	fdriver_unlock(&_tm.table_lock);

	if(log){
		if(memory_log_usable(_tm.mem_log)){
			etr->ptr.physical_pointer=memory_log_insert(_tm.mem_log, etr, -1, log->value);
			if(range_delete_flag){
//				LSM.lop->header_print(log->value);
				range_delete_flag=false;
			}
			inf_free_valueset(log, FS_MALLOC_W);		
		}
		else{
			ppa=get_log_PPA(LOGW);
			cparams->total_num=2;
			__trans_write(NULL, log, ppa, LOGW, req, true); //sync
			etr->ptr.physical_pointer=ppa;
		}
	}
	else{
		//no data to commit
		if(_tm.mem_log){
			
		}
		else{
			cparams->total_num=1;
		}
	}


	/*write table*/
	fdriver_lock(&_tm.table_lock);
	transaction_table_update_all_entry(_tm.ttb, req->tid, is_wait_commit?WAITCOMMIT:COMMIT, false);
	fdriver_unlock(&_tm.table_lock);
/*
	if(req->tid==3446799){
		printf("target commit tid:!\n");
		transaction_table_print(_tm.ttb, false);
	}*/
/*
	if(req->tid==684886){
		printf("target tid print!!!!\n");
		transaction_table_print(_tm.ttb,false);
	}
*/	
	//checking_table_nonfull();
	if(log){
		bool check=false;
		for(int i=0; flushed_tid_list[i]!=UINT32_MAX; i++){
			if(flushed_tid_list[i]==req->tid){
				check=true;
			}
			__make_committed_etr_to_compaction(req, flushed_tid_list[i], false);
		}
	}
	__make_committed_etr_to_compaction(req, req->tid,false);
	/*
	printf("commit!!!!\n");
	transaction_table_print(_tm.ttb,false);*/

	if(memory_log_usable(_tm.mem_log)){
		req->end_req(req);
	}

	return 1;
}

//void *insert_KP_to_skip(KEYT _key, ppa_t ppa){
void *insert_KP_to_skip(map_entry map){
	KEYT temp_key;
	kvssd_cpy_key(&temp_key, &map.key);
	if(map.key.key[0]!='m' && map.key.key[0]!='d'){
		printf("invalid key %s:%d\n", __FILE__, __LINE__);
		abort();
	}

	bench_custom_start(write_opt_time2, 6);
	if(map.type==KVSEP){
		skiplist_insert_data_existIgnore(_tm.commit_KP, temp_key, 0, NULL, map.info.ppa, !(map.info.ppa==TOMBSTONE));
	}
	else if(map.type==KVUNSEP){
		skiplist_insert_data_existIgnore(_tm.commit_KP, temp_key, map.info.v_len, map.data, UINT32_MAX, true);
	}
	else{
		printf("unknown type!!! %s:%d\n",__FILE__, __LINE__);
		abort();
	}
	bench_custom_A(write_opt_time2, 6);
/*
	if(key_const_compare(_key, 'd', 3707, 262149, NULL)){
		printf("target skiplist :%p\n", _tm.commit_KP);
	}
*/
	if(METAFLUSHCHECK(*_tm.commit_KP)){
		skiplist *committing_skip=_tm.commit_KP;
	//	list *committing_etr=_tm.commit_etr;

	//	printf("insert_kp compaction target num :%u\n", committing_skip->size);

		_tm.commit_KP=skiplist_init();
		//_tm.commit_etr=list_init();
		bench_custom_start(write_opt_time2, 7);
		compaction_send_creq_by_skip(committing_skip, NULL, false);
		bench_custom_A(write_opt_time2, 7);
	}
	return NULL;
}

uint32_t processing_read(void * req, transaction_entry **entry_set, t_rparams *trp, uint8_t type){
	/*
	   type
		0: req from user
		1: req from gc
	 */
	algo_req *tr_req=NULL;
	/*
		0 : find data in cache
		1 : issue committed data
		2 : not found in log area
	 */
	request * user_req=NULL;
	struct gc_node *gc_req=NULL;
	
	switch(type){
		case 0: user_req=(request *)req; break;
		case 1: gc_req=(gc_node*)req; break;
		default: abort(); break;
	}

	uint32_t res=0;
	map_entry target;
	for(uint32_t i=trp->index ; entry_set[i]!=NULL; i++){
		transaction_entry *temp=entry_set[i];
		switch(temp->status){
			case CACHED:
			case CACHEDCOMMIT:
			case WAITFLUSHCOMMITCACHED:
				if(type==0){
					res=__lsm_get_sub(user_req, NULL, NULL, temp->ptr.memtable, 0);
					if(res){
						return 0;
					}
				}
				else{
					snode* t=skiplist_find(temp->ptr.memtable, gc_req->lpa);
					if(t){
						if(t->ppa==gc_req->ppa){
							gc_req->status=DONE;
							gc_req->found_src=SKIP;
							gc_req->target=(void*)t;
						}
						else{
							gc_req->status=NOUPTDONE;
							gc_req->plength=0;
						}
						return 0;
					}
				}
				trp->index=i+1;
				break;
			case WAITFLUSHCOMMITLOGGED:
			case LOGGED:
			case COMMIT:
				memory_log_lock(_tm.mem_log);
				if(ISMEMPPA(temp->ptr.physical_pointer)){
					//log buffer hit
					target=LSM.lop->find_map_entry(memory_log_get(_tm.mem_log, temp->ptr.physical_pointer), user_req->key);
					memory_log_unlock(_tm.mem_log);
					if(target.type!=INVALIDENT){
						if(target.type==KVUNSEP){
							memcpy(user_req->value->value, target.data, target.info.v_len);
							user_req->end_req(user_req);
							return 0;
						}
						tr_req=(algo_req*)malloc(sizeof(algo_req));
						tr_req->end_req=transaction_end_req;
						tr_req->params=(void*)trp;
						tr_req->type=DATAR;
						tr_req->parents=user_req;
						user_req->value->ppa=target.info.ppa;
						LSM.li->read(target.info.ppa/NPCINPAGE, PAGESIZE, trp->value, ASYNC, tr_req);
						return 0;
					}
					else
						 break;

				}
				else{
					memory_log_unlock(_tm.mem_log);
					trp->entry_set=entry_set;
					tr_req=(algo_req*)malloc(sizeof(algo_req));
					tr_req->end_req=type==0?transaction_end_req:gc_transaction_end_req;
					tr_req->params=(void*)trp;
					tr_req->type=LOGR;
					tr_req->parents=(request*)req;
					trp->ppa=temp->ptr.physical_pointer;
					trp->index=i+1;
					if(type==1){
						gc_req->status=ISSUE;
					}
					LSM.li->read(temp->ptr.physical_pointer, PAGESIZE, trp->value, ASYNC, tr_req );
				}
				return 1;
			default: 
				break;
		}
	}	
	return 2;
}


uint32_t __transaction_get(request *const req){
	transaction_entry **entry_set;
	t_rparams *trp=NULL;
	algo_req *tr_req=NULL;
	uint32_t res=0;
	if(req->magic==1){
		goto search_lsm;
	}

/*
	if(key_const_compare(req->key, 'd', 3707, 262149, NULL)){
		printf("target set now reading\n");
		transaction_table_print(_tm.ttb, false);
	}
*/
	if(!req->params){ //first round
		//transaction_table_print(_tm.ttb, false);
		trp=(t_rparams*)malloc(sizeof(t_rparams));
		trp->max=transaction_table_find(_tm.ttb, req->tid, req->key, &entry_set);
		/*
		if(trp->max!=0){
			static int cnt=0;
			printf("here? %d max:%d\n",cnt++,trp->max);
		}*/
		trp->index=0;
		trp->value=req->value;
		res=processing_read((void*)req, entry_set, trp, 0);
		req->params=trp; 
	}
	else{
		trp=(t_rparams*)req->params;
		entry_set=trp->entry_set;
		/*searching meta segment*/
		char *sets=ISNOCPY(LSM.setup_values) ? nocpy_pick(trp->ppa)
			: trp->value->value;
		map_entry target=LSM.lop->find_map_entry(sets, req->key);
		if(target.type!=INVALIDENT){	
			if(target.info.ppa==TOMBSTONE){
				free(entry_set);
				memset(req->value->value, 0, LPAGESIZE);
				req->end_req(req);
				return 1;
			}
			else if(target.type==KVUNSEP){
				memcpy(req->value->value, target.data, target.info.v_len);
				req->end_req(req);
				return 1;
			}
			/*issue data*/
			tr_req=(algo_req*)malloc(sizeof(algo_req));
			tr_req->end_req=transaction_end_req;
			tr_req->params=(void*)trp;
			tr_req->type=DATAR;
			tr_req->parents=req;
			req->value->ppa=target.info.ppa;
			LSM.li->read(target.info.ppa/NPCINPAGE, PAGESIZE, trp->value, ASYNC, tr_req);
			free(entry_set);
			return 1;
		}

		/*next round*/
		res=processing_read((void*)req, entry_set, trp,0);
	}

	switch(res){
		case 0: //CACHED, CACHEDCOMMIT, MEMLOG
			free(entry_set);
			free(req->params);
			return 1;
		case 1: //COMMMIT, LOGGED
			req->magic=2;
			return res;
		case 2: //not found in log
			free(entry_set);
			req->magic=2;
			break;
	}

search_lsm:
	if(req->magic==2){
		free(req->params);
		req->params=NULL;

		res=__lsm_get_sub(req, NULL, NULL, _tm.commit_KP, 0);
		if(res) return res;
		//KP check
	}
	req->magic=1;
	//not found in log
	return __lsm_get(req);
}

#ifdef TRACECOLLECT
extern int trace_fd;
#endif

uint32_t transaction_get_postproc(request *const req, uint32_t res_type){
	if(res_type==0){
		free(req->params);
		switch (req->type){
			case FS_GET_T:
				req->type=FS_NOTFOUND_T;
				break;
			case FS_MGET_T:
				req->type=FS_MGET_NOTFOUND_T;
				break;
		}
#ifdef KOO
		char buf[50]={0,};
		key_interpreter(req->key, buf);
		printf("notfound key: %s\n", buf);
	#ifdef TRACECOLLECT
		fsync(trace_fd);
	#endif
#else
		printf("notfound key: %.*s\n",KEYFORMAT(req->key));
		abort();
#endif
		req->end_req(req);
		//abort();
	}
	return res_type;
}

uint32_t transaction_get(request *const req){
	void *_req;
	uint32_t res_type=0;
	request *temp_req;
	while(1){
		if((_req=q_dequeue(LSM.re_q))){
			temp_req=(request*)_req;
			res_type=__transaction_get(temp_req);
			transaction_get_postproc(temp_req,res_type);
		}
		else{
			break;
		}
	}

	return transaction_get_postproc(req,__transaction_get(req));
}

void* transaction_end_req(algo_req * const trans_req){
	t_params *params=NULL;
	t_rparams *rparams=NULL;
	t_cparams *cparams=NULL;
	cml *temp_cml=NULL;
	request *parents=trans_req->parents;
	bool free_struct=true;
	uint32_t start_offset;
	uint16_t value_len;
	switch(trans_req->type){
		case TABLEW:
		case LOGW:
			if(parents && parents->params){
				cparams=(t_cparams*)parents->params;
				cparams->done_num++;
			}
			params=(t_params*)trans_req->params;
			inf_free_valueset(params->value, FS_MALLOC_W);
			if(params->lock){
				fdriver_unlock(params->lock);
			}
			break;
		case LOGR:
			free_struct=false;
			rparams=(t_rparams*)trans_req->params;
			if(parents){
				parents->params=(void*)rparams;
				while(1){
					if(inf_assign_try(parents)) break;
					else{
						if(q_enqueue((void*)parents, LSM.re_q)){
							break;
						}
					}
				}
			}
			else{
				temp_cml=(cml*)trans_req->params;
				temp_cml->isdone=true;
				temp_cml->data=temp_cml->value->value;
				temp_cml->value->value=NULL;
				inf_free_valueset(temp_cml->value, FS_MALLOC_R);
			}
			break;
		case DATAR:
			rparams=(t_rparams*)trans_req->params;
			start_offset=parents->value->ppa%NPCINPAGE;
			value_len=variable_get_value_len(parents->value->ppa);
			parents->value->length=value_len;
			if(start_offset){
				memmove(parents->value->value, &parents->value->value[start_offset*PIECE], value_len);
			}
			parents->end_req(parents);
			break;
	}
	
	if(cparams && cparams->done_num==cparams->total_num){
		free(cparams);
		parents->end_req(parents);
	}
	
	if(free_struct){
		free(params);
		free(rparams);
	}

	free(trans_req);
	return NULL;
}

uint32_t transaction_abort(request *const req){
	printf("not implemented!\n");
	abort();
	return 1;
}
uint32_t get_log_PPA(uint32_t type){
	blockmanager *bm=LSM.bm;
	pm *t=&_tm.t_pm;

retry:
	if(!t->active || bm->check_full(bm, t->active, MASTER_PAGE)){
		if(bm->pt_isgc_needed(bm,LOG_S)){
			gc_log();
			goto retry;
		}
		free(t->active);
		t->active=bm->pt_get_segment(bm, LOG_S, false);
	}

	uint32_t res=bm->get_page_num(bm, t->active);

	if(res==UINT_MAX){
		abort();
	}
	bm->set_oob(bm, (char*)&type, sizeof(type), res);
	bm->populate_bit(bm,res);
	return res;
}

leveling_node *transaction_get_comp_target(skiplist *skip, uint32_t tid){
	transaction_entry *etr=transaction_table_get_comp_target(_tm.ttb, tid);
	if(!etr) return NULL;

	fdriver_lock(&_tm.table_lock);
	leveling_node *res=(leveling_node*)malloc(sizeof(leveling_node));
	run_t *new_entry=LSM.lop->make_run(etr->range.start, etr->range.end, etr->ptr.physical_pointer);

	res->start=new_entry->key;
	res->end=new_entry->end;
	res->entry=new_entry;
	res->tetr=etr;
	res->mem=NULL;
	fdriver_unlock(&_tm.table_lock);
	return res;
}

bool transaction_invalidate_PPA(uint8_t type, uint32_t ppa){
	//printf("invalidated %u ppa\n", ppa);
	if(!LSM.bm->unpopulate_bit(LSM.bm, ppa)){
	//	printf("double invalidated :%u \n", ppa);
	//	abort();
	}
	return true;
}

uint32_t gc_log(){
	compaction_wait_jobs();
	LMI.log_gc_cnt++;
	blockmanager *bm=LSM.bm;
	printf("gc log!!\n");
	__gsegment *tseg=bm->pt_get_gc_target(bm, LOG_S);
	if(!tseg || tseg->invalidate_number==0){
		printf("log full!\n");
		abort();
	} 
	//printf("gc log(%u)! %u ~ %u\n",tseg->invalidate_number, tseg->blocks[0]->block_num * _PPB, tseg->blocks[BPS-1]->block_num* _PPB+ _PPB-1);
	
	//printf("_PPS : %u\n", _PPS);
	if(tseg->invalidate_number <_PPS ){
		//printf("not full invalidataion %d\n",tseg->invalidate_number);
		transaction_table_print(_tm.ttb, false);
		//printf("table print end\n\n");
		//abort();
	}
	uint32_t tpage=0;
	int bidx=0;
	int pidx=0;
	/*
	for_each_page_in_seg(tseg, tpage, bidx, pidx){
		if(LSM.bm->is_valid_page(LSM.bm, tpage)){
			printf("valid page number :%u\n", tpage);
		}
	}*/


	bm->pt_trim_segment(bm, LOG_S, tseg, LSM.li);

	if(ISNOCPY(LSM.setup_values)){
		tpage=bidx=pidx=0;
		for_each_page_in_seg(tseg,tpage,bidx,pidx){
			nocpy_free_page(tpage);
		}
	}
	return 1;
}

uint32_t transaction_clear(transaction_entry *etr){
	fdriver_lock(&_tm.table_lock);
	uint32_t res=transaction_table_clear(_tm.ttb, etr, NULL);
	//transaction_table_print(_tm.ttb, false);
	fdriver_unlock(&_tm.table_lock);
	//checking_table_nonfull();
	return res;
}

bool transaction_debug_search(KEYT key){
	char *test=(char*)malloc(PAGESIZE);
	for(uint32_t i=0; i<_tm.ttb->full; i++){
		transaction_entry *etr=&_tm.ttb->etr[i];
		map_entry temp;
		switch(etr->status){
			case EMPTY:
				continue;
			case CACHED:
				if(skiplist_find(etr->ptr.memtable,key)){
					printf("find in transaction cache!!\n");
					return true;
				}
				continue;
			case COMMIT:
			case COMPACTION:
			case LOGGED:
				if(lsm_test_read(etr->ptr.physical_pointer,test)){
					return true;
				}
				temp=LSM.lop->find_map_entry(test, key);	
				if(temp.type!=INVALIDENT){
					printf("find in transaction log index:%u!!\n",i);
					free(test);
					return true;
				}
			default:
				continue;
		}
	}
	free(test);
	return false;
}

void transaction_evicted_write_entry(transaction_entry* etr, char *data){
	//printf("called!\n");
	//transaction_table_print(_tm.ttb, true);
	//abort();
	printf("abort log_write!!\n");
	abort();
	if((void*)etr==(void*)&_tm.last_table){
		ppa_t ppa=get_log_PPA(TABLEW);
		value_set *value=inf_get_valueset(data, FS_MALLOC_W, PAGESIZE);
		__trans_write(NULL, value, ppa, LOGW, NULL, true);
	//	printf("table ppa update %d->%d\n", _tm.last_table, ppa);
		_tm.last_table=ppa;
	}
	else{
		if(!etr) return;
		/*
		if(!find_last_entry(etr->tid)){
			return;
		}*/
		ppa_t ppa=get_log_PPA(LOGW);
		value_set *value=inf_get_valueset(data, FS_MALLOC_W, PAGESIZE);
		__trans_write(NULL, value, ppa, LOGW, NULL, true);
	//	printf("tid: %d ppa update %d->%d\n",etr->tid, etr->ptr.physical_pointer, ppa);
		if(temp_tid==0){
			temp_tid=etr->tid;
		}
/*
		if(!find_last_entry(etr->tid)){
			printf("no data in table!\n");
			abort();
		}*/
	//	printf("called?? %u->%u\n",etr->ptr.physical_pointer, ppa);
		etr->ptr.physical_pointer=ppa;
	}
}

void transaction_log_write_entry(transaction_entry *etr, char *data){
	if(memory_log_usable(_tm.mem_log))
		etr->ptr.physical_pointer=memory_log_insert(_tm.mem_log, etr, -1, data);
	else{
		ppa_t ppa=get_log_PPA(TABLEW);
		__trans_write(NULL, inf_get_valueset(data, FS_MALLOC_W, PAGESIZE), ppa, LOGW , NULL, false);
		etr->status=LOGGED;
		etr->ptr.physical_pointer=ppa;
	}
}
