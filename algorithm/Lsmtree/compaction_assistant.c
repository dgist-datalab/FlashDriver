#include "lsmtree.h"
#include "compaction.h"
#include "skiplist.h"
#include "page.h"
#include "bloomfilter.h"
#include "nocpy.h"
#include "lsmtree_scheduling.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <sched.h>

#include "../../interface/interface.h"
#include "../../include/types.h"
#include "../../include/data_struct/list.h"
#include "../../include/utils/kvssd.h"
#include "../../include/sem_lock.h"
#include "../../bench/bench.h"

#ifdef DEBUG
#endif

#ifdef KVSSD
extern KEYT key_min, key_max;
#endif

volatile int memcpy_cnt;
extern lsmtree LSM;
extern volatile int comp_target_get_cnt;
extern MeasureTime write_opt_time[10];
volatile int epc_check=0;
compM compactor;
pthread_mutex_t compaction_wait;
pthread_mutex_t compaction_flush_wait;
pthread_mutex_t compaction_req_lock;
pthread_cond_t compaction_req_cond;

void compaction_sub_pre(){
	pthread_mutex_lock(&compaction_wait);
	memcpy_cnt=0;
}

void compaction_selector(level *a, level *b,leveling_node *lnode, pthread_mutex_t* lock){
	if(b->istier){
	}
	else{
		leveling(a,b,lnode,lock);
	}
}

void compaction_sub_wait(){
#ifdef MUTEXLOCK
	if(epc_check==comp_target_get_cnt+memcpy_cnt)
		pthread_mutex_unlock(&compaction_wait);
#elif defined (SPINLOCK)
	while(comp_target_get_cnt+memcpy_cnt!=epc_check){}
#endif

	memcpy_cnt=0;
	comp_target_get_cnt=0;
	epc_check=0;
}

void compaction_sub_post(){
	pthread_mutex_unlock(&compaction_wait);
}

void htable_checker(htable *table){
	for(uint32_t i=0; i<LSM.KEYNUM; i++){
		if(table->sets[i].ppa<512 && table->sets[i].ppa!=0){
			printf("here!!\n");
		}
	}
}

bool compaction_init(){
	compactor.processors=(compP*)malloc(sizeof(compP)*CTHREAD);
	memset(compactor.processors,0,sizeof(compP)*CTHREAD);

	pthread_mutex_init(&compaction_req_lock,NULL);
	pthread_cond_init(&compaction_req_cond,NULL);

	for(int i=0; i<CTHREAD; i++){
		compactor.processors[i].master=&compactor;
		pthread_mutex_init(&compactor.processors[i].flag, NULL);
		pthread_mutex_lock(&compactor.processors[i].flag);
		q_init(&compactor.processors[i].q,CQSIZE);
		pthread_create(&compactor.processors[i].t_id,NULL,compaction_main,NULL);
	}
	compactor.stopflag=false;
	pthread_mutex_init(&compaction_wait,NULL);
	pthread_mutex_init(&compaction_flush_wait,NULL);
	pthread_mutex_lock(&compaction_flush_wait);

#ifdef PIPECOMP
	compactor.pt_leveling=pipe_partial_leveling;
#elif defined(HWCOMP)
	compactor.pt_leveling=hw_partial_leveling;
#else
	compactor.pt_leveling=partial_leveling;
#endif

#ifdef PIPECOMP
	lsm_io_sched_init();
#endif
	return true;
}


void compaction_free(){
	compactor.stopflag=true;
	int *temp;
	for(int i=0; i<CTHREAD; i++){
		compP *t=&compactor.processors[i];
		pthread_cond_signal(&compaction_req_cond);
		while(pthread_tryjoin_np(t->t_id,(void**)&temp)){
			pthread_cond_signal(&compaction_req_cond);
		}
		q_free(t->q);
	}
	free(compactor.processors);
}

void compaction_wait_done(){
	bool flag=false;
	while(1){
#ifdef LEAKCHECK
		sleep(2);
#endif
		for(int i=0; i<CTHREAD; i++){
			compP* proc=&compactor.processors[i];
			if(proc->q->size!=CQSIZE){
				flag=true;
				break;
			}
		}
		if(flag) break;
	}
}

void compaction_assign(compR* req){
	//static int seq_num=0;
	bool flag=false;
	while(1){
#ifdef LEAKCHECK
		sleep(2);
#endif
		for(int i=0; i<CTHREAD; i++){
			compP* proc=&compactor.processors[i];
			pthread_mutex_lock(&compaction_req_lock);
			if(proc->q->size==0){
				if(q_enqueue((void*)req,proc->q)){
					flag=true;
				}
				else{
					DEBUG_LOG("FUCK!");
				}
			}
			else {
				if(q_enqueue((void*)req,proc->q)){	
					flag=true;
				}
				else{
					flag=false;
				}
			}
			if(flag){
				pthread_cond_signal(&compaction_req_cond);
				pthread_mutex_unlock(&compaction_req_lock);
				break;
			}
			else{
				pthread_mutex_unlock(&compaction_req_lock);
			}
		}
		if(flag) break;
	}
}

bool compaction_force(){
	for(int i=LEVELN-2; i>=0; i--){
		if(LSM.disk[i]->n_num){
			compaction_selector(LSM.disk[i],LSM.disk[LEVELN-1],NULL,&LSM.level_lock[LEVELN-1]);
			return true;
		}
	}
	return false;
}
bool compaction_force_target(int from, int to){
	int test=LSM.lop->get_number_runs(LSM.disk[from]);
	if(!test) return false;
	//LSM.lop->print_level_summary();
	compaction_selector(LSM.disk[from],LSM.disk[to],NULL,&LSM.level_lock[to]);
	return true;
}

bool compaction_force_levels(int nol){
	for(int i=0; i<nol; i++){
		int max=0,target=0;
		for(int j=0; j<LEVELN-1; j++){
			int noe=LSM.lop->get_number_runs(LSM.disk[j]);
			if(max<noe){
				max=noe;
				target=j;
			}
		}
		if(max!=0){
			compaction_selector(LSM.disk[target],LSM.disk[LEVELN-1],NULL,&LSM.level_lock[LEVELN-1]);
		}else{
			return false;
		}
	}
	return true;
}

extern pm data_m;
void compaction_cascading(bool *_is_gc_needed){
	int start_level=0,des_level=-1;
	bool is_gc_needed=*_is_gc_needed;
#ifdef MULTIOPT
	for(int i=0; i<LEVELN; i++){
		if(LSM.lop->full_check(LSM.disk[i])){
			des_level=i;
			if(des_level==LEVELN-1){
				LSM.disk[des_level]->m_num*=2;
				des_level--;
				break;
			}
		}
		else break;
	}
	
	if(is_gc_needed || des_level!=-1){
		int target_des=des_level+1;
	#ifdef GCOPT
		if(!is_gc_needed && target_des==LEVELN-1){
				is_gc_needed=gc_dynamic_checker(true);
		}
	#endif
		if(is_gc_needed) target_des=LEVELN-1; 
		/*do level caching*/

		int i=0;
		for(i=start_level; i<target_des; i++){
			if(i+1<LEVELCACHING){
				LSM.compaction_cnt++;
				compaction_selector(LSM.disk[i],LSM.disk[i+1],NULL,&LSM.level_lock[i+1]);
			}
			else
				break;

			if(i+1==target_des) return; //done
		}
		start_level=i;

		LSM.compaction_cnt++;
		multiple_leveling(start_level,target_des);
	}
#else
	while(1){
		if(is_gc_needed || unlikely(LSM.lop->full_check(LSM.disk[start_level]))){
			LSM.compaction_cnt++;
			des_level=(start_level==LEVELN?start_level:start_level+1);
			if(des_level==LEVELN) break;
			if(start_level==LEVELN-2){
	#ifdef GCOPT
				if(!is_gc_needed){
					is_gc_needed=gc_dynamic_checker(true);
				}
	#endif
			}
			compaction_selector(LSM.disk[start_level],LSM.disk[des_level],NULL,&LSM.level_lock[des_level]);
			LSM.disk[start_level]->iscompactioning=false;
			start_level++;
		}
		else{
			break;
		}
	}
#endif
	*_is_gc_needed=is_gc_needed;
}

void *compaction_main(void *input){
	void *_req;
	compR*req;
	compP *_this=NULL;
	//static int ccnt=0;
	char thread_name[128]={0};
	pthread_t current_thread=pthread_self();
	sprintf(thread_name,"%s","compaction_thread");
	pthread_setname_np(pthread_self(),thread_name);

	for(int i=0; i<CTHREAD; i++){
		if(pthread_self()==compactor.processors[i].t_id){
			_this=&compactor.processors[i];
		}
	}		
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);	
	CPU_SET(1,&cpuset);
	while(1){
#ifdef LEAKCHECK
		sleep(2);
#endif
		pthread_mutex_lock(&compaction_req_lock);
		if(_this->q->size==0){
			pthread_cond_wait(&compaction_req_cond,&compaction_req_lock);
			cpu_set_t cpuset;
			CPU_ZERO(&cpuset);
			CPU_SET(1,&cpuset);
		}
		_req=q_pick(_this->q);
		pthread_mutex_unlock(&compaction_req_lock);

		pthread_setaffinity_np(current_thread,sizeof(cpu_set_t),&cpuset);

		if(compactor.stopflag)
			break;


		req=(compR*)_req;
		leveling_node lnode;

		if(req->fromL==-1){
			LSM.zero_compaction_cnt++;
			lnode.mem=req->temptable;
			compaction_data_write(&lnode);
			compaction_selector(NULL,LSM.disk[0],&lnode,&LSM.level_lock[0]);
		}
		bool is_gc_needed=false;
#if LEVELN!=1
		compaction_cascading(&is_gc_needed);
#endif
#ifdef GCOPT
		if(is_gc_needed){
			gc_data();
		}
#endif
		free(lnode.start.key);
		free(lnode.end.key);

		skiplist_free(req->temptable);
#ifdef WRITEWAIT
		if(req->last){
			lsm_io_sched_flush();	
			LSM.li->lower_flying_req_wait();
			pthread_mutex_unlock(&compaction_flush_wait);
		}
#endif
		free(req);
		q_dequeue(_this->q);
	}
	
	return NULL;
}

void compaction_check(KEYT key, bool force){
	if(LSM.memtable->size<LSM.FLUSHNUM) return;
	compR *req;
	bool last;
	uint32_t avg_cnt;
	skiplist *t=NULL, *t2=NULL;
	do{
		last=0;
		if(t2!=NULL){
			t=t2;
		}else{
			t=skiplist_cutting_header(LSM.memtable,&avg_cnt);
			LSM.keynum_in_header=(LSM.keynum_in_header*LSM.keynum_in_header_cnt+avg_cnt)/(LSM.keynum_in_header_cnt+1);
		}
		t2=skiplist_cutting_header(LSM.memtable,&avg_cnt);
		LSM.keynum_in_header=(LSM.keynum_in_header*LSM.keynum_in_header_cnt+avg_cnt)/(LSM.keynum_in_header_cnt+1);

		if(t2==LSM.memtable) last=1;
		req=(compR*)malloc(sizeof(compR));
		req->fromL=-1;
		req->last=last;
		req->temptable=t;
		compaction_assign(req);
	}while(!last);

#ifdef WRITEWAIT
	//LSM.memtable=skiplist_init();
	pthread_mutex_lock(&compaction_flush_wait);
#endif
}



void compaction_subprocessing(struct skiplist *top, struct run** src, struct run** org, struct level *des){
	compaction_sub_wait();
	LSM.lop->merger(top,src,org,des);

	KEYT key,end;
	run_t* target=NULL;
	while((target=LSM.lop->cutter(top,des,&key,&end))){
		compaction_htable_write_insert(des,target,false);

		free(target);
	}
}

void compaction_lev_seq_processing(level *src, level *des, int headerSize){
#ifdef LEVELCACHING
	if(src->idx<LEVELCACHING){
		run_t **datas;
		int cache_added_size=LSM.lop->get_number_runs(src);
		LSM.lop->cache_comp_formatting(src,&datas);
		cache_size_update(LSM.lsm_cache,LSM.lsm_cache->m_size+cache_added_size);
		for(int i=0;datas[i]!=NULL; i++){
#ifdef BLOOM
			bf_free(datas[i]->filter);
			datas[i]->filter=LSM.lop->making_filter(datas[i],-1,des->fpr);
#endif
			compaction_htable_write_insert(des,datas[i],false);
			free(datas[i]);
		}
		free(datas);
		return;
	}
#endif

#ifdef MONKEY
	if(src->m_num!=des->m_num){
		compaction_seq_MONKEY(src,headerSize,des);
		return;
	}
#endif
	run_t *r;
	lev_iter *iter=LSM.lop->get_iter(src,src->start, src->end);
	for_each_lev(r,iter,LSM.lop->iter_nxt){

		r->iscompactioning=SEQMOV;
		LSM.lop->insert(des,r);
	}
}
#ifdef MONKEY
void compaction_seq_MONKEY(level *t,int num,level *des){
	run_t **target_s;
	LSM.lop->range_find(t,t->start,t->end,&target_s);

	compaction_sub_pre();
	for(int j=0; target_s[j]!=NULL; j++){
		if(!htable_read_preproc(target_s[j])){
			compaction_htable_read(target_s[j],(PTR*)&target_s[j]->cpt_data);
		}
		target_s[j]->iscompactioning=SEQCOMP;
		epc_check++;
	}

	compaction_sub_wait();

	for(int k=0; target_s[k]; k++){
		BF *filter=LSM.lop->making_filter(target_s[k],-1,des->fpr);
		BF *temp=target_s[k]->filter;

		/*filter back*/
		target_s[k]->filter=filter;
		htable_read_postproc(target_s[k]);
		LSM.lop->insert(des,target_s[k]);
		target_s[k]->filter=temp;
	}
	compaction_sub_post();
	free(target_s);
}
#endif

bool htable_read_preproc(run_t *r){
	bool res=false;
	pthread_mutex_lock(&LSM.lsm_cache->cache_lock);
	if(r->c_entry){
		cache_entry_lock(LSM.lsm_cache,r->c_entry);
#ifndef PIPECOMP
		memcpy_cnt++;
#endif

#ifndef NOCPY
		r->cpt_data=htable_copy(r->cache_data);
		r->cpt_data->done=true;
#endif//when the data is cached, the data will be loaded from memory in merger logic
		pthread_mutex_unlock(&LSM.lsm_cache->cache_lock);		
		res=true;
	}
	else{
		pthread_mutex_unlock(&LSM.lsm_cache->cache_lock);
		r->cpt_data=htable_assign(NULL,false);
		r->cpt_data->iscached=0;
	}
	if(!r->iscompactioning) r->iscompactioning=COMP;
	return res;
}

void htable_read_postproc(run_t *r){
	if(r->iscompactioning!=INVBYGC && r->iscompactioning!=SEQCOMP){
		if(r->pbn!=UINT32_MAX)
			invalidate_PPA(HEADER,r->pbn);
		else{
			//the run belong to levelcaching lev
		}
	}
	if(r->c_entry){
		cache_entry_unlock(LSM.lsm_cache,r->c_entry);
#ifndef NOCPY
		htable_free(r->cpt_data);
#endif
	}else{
		htable_free(r->cpt_data);
		r->cpt_data=NULL;
		if(r->pbn==UINT32_MAX){
			LSM.lop->release_run(r);
			free(r);
		}
	}
}