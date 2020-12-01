#include "array.h"
#include "../../level.h"
#include "../../bloomfilter.h"
#include "../../lsmtree.h"
#include "../../../../interface/interface.h"
#include "../../../../interface/koo_hg_inf.h"
#include "../../../../include/utils/kvssd.h"
#include "../../../../include/settings.h"
#include "array.h"
extern lsmtree LSM;

void array_tier_align( level *lev){
	printf("this is empty\n");
}
/*
static int binary_search_in_runs(run_t *arrs, int max){
}
*/

static int test_run(run_t *target, run_t *tt){
	if(KEYCMP(target->key,tt->end)>0) return 1;
	if(KEYCMP(target->end, tt->key)<0) return -1;
	return 0;
}

int32_t array_chk_overlap_run( level *des, level *src, KEYT start, KEYT end){
	if(!(des && src))  return -1;
	int upper_max=src->n_num;
	int lower_max=des->n_num;

	run_t *up_body=((array_body*)src->level_data)->arrs;
	run_t *lo_body=((array_body*)des->level_data)->arrs;

	run_t* up, *down_now, *down_nxt;
	uint32_t before_idx=0;
	int res1, res2;
	uint32_t res=0;
	array_print(des);
	array_print(src);
	for(uint32_t i=0; i<upper_max; i++){
		up=&up_body[i];
		for(int j=before_idx; j<lower_max-1; j++){
			down_now=&lo_body[j];
			down_nxt=&lo_body[j+1];

			res1=test_run(up, down_now);
			if(!res1){
				before_idx=j-1>0?j-1:0;
				break;
			}
			res2=test_run(up, down_nxt);
			if(res1==1 && res2==-1){
				res++;
				before_idx=j+1;
				break;
			}
			before_idx=j-1>0?j-1:0;
		}
	}
	return res;
}

bool array_chk_overlap(level * lev, KEYT start, KEYT end){
#ifdef KVSSD
	if(KEYCMP(lev->start,end)>0 || KEYCMP(lev->end,start)<0)
#else
	if(lev->start > end || lev->end < start)
#endif
	{
		return false;
	}

#ifdef KOO
	/*
	printf("--------------------------\n");
	char buf[100], buf2[100];
	key_interpreter(lev->start, buf);
	key_interpreter(lev->end, buf2);
	printf("lev:\t%s ~ %s\n",buf, buf2);
	key_interpreter(start, buf);
	key_interpreter(end, buf2);
	printf("target:\t%s ~ %s\n",buf, buf2);*/
#endif

	return true;
}

run_t *array_range_find_lowerbound(level *lev, KEYT target){
	array_body *b=(array_body*)lev->level_data;
	run_t *arrs=b->arrs;
	int target_idx=array_bound_search(arrs,lev->n_num,target,true);
	if(target_idx==-1) return NULL;
	return &arrs[target_idx];
	/*
	for(int i=target_idx; i<lev->n_num; i++){
		ptr=&arrs[i];
#ifdef KVSSD
		if(KEYCMP(ptr->key,target)<=0 && KEYCMP(ptr->end,target)>=0)
#else
		if(ptr->key <= target && ptr->end >= target)
#endif
		{
			return ptr;
		}
	}
	return NULL;*/
}
#ifdef BLOOM
htable *array_mem_cvt2table(skiplist* mem,run_t* input,BF *filter)
#else
htable *array_mem_cvt2table(skiplist*mem,run_t* input)
#endif
{
	/*static int cnt=0;
	eprintf("cnt:%d\n",cnt++);*/
	htable *res=ISNOCPY(LSM.setup_values)?htable_assign(NULL,0):htable_assign(NULL,1);
	if(input)
		input->cpt_data=res;
#ifdef KVSSD
	snode *temp;
	char *ptr=(char*)res->sets;
	uint16_t *bitmap=(uint16_t*)ptr;
	uint32_t idx=1;
	memset(bitmap,-1,KEYBITMAP/sizeof(uint16_t));
	uint16_t data_start=KEYBITMAP;
	uint16_t added_length=0;
	char *now_ptr;
	bitmap[0]=mem->size;
	bool header_print_flag=false;
	for_each_sk(mem,temp){
		//printf("idx:%d data_start:%d key_len:%d\n",idx,data_start,temp->key.len);
		if(input && idx==1){
			kvssd_cpy_key(&input->key,&temp->key);
		}
		else if(input && idx==mem->size){
			kvssd_cpy_key(&input->end,&temp->key);
		}

		added_length=0;
		now_ptr=&ptr[data_start];

		if(temp->key.key[0]=='d'){
			header_print_flag=true;
		}
#ifdef META_UNSEP
		if(temp->ppa!=TOMBSTONE && temp->key.key[0]=='m'){
			now_ptr[added_length++]=KVUNSEP;
			memcpy(&now_ptr[added_length], &temp->value.u_value->length, sizeof(uint32_t));
			added_length+=sizeof(uint32_t);
			memcpy(&now_ptr[added_length], temp->value.u_value->value, temp->value.u_value->length);
			added_length+=temp->value.u_value->length;
		}
		else
#endif
		{
			now_ptr[added_length++]=KVSEP;
			memcpy(&now_ptr[added_length],&temp->ppa,sizeof(temp->ppa));
			added_length+=sizeof(temp->ppa);
		}
	
		if(temp->key.len+added_length+data_start>PAGESIZE){
			printf("overflow!! %s:%d\n", __FILE__, __LINE__);
			abort();
		}
		memcpy(&now_ptr[added_length],temp->key.key,temp->key.len);
		added_length+=temp->key.len;

		bitmap[idx]=data_start;
#ifdef BLOOM
		if(filter)bf_set(filter,temp->key);
#endif
		data_start+=added_length;
		//free(temp->key.key);
		idx++;
	}
	bitmap[idx]=data_start;
	if(header_print_flag){
	//	array_header_print(ptr);
	}
#else
	not implemented
#endif
	return res;
}
//static int merger_cnt;

BF* array_making_filter(run_t *data,int num, float fpr){
	BF *filter=bf_init(KEYBITMAP/sizeof(uint16_t),fpr);
	char *body=data_from_run(data);
	int idx;
	uint16_t *bitmap=(uint16_t*)body;
	p_entry pent;
	for_each_header_start(idx,pent,bitmap,body)
		bf_set(filter,pent.key);
	for_each_header_end
	return filter;
}



static char *make_rundata_from_snode(snode *temp){
	char *res=(char*)malloc(PAGESIZE);
	char *ptr=res;
	uint16_t *bitmap=(uint16_t*)ptr;
	uint32_t idx=1;
	memset(bitmap,-1,KEYBITMAP/sizeof(uint16_t));
	uint16_t data_start=KEYBITMAP;
	uint32_t length=0;
	do{
		memcpy(&ptr[data_start],&temp->ppa,sizeof(temp->ppa));
		memcpy(&ptr[data_start+sizeof(temp->ppa)],temp->key.key,temp->key.len);
		bitmap[idx]=data_start;

		data_start+=temp->key.len+sizeof(temp->ppa);
		length+=KEYLEN(temp->key);
		idx++;
		temp=temp->list[1];
	}
	while(temp && length+KEYLEN(temp->key)<=PAGESIZE-KEYBITMAP);
	bitmap[0]=idx-1;
	bitmap[idx]=data_start;
	return res;
}

void array_header_print(char *data){
	int idx;
	ppa_t *ppa;
	uint16_t *bitmap;
	char *body;
#ifdef KOO
	char buf[100];
#endif
	body=data;
	bitmap=(uint16_t*)body;
	printf("header_num:%d : %p\n",bitmap[0],data);
	p_entry pent;
	for_each_header_start(idx,pent,bitmap,body)
#ifdef DVALUE
	#ifdef KOO
		key_interpreter(pent.key, buf);
		fprintf(stderr,"[%d] %d key:%s ppa:%u\n",pent.type,idx,buf, pent.info.ppa);
	#else
		fprintf(stderr,"[type-%d %d:%d] key(%p):%.*s(%d),%u\n",pent.type, idx,bitmap[idx],&data[bitmap[idx]],pent.key.len, pent.key.key, pent.len, pent.info.ppa);
	#endif
#else
		abort();
//		fprintf(stderr,"[%d:%d] key(%p):%.*s(%d) ,%u\n",idx,bitmap[idx],&data[bitmap[idx]],key.len,key.key,key.len,*ppa);
#endif
	for_each_header_end
	printf("header_num:%d : %p\n",bitmap[0],data);
}


run_t *array_next_run(level *lev,KEYT key){
	array_body *b=(array_body*)lev->level_data;
	run_t *arrs=b->arrs;
	int target_idx=array_binary_search(arrs,lev->n_num,key);
	if(target_idx==-1) return NULL;
	if(target_idx+1<lev->n_num){
		return &arrs[target_idx+1];
	}
	return NULL;
}

typedef struct header_iter{
	char *header_data;
	uint32_t idx;
}header_iter;

//keyset_iter* array_header_get_keyiter(level *lev, char *data,KEYT *key){
//	keyset_iter *res=(keyset_iter*)malloc(sizeof(keyset_iter));
//	header_iter *p_data=(header_iter*)malloc(sizeof(header_iter));
//	res->private_data=(void*)p_data;
//	if(!data){	
//		array_body *b=(array_body*)lev->level_data;
//		run_t *arrs=b->arrs;
//		int target=array_binary_search(arrs,lev->n_num,*key);
//		if(target==-1) p_data->header_data=NULL;
//		else{
//			p_data->header_data=arrs[target].level_caching_data;
//		}
//	}
//	else{
//		p_data->header_data=data;
//	}
//	data=p_data->header_data;
//	if(key==NULL)
//		p_data->idx=0;
//	else if(data)
//		p_data->idx=array_find_idx_lower_bound(data,*key);
//	else return NULL;
//
//	return res;
//}
//
//keyset array_header_next_key(level *lev, keyset_iter *k_iter){
//	header_iter *p_data=(header_iter*)k_iter->private_data;
//	keyset res;
//	res.ppa=-1;
//
//	if(p_data!=NULL){
//		if(GETNUMKEY(p_data->header_data)>p_data->idx){
//			uint16_t *bitmap=GETBITMAP(p_data->header_data);
//			char *data=p_data->header_data;	
//			int idx=p_data->idx;
//			res.ppa=*((ppa_t*)&data[bitmap[idx]]);
//			res.lpa.key=((char*)&(data[bitmap[idx]+sizeof(ppa_t)]));	
//			res.lpa.len=bitmap[idx+1]-bitmap[idx]-sizeof(ppa_t);
//			p_data->idx++;
//		}
//		else{
//			free(p_data);
//			k_iter->private_data=NULL;
//		}
//	}
//	return res;
//}
//
//void array_header_next_key_pick(level *lev, keyset_iter * k_iter,keyset *res){
//	header_iter *p_data=(header_iter*)k_iter->private_data;
//	if(GETNUMKEY(p_data->header_data)>p_data->idx){
//		uint16_t *bitmap=GETBITMAP(p_data->header_data);
//		char *data=p_data->header_data;	
//		int idx=p_data->idx;
//		res->ppa=*((ppa_t*)&data[bitmap[idx]]);
//		res->lpa.key=((char*)&(data[bitmap[idx]+sizeof(ppa_t)]));	
//		res->lpa.len=bitmap[idx+1]-bitmap[idx]-sizeof(ppa_t);
//	}
//	else{
//		res->ppa=-1;
//	}
//}

void array_normal_merger(skiplist *skip,run_t *r,bool iswP){

//	ppa_t *ppa_ptr;
//	KEYT key;
//	char* body;
//	int idx;
//	body=data_from_run(r);
//	uint16_t *bitmap=(uint16_t*)body;
//	for_each_header_start(idx,key,ppa_ptr,bitmap,body)
//		if(iswP){
//			skiplist_insert_wP(skip,key,*ppa_ptr,*ppa_ptr==UINT32_MAX?false:true);
//		}
//		else
//			skiplist_insert_existIgnore(skip,key,*ppa_ptr,*ppa_ptr==UINT32_MAX?false:true);
//	for_each_header_end
}


void array_checking_each_key(char *data,void*(*test)(map_entry ent)){
	int idx;
	uint16_t *bitmap=(uint16_t *)data;
	p_entry pent;
	for_each_header_start(idx,pent,bitmap,data)
		/*
		if(key.len > 100){
			array_header_print(data);
			abort();
		}*/
		test(pent);
	for_each_header_end
}

int array_cache_comp_formatting(level *lev ,run_t ***des, bool des_cache){
	array_body *b=(array_body*)lev->level_data;
	run_t *arrs=b->arrs;
	//static int cnt=0;
	//can't caculate the exact nubmer of run...
	run_t **res=(run_t**)malloc(sizeof(run_t*)*(lev->n_num+1));
	
	for(int i=0; i<lev->n_num; i++){
		if(des_cache){
			res[i]=&arrs[i];
		}else{
			res[i]=array_make_run(arrs[i].key,arrs[i].end,arrs[i].pbn);
			res[i]->cpt_data=ISNOCPY(LSM.setup_values)?htable_assign(arrs[i].level_caching_data,0):htable_assign(arrs[i].level_caching_data,1);
		}
	}
	res[lev->n_num]=NULL;
	*des=res;
	return lev->n_num;
}
