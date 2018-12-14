#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <limits.h>
#include "../../include/lsm_settings.h"
#include "../../interface/interface.h"
#include "../../bench/bench.h"
#include "compaction.h"
#include "lsmtree.h"
#include "page.h"
#include "nocpy.h"
#include "factory.h"
#include<stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

extern uint32_t hash_insert_cnt;

struct algorithm algo_lsm={
	.create=lsm_create,
	.destroy=lsm_destroy,
	.get=lsm_get,
	.set=lsm_set,
	.remove=lsm_remove
};
extern OOBT *oob;
lsmtree LSM;
int save_fd;
int32_t SIZEFACTOR;
int32_t caching_size;

MeasureTime __get_mt;
MeasureTime __get_mt2;
uint64_t bloomfilter_memory;
uint64_t __get_max_value;
int __header_read_cnt;

void lsm_debug_print(){
	printf("mt:%ld %.6f\n",__get_mt.adding.tv_sec,(float)__get_mt.adding.tv_usec/1000000);
	printf("mt2:%ld %.6f\n",__get_mt2.adding.tv_sec,(float)__get_mt2.adding.tv_usec/1000000);
	printf("header_read_cnt:%d\n",__header_read_cnt);
	printf("\n");
}
extern level_ops h_ops;
void lsm_bind_ops(lsmtree *l){
	l->lop=&h_ops;
	l->KEYNUM=l->lop->get_max_table_entry();
	l->FLUSHNUM=l->lop->get_max_flush_entry(1024);
	l->inplace_compaction=true;
}
uint32_t __lsm_get(request *const);
static int32_t get_sizefactor(uint64_t as){
	uint32_t _f=LEVELN;
	int32_t res;
	
#ifdef LEVELCACHING
	uint32_t all_memory=(as/1024);
	caching_size=CACHINGSIZE*(all_memory/(8*K));
	res=caching_size;
	//res=_f?ceil(pow(10,((log10(as/PAGESIZE/LSM.FLUSHNUM-caching_size)))/(_f-1))):as/PAGESIZE/LSM.FLUSHNUM;
#else
	res=_f?ceil(pow(10,log10(as/PAGESIZE/LSM.FLUSHNUM)/(_f))):as/PAGESIZE/LSM.FLUSHNUM;
#endif
	return res;
}
uint32_t lsm_create(lower_info *li, algorithm *lsm){
#if(SIMULATION)
	return __lsm_create_simulation(li,lsm);
#else
	return __lsm_create_normal(li,lsm);
#endif
}

uint32_t __lsm_create_simulation(lower_info *li, algorithm *lsm){
	measure_init(&__get_mt);
	measure_init(&__get_mt2);
	lsm_bind_ops(&LSM);
	LSM.memtable=skiplist_init();
	SIZEFACTOR=get_sizefactor(SIMULATION);
	unsigned long long sol;
#ifdef MONKEY
	int32_t SIZEFACTOR2=ceil(pow(10,log10(SIMULATION/PAGESIZE/LSM.KEYNUM/LEVELN)/(LEVELN-1)));
	float ffpr=RAF*(1-SIZEFACTOR2)/(1-pow(SIZEFACTOR2,LEVELN-1));
#endif
	float target_fpr=0;
	uint64_t sizeofall=0;
#ifdef LEVELCACHING
	uint64_t lev_caching_mem=0;
	sol=SIZEFACTOR;
#else
	sol=SIZEFACTOR;
#endif

	printf("LSM KEYNUM:%u LSM FLUSHNUM:%u\n",LSM.KEYNUM,LSM.FLUSHNUM);
	for(int i=0; i<LEVELN-1; i++){//for lsmtree -1 level
		LSM.disk[i]=LSM.lop->init(sol,i,target_fpr,false);

#ifdef BLOOM
	#ifdef MONKEY
		target_fpr=pow(SIZEFACTOR2,i)*ffpr;
	#else
		target_fpr=(float)RAF/LEVELN;
	#endif
		LSM.disk[i]->fpr=target_fpr;
#endif
		printf("[%d] fpr:%lf bytes per entry:%lu noe:%d\n",i+1,target_fpr,bf_bits(LSM.KEYNUM,target_fpr), LSM.disk[i]->m_num);
		sizeofall+=LSM.disk[i]->m_num;
#ifdef LEVELCACHING
		if(i<LEVELCACHING){
			lev_caching_mem+=LSM.disk[i]->m_num*PAGESIZE;
		}   
#endif
		bloomfilter_memory+=bf_bits(LSM.KEYNUM,target_fpr)*sol;
		sol*=SIZEFACTOR;
		LSM.level_addr[i]=(PTR)LSM.disk[i];
	}   
#ifdef TIERING
	LSM.disk[LEVELN-1]=LSM.lop->init(sol,LEVELN-1,1,true);
#else
	LSM.disk[LEVELN-1]=LSM.lop->init(sol,LEVELN-1,1,false);
#endif
	printf("[%d] fpr:1.0000 bytes per entry:%lu noe:%d\n",LEVELN,bf_bits(LSM.KEYNUM,1),LSM.disk[LEVELN-1]->m_num);
	sizeofall+=LSM.disk[LEVELN-1]->m_num;

	uint64_t all_storage=sizeofall*LSM.KEYNUM*PAGESIZE;
	uint64_t all_simul_storage=sizeofall*LSM.FLUSHNUM*PAGESIZE;
	printf("level:%d sizefactor:%d\n",LEVELN,SIZEFACTOR);
	printf("all level size:%lu(MB), %lf(GB)   ------------> simulated:%lu(MB) %lu(GB) %lu(TB)\n",all_storage/M,(double)all_storage/G,all_simul_storage/M,all_simul_storage/G,all_simul_storage/T);
	printf("all level header size: %lu(MB), except last header: %lu(MB)\n",sizeofall*PAGESIZE/M,(sizeofall-LSM.disk[LEVELN-1]->m_num)*PAGESIZE/M);
	printf("WRITE WAF:%f\n",(float)SIZEFACTOR * LEVELN /LSM.FLUSHNUM);
	printf("top level size:%d(MB)\n",LSM.disk[0]->m_num*PAGESIZE/M);
	printf("blommfileter : %fMB\n",(float)bloomfilter_memory/M);

#ifdef LEVELCACHING
	printf("level cache :%fMB\n",(float)lev_caching_mem/M);
#endif

	pthread_mutex_init(&LSM.templock,NULL);
	pthread_mutex_init(&LSM.memlock,NULL);
	pthread_mutex_init(&LSM.entrylock,NULL);
	pthread_mutex_init(&LSM.valueset_lock,NULL);
	for(int i=0; i< LEVELN; i++){
		pthread_mutex_init(&LSM.level_lock[i],NULL);
	}
	compaction_init();
#ifdef DVALUE
	factory_init();
#endif
	q_init(&LSM.re_q,RQSIZE);
#ifdef CACHE
	LSM.lsm_cache=cache_init();
#endif
	LSM.caching_value=NULL;
	LSM.li=li;
	algo_lsm.li=li;
	pm_init();
#ifdef NOCPY
	nocpy_init();
#endif
	return 0;
}

uint32_t __lsm_create_normal(lower_info *li, algorithm *lsm){
	measure_init(&__get_mt);
	measure_init(&__get_mt2);
	lsm_bind_ops(&LSM);
	LSM.memtable=skiplist_init();
	SIZEFACTOR=get_sizefactor(TOTALSIZE);
	unsigned long long sol;
#ifdef MONKEY
	int32_t SIZEFACTOR2=ceil(pow(10,log10(TOTALSIZE/PAGESIZE/LSM.KEYNUM/LEVELN)/(LEVELN-1)));
	float ffpr=RAF*(1-SIZEFACTOR2)/(1-pow(SIZEFACTOR2,LEVELN-1));
#endif
	float target_fpr=0;
	uint64_t sizeofall=0;
#ifdef LEVELCACHING
	uint64_t lev_caching_mem=0;
	//sol=SIZEFACTOR*caching_size;
#endif
//#else
	sol=SIZEFACTOR;

	printf("LSM KEYNUM:%d FLUSHNUM:%d\n",LSM.KEYNUM,LSM.FLUSHNUM);
	for(int i=0; i<LEVELN-1; i++){//for lsmtree -1 level
		LSM.disk[i]=LSM.lop->init(sol,i,target_fpr,false);
#ifdef BLOOM
	#ifdef MONKEY
		target_fpr=pow(SIZEFACTOR2,i)*ffpr;
	#else
		target_fpr=(float)RAF/LEVELN;
	#endif
		LSM.disk[i]->fpr=target_fpr;
#endif
		printf("[%d] fpr:%lf bytes per entry:%lu noe:%d\n",i+1,target_fpr,bf_bits(LSM.KEYNUM,target_fpr), LSM.disk[i]->m_num);
		sizeofall+=LSM.disk[i]->m_num;
#ifdef LEVELCACHING
		lev_caching_mem+=LSM.disk[i]->m_num;
#endif

		bloomfilter_memory+=bf_bits(LSM.KEYNUM,target_fpr)*sol;
		sol*=SIZEFACTOR;
		LSM.level_addr[i]=(PTR)LSM.disk[i];
	}   

#ifdef TIERING
	LSM.disk[LEVELN-1]=LSM.lop->init(sol,LEVELN-1,1,true);
#else
	LSM.disk[LEVELN-1]=LSM.lop->init(sol,LEVELN-1,1,false);
#endif
	printf("[%d] fpr:1.0000 bytes per entry:%lu noe:%d\n",LEVELN,bf_bits(LSM.KEYNUM,1),LSM.disk[LEVELN-1]->m_num);
	sizeofall+=LSM.disk[LEVELN-1]->m_num;
	printf("level:%d sizefactor:%d\n",LEVELN,SIZEFACTOR);
	printf("all level size:%lu(MB), %lf(GB)\n",sizeofall*8*M,(double)sizeofall*8*M/G);
	printf("all level header size: %lu(MB), except last header: %lu(MB)\n",sizeofall*PAGESIZE/M,(sizeofall-LSM.disk[LEVELN-1]->m_num)*PAGESIZE/M);
	printf("WRITE WAF:%f\n",(float)SIZEFACTOR * LEVELN /LSM.KEYNUM);
	printf("top level size:%d(MB)\n",LSM.disk[0]->m_num*8);
	printf("blommfileter : %fMB\n",(float)bloomfilter_memory/1024/1024);

#ifdef LEVELCACHING
	printf("level cache :%dMB %.2f(%%)\n",lev_caching_mem*PAGESIZE/M,(float)lev_caching_mem/sizeofall*100);
#endif

	pthread_mutex_init(&LSM.templock,NULL);
	pthread_mutex_init(&LSM.memlock,NULL);
	pthread_mutex_init(&LSM.entrylock,NULL);
	pthread_mutex_init(&LSM.valueset_lock,NULL);
	for(int i=0; i< LEVELN; i++){
		pthread_mutex_init(&LSM.level_lock[i],NULL);
	}
	compaction_init();
#ifdef DVALUE
	factory_init();
#endif
	q_init(&LSM.re_q,RQSIZE);
#ifdef CACHE
	LSM.lsm_cache=cache_init();
#endif
	LSM.caching_value=NULL;
	LSM.li=li;
	algo_lsm.li=li;
	pm_init();
#ifdef NOCPY
	nocpy_init();
#endif
	return 0;
}


extern uint32_t data_gc_cnt,header_gc_cnt,block_gc_cnt;
void lsm_destroy(lower_info *li, algorithm *lsm){
	lsm_debug_print();

	compaction_free();
#ifdef DVALUE
	factory_free();
#endif
#ifdef CACHE
	cache_free(LSM.lsm_cache);
#endif
	printf("last summary-----\n");
	for(int i=0; i<LEVELN; i++){
		LSM.lop->release(LSM.disk[i]);
	}
	skiplist_free(LSM.memtable);
	if(LSM.temptable)
		skiplist_free(LSM.temptable);
	printf("data gc: %d\n",data_gc_cnt);
	printf("header gc: %d\n",header_gc_cnt);
	printf("block gc: %d\n",block_gc_cnt);
#ifdef NOCPY
	nocpy_free();
#endif

	printf("---------------------------hash_insert_cnt: %u\n",hash_insert_cnt);
}

extern pthread_mutex_t compaction_wait,gc_wait;
extern int epc_check,gc_read_wait;
#ifdef CACHE
extern int memcpy_cnt;
#endif
volatile int comp_target_get_cnt=0,gc_target_get_cnt;
extern pthread_cond_t factory_cond;
void* lsm_end_req(algo_req* const req){
	lsm_params *params=(lsm_params*)req->params;
	request* parents=req->parents;
	bool havetofree=true;
	void *req_temp_params=NULL;
	PTR target=NULL;
	htable **t_table=NULL;
	htable *table=NULL;
	//htable mapinfo;
#ifdef DVALUE
	block *bl=NULL;
#endif
	switch(params->lsm_type){
		case OLDDATA:
			//do nothing
			break;
		case HEADERR:
			if(!parents){ //end_req for compaction
				//mem cpy for compaction
				t_table=(htable**)params->target;
				table=*t_table;
#ifdef NOCPY
				nocpy_copy_to((char*)table->sets,params->ppa);
#else
				memcpy(table->sets,params->value->value,PAGESIZE);
#endif

				//htable_print(table,params->ppa);
				comp_target_get_cnt++;
#ifdef CACHE
				if(epc_check==comp_target_get_cnt+memcpy_cnt){
#else
				if(epc_check==comp_target_get_cnt){
#endif
#ifdef MUTEXLOCK
						pthread_mutex_unlock(&compaction_wait);
#endif
					}
					//have to free
					inf_free_valueset(params->value,FS_MALLOC_R);
			}
			else{
				if(!parents->isAsync){ // end_req for lsm_get
					dl_sync_arrive(&params->lock);
					havetofree=false;
				}
				else{
#ifdef CACHE
					((run_t*)params->entry_ptr)->isflying=2;
#endif
					while(1){
						if(inf_assign_try(parents)){
							break;
						}else{
							if(q_enqueue((void*)parents,LSM.re_q))
								break;
						}
					}
					//pthread_mutex_destroy(&params->lock);
				}
				parents=NULL;
			}
			break;
		case HEADERW:
			inf_free_valueset(params->value,FS_MALLOC_W);
			free(params->htable_ptr);
			break;
		case GCDR:
		case GCHR:
			target=(PTR)params->target;//gc has malloc in gc function
#ifdef NOCPY
			if(params->lsm_type==GCHR)
				nocpy_copy_to((char*)target,params->ppa);
#else
			memcpy(target,params->value->value,PAGESIZE);
#endif

			if(gc_read_wait==gc_target_get_cnt){
#ifdef MUTEXLOCK
				pthread_mutex_unlock(&gc_wait);
#elif defined(SPINLOCK)

#endif
			}
			inf_free_valueset(params->value,FS_MALLOC_R);
			gc_target_get_cnt++;
			break;
		case GCDW:
		case GCHW:
			inf_free_valueset(params->value,FS_MALLOC_W);
			break;
		case DATAR:
			req_temp_params=parents->params;
			if(req_temp_params){
				parents->type_ftl=((int*)req_temp_params)[2];
			}
			parents->type_lower=req->type_lower;
			free(req_temp_params);
#ifdef DVALUE
			if(!PBITFULL(parents->value->ppa,false)){//small data

				pthread_mutex_lock(&LSM.valueset_lock);
				if(!LSM.caching_value){
					LSM.caching_value=(PTR)malloc(PAGESIZE);
				}
				memcpy(LSM.caching_value,parents->value->value,PAGESIZE);
				pthread_mutex_unlock(&LSM.valueset_lock);
				params->lsm_type=SDATAR;
				ftry_assign(req); //return by factory thread
				return NULL;
			}
#endif
			break;
		case DATAW:
			inf_free_valueset(params->value,FS_MALLOC_W);
			break;
#ifdef DVALUE
		case BLOCKW:
			bl=(block*)params->htable_ptr;
			free(bl->length_data);
			bl->length_data=NULL;
			inf_free_valueset(params->value,FS_MALLOC_W);
			break;
		case BLOCKR:
			bl=(block*)params->htable_ptr;
			memcpy(bl->length_data,params->value->value,PAGESIZE);
			inf_free_valueset(params->value,FS_MALLOC_R);
			bl->isflying=false;

			pthread_mutex_unlock(&bl->lock);
			break;
#endif
		default:
			break;
	}
	if(parents)
		parents->end_req(parents);
	if(havetofree){
		free(params);
		free(req);
	}
	return NULL;
}

uint32_t lsm_set(request * const req){
	MS(&__get_mt);
	bench_algo_start(req);
#ifdef DEBUG
	printf("lsm_set!\n");
		printf("key : %u\n",req->key);//for debug
#endif

	compaction_check();
	MS(&__get_mt2);
	if(req->type==FS_DELETE_T){
		skiplist_insert(LSM.memtable,req->key,req->value,false);
	}
	else{
		skiplist_insert(LSM.memtable,req->key,req->value,true);
	}
	MA(&__get_mt2);

	req->value=NULL;
	//req->value will be ignored at free
	MP(&req->latency_ftl);
	bench_algo_end(req);
	req->end_req(req); //end write

	MA(&__get_mt);
	if(LSM.memtable->size==LSM.KEYNUM)
		return 1;
	else
		return 0;
}
int nor;
MeasureTime lsm_mt;
uint32_t lsm_get(request *const req){
	void *re_q;
	static bool temp=false;
	static bool level_show=false;
	static bool debug=false;
	uint32_t res_type=0;
	if(!level_show){
		level_show=true;
		measure_init(&lsm_mt);
#ifdef CACHE
//		cache_print(LSM.lsm_cache);
#endif
		//readlockbywrite=0;
	}

	//printf("seq: %d, key:%u\n",nor++,req->key);
	while(1){
		if((re_q=q_dequeue(LSM.re_q))){
			request *tmp_req=(request*)re_q;
			bench_algo_start(tmp_req);
			res_type=__lsm_get(tmp_req);
			if(res_type==0){
				//printf("from req not found seq: %d, key:%u\n",nor++,req->key);
	//			LSM.lop->all_print();
				tmp_req->type=FS_NOTFOUND_T;
				tmp_req->end_req(tmp_req);
			//	abort();
			}
		}
		else 
			break;
	}
	if(!temp){
	//	LSM.lop->all_print();
		temp=true;
	}
	bench_algo_start(req);
	res_type=__lsm_get(req);
	if(!debug && LSM.disk[0]->n_num>0){
		debug=true;
	}
	if(res_type==0){
		//printf("not found seq: %d, key:%u\n",nor++,req->key);
//		LSM.lop->all_print();
		req->type=FS_NOTFOUND_T;
		req->end_req(req);
		//abort();
	}
	return res_type;
}

algo_req* lsm_get_req_factory(request *parents){
	algo_req *lsm_req=(algo_req*)malloc(sizeof(algo_req));
	lsm_params *params=(lsm_params*)malloc(sizeof(lsm_params));
	params->lsm_type=DATAR;
	lsm_req->params=params;
	lsm_req->parents=parents;
	dl_sync_init(&params->lock,1);
	lsm_req->end_req=lsm_end_req;
	lsm_req->type_lower=0;
	lsm_req->rapid=true;
	lsm_req->type=DATAR;
	return lsm_req;
}
int __lsm_get_sub(request *req,run_t *entry, keyset *table,skiplist *list){
	int res=0;
	if(!entry && !table && !list){
		return 0;
	}
	KEYT ppa;
	algo_req *lsm_req=NULL;
	snode *target_node;
	keyset *target_set;
	if(list){//skiplist check for memtable and temp_table;
		target_node=skiplist_find(list,req->key);
		if(!target_node) return 0;
		bench_cache_hit(req->mark);
		if(target_node->value){
			memcpy(req->value->value,target_node->value->value,PAGESIZE);
			bench_algo_end(req);
			req->end_req(req);
			return 2;
		}
		else{
			lsm_req=lsm_get_req_factory(req);
			req->value->ppa=target_node->ppa;
			ppa=target_node->ppa;
			res=4;
		}
	}

	if(entry && !table){ //tempent check
		target_set=LSM.lop->find_keyset((char*)entry->cpt_data->sets,req->key);
		if(target_set){
			lsm_req=lsm_get_req_factory(req);
			bench_cache_hit(req->mark);	
			req->value->ppa=target_set->ppa;
			ppa=target_set->ppa;
			res=4;
		}
	}
	if(!res && table){//retry check or cache hit check
#ifdef NOCPY
		table=(keyset*)nocpy_pick(entry->pbn);
#endif
		target_set=LSM.lop->find_keyset((char*)table,req->key);
		if(target_set){
#if defined(CACHE) && !defined(FLASHCECK)
			if(entry && !entry->t_table){
				htable temp; temp.sets=table;
				entry->t_table=htable_copy(&temp);
				cache_entry *c_entry=cache_insert(LSM.lsm_cache,entry,0);
				entry->c_entry=c_entry;
			}
#endif
			lsm_req=lsm_get_req_factory(req);
			req->value->ppa=target_set->ppa;
			ppa=target_set->ppa;
			res=4;
		}
	}

	if(lsm_req==NULL){
		return 0;
	}
	bench_algo_end(req);
	if(ppa==UINT_MAX){
		free(lsm_req->params);
		free(lsm_req);
		req->end_req(req);
		return 5;
	}

	if(lsm_req){
#ifdef DVALUE
		LSM.li->pull_data(ppa/(PAGESIZE/PIECE),PAGESIZE,req->value,ASYNC,lsm_req);
#else
		LSM.li->pull_data(ppa,PAGESIZE,req->value,ASYNC,lsm_req);
#endif
	}
	return res;
}
void dummy_htable_read(KEYT pbn,request *req){
	algo_req *lsm_req=lsm_get_req_factory(req);
	lsm_params *params=(lsm_params*)lsm_req->params;
	lsm_req->type=HEADERR;
	params->lsm_type=HEADERR;
	params->ppa=pbn;
	LSM.li->pull_data(params->ppa,PAGESIZE,req->value,ASYNC,lsm_req);
}
uint32_t __lsm_get(request *const req){
	/*memtable*/
	int res=__lsm_get_sub(req,NULL,NULL,LSM.memtable);
	if(res)return res;

	//check cached data
	pthread_mutex_lock(&LSM.valueset_lock);
	pthread_mutex_unlock(&LSM.valueset_lock);


	pthread_mutex_lock(&LSM.templock);
	res=__lsm_get_sub(req,NULL,NULL,LSM.temptable);
	pthread_mutex_unlock(&LSM.templock);
	if(res) return res;

	pthread_mutex_lock(&LSM.entrylock);
	res=__lsm_get_sub(req,LSM.tempent,NULL,NULL);
	pthread_mutex_unlock(&LSM.entrylock);
	if(res) return res;


	int level;
	int run;
	int round;
#if (LEVELN!=1)
	run_t** entries;
	run_t *entry;
	bool comback_req=false;
#endif
	htable mapinfo;

	if(req->params==NULL){
		int *_temp_data=(int *)malloc(sizeof(int)*3);
		req->params=(void*)_temp_data;
		_temp_data[0]=run=0;
		_temp_data[1]=round=0;
		_temp_data[2]=level=0;
	}
	else{
		int *temp_req=(int*)req->params;
		level=temp_req[0];
		run=temp_req[1];
		round=temp_req[2];

		mapinfo.sets=(keyset*)req->value->value;
#if (LEVELN==1)
		KEYT offset=req->key%LSM.KEYNUM;
		algo_req *lsm_req=lsm_get_req_factory(req);
		LSM.li->pull_data(mapinfo.sets[offset].ppa,PAGESIZE,req->value,ASYNC,lsm_req);
		return 1;
#else

#ifdef LEVELEMUL

		KEYT tppa=find_S_ent(&LSM.disk[level]->o_ent[run],req->key);
		if(tppa!=UINT_MAX){
			algo_req *mreq=lsm_get_req_factory(req);
			LSM.li->pull_data(tppa,PAGESIZE,req->value,ASYNC,mreq);
			return 1;
		}
		level++;
#else
		run_t **_entry=LSM.lop->find_run(LSM.disk[level],req->key);
	#ifdef CACHE
		pthread_mutex_lock(&LSM.lsm_cache->cache_lock);
	#endif
		res=__lsm_get_sub(req,_entry[run],mapinfo.sets,NULL);	
	#ifdef CACHE
		pthread_mutex_unlock(&LSM.lsm_cache->cache_lock);
		_entry[run]->req=NULL;
		_entry[run]->isflying=0;
	#endif
		free(_entry);
		if(res)return res;
#endif

#ifndef FLASHCHCK
		run+=1;
#endif

		comback_req=true;
#endif
	}
	
	for(int i=level; i<LEVELN; i++){
#ifdef LEVELCACHING
		if(i<LEVELCACHING){
			pthread_mutex_lock(&LSM.level_lock[i]);
			keyset *find=LSM.lop->cache_find(LSM.disk[i],req->key);
			pthread_mutex_unlock(&LSM.level_lock[i]);
			if(find){
				algo_req *lsm_req=lsm_get_req_factory(req);
				req->value->ppa=find->ppa;
#ifdef DVALUE
				LSM.li->pull_data(find->ppa/(PAGESIZE/PIECE),PAGESIZE,req->value,ASYNC,lsm_req);
#else
				LSM.li->pull_data(find->ppa,PAGESIZE,req->value,ASYNC,lsm_req);
#endif
				res=1;
			}
			if(res) return res;
			else continue;
		}
#endif

#ifdef LEVELEMUL
		int *temp_data=(int*)req->params;
		temp_data[0]=i;
		temp_data[1]=0;
		round++;
		temp_data[2]=round;
		o_entry *toent=find_O_ent(LSM.disk[i],req->key,(uint32_t*)&temp_data[1]);
		if(toent && toent->pba!=UINT_MAX){
			bench_algo_end(req);
			dummy_htable_read(toent->pba,req);
			return 3;
		}
		else continue;
#endif

#if (LEVELN==1)
		KEYT p=req->key/LSM.KEYNUM;
		KEYT target_ppa=LSM.disk[i]->o_ent[p].pba;	
		int *temp_data=(int*)req->params;
		temp_data[0]=0;
		temp_data[1]=0;
		round++;
		temp_data[2]=round;
#else
		pthread_mutex_lock(&LSM.level_lock[i]);
		entries=LSM.lop->find_run(LSM.disk[i],req->key);
		pthread_mutex_unlock(&LSM.level_lock[i]);
		if(!entries){
			continue;
		}
		if(comback_req && level!=i){
			run=0;
		}
		for(int j=run; entries[j]!=NULL; j++){
			entry=entries[j];
			//read mapinfo
			int *temp_data=(int*)req->params;
			temp_data[0]=i;
			temp_data[1]=j;
			round++;
			temp_data[2]=round;
#ifdef FLASHCHECK
			pthread_mutex_lock(&LSM.lsm_cache->cache_lock);
			if(comback_req && entry->c_entry){
				res=__lsm_get_sub(req,NULL,entry->t_table->sets,NULL);
				pthread_mutex_unlock(&LSM.lsm_cache->cache_lock);
				if(res){ 
					free(entries);
					return res;
				}
				continue;
			}
			pthread_mutex_unlock(&LSM.lsm_cache->cache_lock);
#elif defined(CACHE)
			pthread_mutex_lock(&LSM.lsm_cache->cache_lock);
			if(entry->c_entry){
				res=__lsm_get_sub(req,NULL,entry->t_table->sets,NULL);
				if(res){
					cache_update(LSM.lsm_cache,entry);
					free(entries);
					pthread_mutex_unlock(&LSM.lsm_cache->cache_lock);
					return res;
				}
				pthread_mutex_unlock(&LSM.lsm_cache->cache_lock);
				continue;
			}
			pthread_mutex_unlock(&LSM.lsm_cache->cache_lock);
#endif

#ifdef BLOOM
			if(!bf_check(entry->filter,req->key)){
				continue;
			}
#endif

#endif
			algo_req *lsm_req=lsm_get_req_factory(req);
			lsm_params* params=(lsm_params*)lsm_req->params;
			lsm_req->type=HEADERR;
			params->lsm_type=HEADERR;
#if (LEVELN==1)
			params->ppa=target_ppa;
#else
			params->ppa=entry->pbn;
	#ifdef CACHE
			if(entry->isflying==1 || entry->isflying==2){
				while(entry->isflying!=2){}//wait for mapping
				request *__req=(request*)entry->req;
				mapinfo.sets=(keyset*)__req->value->value;
				pthread_mutex_lock(&LSM.lsm_cache->cache_lock);
				res=__lsm_get_sub(req,entry,mapinfo.sets,NULL);
				pthread_mutex_unlock(&LSM.lsm_cache->cache_lock);
				entry->isflying=0;
				if(res){ 
					free(entries);
					return res;
				}
				else{
					free(params);
					free(lsm_req);
					continue;
				}
			}
			else{
				entry->isflying=1;
				entry->req=(void*)req;
				params->entry_ptr=(void*)entry;
			}
	#endif

#endif
			LSM.li->pull_data(params->ppa,PAGESIZE,req->value,ASYNC,lsm_req);
			__header_read_cnt++;

#if (LEVELN!=1)
			if(!req->isAsync){
				dl_sync_wait(&params->lock); // wait until read table data;
				mapinfo.sets=(keyset*)req->value->value;
				res=__lsm_get_sub(req,NULL,mapinfo.sets,NULL);
				if(!res){
					continue; // check next entry
				}else{
					free(entries);
				}
			}
			else{
				free(entries);
				bench_algo_end(req);
				return 3; //async
			}
		}

		free(entries);
#else
		return 3;
#endif
	}
	bench_algo_end(req);
	return res;
}

uint32_t lsm_remove(request *const req){
	return lsm_set(req);
}

htable *htable_assign(char *cpy_src, bool using_dma){
	htable *res=(htable*)malloc(sizeof(htable));
	if(!using_dma){
		res->sets=(keyset*)malloc(PAGESIZE);
		res->t_b=0;
		res->origin=NULL;
		if(cpy_src) memcpy(res->sets,cpy_src,PAGESIZE);
		else memset(res->sets,-1,PAGESIZE);
	}else{
		value_set *temp;
		if(cpy_src) temp=inf_get_valueset(cpy_src,FS_MALLOC_W,PAGESIZE);
		else temp=inf_get_valueset(NULL,FS_MALLOC_W,PAGESIZE);
		res->t_b=FS_MALLOC_W;
		res->sets=(keyset*)temp->value;
		res->origin=temp;
	}
	return res;
}

void htable_free(htable *input){
	if(input->t_b){
		inf_free_valueset(input->origin,input->t_b);
	}else{
		free(input->sets);
		free(input);
	}
}

htable *htable_copy(htable *input){
	htable *res=(htable*)malloc(sizeof(htable));
	res->sets=(keyset*)malloc(PAGESIZE);
	memcpy(res->sets,input->sets,PAGESIZE);
	res->t_b=0;
	res->origin=NULL;
	return res;
}

void htable_print(htable * input,KEYT ppa){
	bool check=false;
	int cnt=0;
	for(uint32_t i=0; i<FULLMAPNUM; i++){
		if(input->sets[i].lpa>RANGE && input->sets[i].lpa!=UINT_MAX){
			printf("bad reading %u\n",input->sets[i].lpa);
			cnt++;
			check=true;
		}
		else{
			continue;
		}
		printf("[%d] %u %u\n",i, input->sets[i].lpa, input->sets[i].ppa);
	}
	if(check){
		printf("bad page at %d cnt:%d---------\n",ppa,cnt);
		abort();
	}
}
