#include<string.h>
#include<stdio.h>
#include<stdlib.h>
#include<limits.h>
#include<unistd.h>
#include<sys/types.h>
#include"skiplist.h"
#include"variable.h"
#include"../../interface/interface.h"
#include "../../include/slab.h"
#ifdef Lsmtree
#include "lsmtree.h"
#include "level.h"
#include "page.h"

extern MeasureTime compaction_timer[3];
extern OOBT *oob;
extern lsmtree LSM;
#ifdef KVSSD
extern	KEYT key_max, key_min;
#endif

#endif
#ifdef USINGSLAB
//extern struct slab_chain snode_slab;
extern kmem_cache_t snode_slab;
#endif
skiplist *skiplist_init(){
	skiplist *point=(skiplist*)malloc(sizeof(skiplist));
	point->level=1;
#ifdef USINGSLAB
	//point->header=(snode*)slab_alloc(&snode_slab);
	point->header=(snode*)kmem_cache_alloc(snode_slab,KM_NOSLEEP);
#else
	point->header=(snode*)malloc(sizeof(snode));
#endif
	point->header->list=(snode**)malloc(sizeof(snode*)*(MAX_L+1));
	for(int i=0; i<MAX_L; i++) point->header->list[i]=point->header;
#if defined(KVSSD) && defined(Lsmtree)
	point->all_length=0;
	point->header->key=key_max;
	point->start=key_max;
	point->end=key_min;
#else
	point->header->key=UINT_MAX;
	point->start=UINT_MAX;
	point->end=0;
#endif
	point->header->value=NULL;
	point->size=0;
	return point;
}

snode *skiplist_find(skiplist *list, KEYT key){
	if(!list) return NULL;
	if(list->size==0) return NULL;
	snode *x=list->header;
	for(int i=list->level; i>=1; i--){
#if defined(KVSSD) && defined(Lsmtree)
		while(KEYCMP(x->list[i]->key,key)<0)
#else
		while(x->list[i]->key<key)
#endif
			x=x->list[i];
	}

#if defined(KVSSD) && defined(Lsmtree)
	if(KEYCMP(x->list[1]->key,key)==0)
#else
	if(x->list[1]->key==key)
#endif
		return x->list[1];
	return NULL;
}

snode *skiplist_find_lowerbound(skiplist *list, KEYT key){
	if(!list) return NULL;
	if(list->size==0) return NULL;
	snode *x=list->header;
	for(int i=list->level; i>=1; i--){

#if defined(KVSSD) && defined(Lsmtree)
		while(KEYCMP(x->list[i]->key,key)<0)
#else
		while(x->list[i]->key<key)
#endif
			x=x->list[i];
	}

	return x->list[1];
}

snode *skiplist_range_search(skiplist *list,KEYT key){
	if(list->size==0) return NULL;
	snode *x=list->header;
	snode *bf=list->header;
	for(int i=list->level; i>=1; i--){
#if defined(KVSSD) && defined(Lsmtree)
		while(KEYCMP(x->list[i]->key,key)<=0)
#else
		while(x->list[i]->key<=key)
#endif
		{
			bf=x;
			x=x->list[i];
		}
	}
	
	bf=x;
	x=x->list[1];
#if defined(KVSSD) && defined(Lsmtree)
	if(KEYCMP(bf->key,key)<=0 && KEYCMP(key,x->key)<0)
#else
	if(bf->key<=key && key< x->key)
#endif
	{
		return bf;
	}


#if defined(KVSSD) && defined(Lsmtree)
	if(KEYCMP(key,list->header->list[1]->key)<=0)
#else
	if(key<=list->header->list[1]->key)
#endif
	{
		return list->header->list[1];
	}
	return NULL;
}

snode *skiplist_strict_range_search(skiplist *list,KEYT key){
	if(list->size==0) return NULL;
	snode *x=list->header;
	snode *bf=list->header;
	for(int i=list->level; i>=1; i--){
#if defined(KVSSD) && defined(Lsmtree)
		while(KEYCMP(x->list[i]->key,key)<=0)
#else
		while(x->list[i]->key<=key)
#endif	
		{
			bf=x;
			x=x->list[i];
		}
	}
	
	bf=x;
	x=x->list[1];
#if defined(KVSSD) && defined(Lsmtree)
	if(KEYCMP(bf->key,key)<=0 && KEYCMP(key,x->key)<0)
#else
	if(bf->key<=key && key< x->key)
#endif
	{
		return bf;
	}

#if defined(KVSSD) && defined(Lsmtree)
	else if(KEYCMP(bf->key,key_max)==0)
#else
	else if(bf->key==UINT_MAX)
#endif
	{
		return x;
	}
	return NULL;
}

static int getLevel(){
	int level=1;
	int temp=rand();
	while(temp % PROB==1){
		temp=rand();
		level++;
		if(level+1>=MAX_L) break;
	}
	return level;
}

#ifdef Lsmtree
snode *skiplist_insert_wP(skiplist *list, KEYT key, ppa_t ppa,bool deletef){
#if !(defined(KVSSD) && defined(Lsmtree))
	if(key>RANGE){
		printf("bad page read key:%u\n",key);
		return NULL;
	}
#endif
	snode *update[MAX_L+1];
	snode *x=list->header;


	for(int i=list->level; i>=1; i--){
#if defined(KVSSD) && defined(Lsmtree)
		while(KEYCMP(x->list[i]->key,key)<0)
#else
		while(x->list[i]->key<key)
#endif
			x=x->list[i];
		update[i]=x;
	}
	
	x=x->list[1];

#if defined(KVSSD) && defined(Lsmtree)
	if(KEYCMP(key,list->start)<0) list->start=key;
	if(KEYCMP(key,list->end)>0) list->end=key;
	if(KEYCMP(key,x->key)==0)
#else
	if(key<list->start) list->start=key;
	if(key>list->end) list->end=key;
	if(key==x->key)
#endif
	{
		//ignore new one;
#ifndef DVALUE
		invalidate_PPA(ppa);
#else
		invalidate_DPPA(ppa);
#endif
		return x;
	}
	else{
		int level=getLevel();
		if(level>list->level){
			for(int i=list->level+1; i<=level; i++){
				update[i]=list->header;
			}
			list->level=level;
		}
#ifdef USINGSLAB
	//	x=(snode*)slab_alloc(&snode_slab);
		x=(snode*)kmem_cache_alloc(snode_slab,KM_NOSLEEP);
#else
		x=(snode*)malloc(sizeof(snode));
#endif
		x->list=(snode**)malloc(sizeof(snode*)*(level+1));

#ifdef KVSSD
		list->all_length+=KEYLEN(key);
#endif
		x->key=key;
		x->ppa=ppa;
		x->isvalid=deletef;
		x->value=NULL;
		for(int i=1; i<=level; i++){
			x->list[i]=update[i]->list[i];
			update[i]->list[i]=x;
		}
		x->level=level;
		list->size++;
	}
	return x;
}

snode *skiplist_insert_existIgnore(skiplist *list,KEYT key,ppa_t ppa,bool deletef){
#ifndef KVSSD
	if(key>RANGE){
		printf("bad page read\n");
		return NULL;
	}
#endif
	snode *update[MAX_L+1];
	snode *x=list->header;
	
	for(int i=list->level; i>=1; i--){
#ifdef KVSSD
		while(KEYCMP(x->list[i]->key,key)<0)
#else
		while(x->list[i]->key<key)
#endif
			x=x->list[i];
		update[i]=x;
	}

	x=x->list[1];
#ifdef KVSSD
	if(KEYCMP(key,list->start)<0) list->start=key;
	if(KEYCMP(key,list->end)>0) list->end=key;
	if(KEYCMP(key,x->key)==0)
#else
	if(key<list->start) list->start=key;
	if(key>list->end) list->end=key;
	if(key==x->key)
#endif
	{
		//delete exists ppa; input ppa
#ifndef DVALUE
		invalidate_PPA(x->ppa);
#else
		/*
		if(x->ppa/NPCINPAGE/256==384){
			printf("%.*s\n",KEYFORMAT(x->key));
		}*/
		invalidate_DPPA(x->ppa);
#endif
		x->ppa=ppa;
		x->isvalid=deletef;
		return x;
	}
	else{
		
		int level=getLevel();
		if(level>list->level){
			for(int i=list->level+1; i<=level; i++){
				update[i]=list->header;
			}
			list->level=level;
		}
#ifdef USINGSLAB
		//x=(snode*)slab_alloc(&snode_slab);
		x=(snode*)kmem_cache_alloc(snode_slab,KM_NOSLEEP);
#else
		x=(snode*)malloc(sizeof(snode));
#endif
		x->list=(snode**)malloc(sizeof(snode*)*(level+1));

#ifdef KVSSD
		list->all_length+=KEYLEN(key);
#endif

		x->key=key;
		x->ppa=ppa;
		x->isvalid=deletef;
		x->value=NULL;
		for(int i=1; i<=level; i++){
			x->list[i]=update[i]->list[i];
			update[i]->list[i]=x;
		}
		x->level=level;
		list->size++;
	}
	return x;
}

snode *skiplist_general_insert(skiplist *list,KEYT key,void* value,void (*overlap)(void*)){
	snode *update[MAX_L+1];
	snode *x=list->header;
	
	for(int i=list->level; i>=1; i--){
#if defined(KVSSD) && defined(Lsmtree)
		while(KEYCMP(x->list[i]->key,key)<0)
#else
		while(x->list[i]->key<key)
#endif
			x=x->list[i];
		update[i]=x;
	}
	x=x->list[1];

	run_t *t_r=(run_t*)value;
#if defined(KVSSD) && defined(Lsmtree)
	if(KEYCMP(key,list->start)<0) list->start=key;
	if(KEYCMP(key,list->end)>0) list->end=key;
	if(KEYCMP(key,x->key)==0)
#else
	if(key<list->start) list->start=key;
	if(key>list->end) list->end=key;
	if(key==x->key)
#endif
	{
		if(overlap)
			overlap((void*)x->value);
		x->value=(value_set*)value;
		t_r->run_data=(void*)x;
		return x;
	}
	else{
		int level=getLevel();
		if(level>list->level){
			for(int i=list->level+1; i<=level; i++){
				update[i]=list->header;
			}
			list->level=level;
		}
#ifdef USINGSLAB
		//x=(snode*)slab_alloc(&snode_slab);
		x=(snode*)kmem_cache_alloc(snode_slab,KM_NOSLEEP);
#else
		x=(snode*)malloc(sizeof(snode));
#endif
		x->list=(snode**)malloc(sizeof(snode*)*(level+1));

		x->key=key;
		x->ppa=UINT_MAX;
		x->value=(value_set*)value;
		t_r->run_data=(void*)x;

		for(int i=1; i<=level; i++){
			x->list[i]=update[i]->list[i];
			update[i]->list[i]=x;
		}
		x->level=level;
		list->size++;
	}
	return x;

}
#endif
snode *skiplist_insert_iter(skiplist *list,KEYT key,ppa_t ppa){
	snode *update[MAX_L+1];
	snode *x=list->header;

	for(int i=list->level; i>=1; i--){
#if defined(KVSSD) && defined(Lsmtree)
		while(KEYCMP(x->list[i]->key,key)<0)
#else
		while(x->list[i]->key<key)
#endif
			x=x->list[i];
		update[i]=x;
	}
	x=x->list[1];
#if defined(KVSSD) && defined(Lsmtree)
	if(KEYCMP(key,list->start)<0) list->start=key;
	if(KEYCMP(key,list->end)>0) list->end=key;
	if(KEYCMP(key,x->key)==0)
#else
	if(key<list->start) list->start=key;
	if(key>list->end) list->end=key;
	if(key==x->key)
#endif
	{
#ifdef DEBUG

#endif
		x->ppa=ppa;
		return x;
	}
	else{
		int level=getLevel();
		if(level>list->level){
			for(int i=list->level+1; i<=level; i++){
				update[i]=list->header;
			}
			list->level=level;
		}
#ifdef USINGSLAB
	//	x=(snode*)slab_alloc(&snode_slab);
		x=(snode*)kmem_cache_alloc(snode_slab,KM_NOSLEEP);
#else
		x=(snode*)malloc(sizeof(snode));
#endif
		x->list=(snode**)malloc(sizeof(snode*)*(level+1));

		x->key=key;

		x->ppa=ppa;
		x->value=NULL;
		list->all_length+=key.len;
		// ++ ctoc
		x->t_ppa = -1;
		x->bypass = false;
		x->write_flying = false;
		// -- ctoc
		for(int i=1; i<=level; i++){
			x->list[i]=update[i]->list[i];
			update[i]->list[i]=x;
		}
		x->level=level;
		list->size++;
	}
	return x;
}
snode *skiplist_insert(skiplist *list,KEYT key,value_set* value, bool deletef){
	snode *update[MAX_L+1];
	snode *x=list->header;

	for(int i=list->level; i>=1; i--){
#if defined(KVSSD) && defined(Lsmtree)
		while(KEYCMP(x->list[i]->key,key)<0)
#else
		while(x->list[i]->key<key)
#endif
			x=x->list[i];
		update[i]=x;
	}
	x=x->list[1];
#if defined(KVSSD) && defined(Lsmtree)
	if(KEYCMP(key,list->start)<0) list->start=key;
	if(KEYCMP(key,list->end)>0) list->end=key;
#else
	if(key<list->start) list->start=key;
	if(key>list->end) list->end=key;
#endif
	if(value!=NULL){
		value->length=(value->length/PIECE)+(value->length%PIECE?1:0);
	}
#if defined(KVSSD) && defined(Lsmtree)
	if(KEYCMP(key,x->key)==0)
#else
	if(key==x->key)
#endif
	{
#ifdef DEBUG

#endif
	//	algo_req * old_req=x->req;
	//	lsm_params *old_params=(lsm_params*)old_req->params;
	//	old_params->lsm_type=OLDDATA;
		
		if(x->value)
			inf_free_valueset(x->value,FS_MALLOC_W);
	//	old_req->end_req(old_req);

		x->value=value;
		x->isvalid=deletef;
		return x;
	}
	else{
		int level=getLevel();
		if(level>list->level){
			for(int i=list->level+1; i<=level; i++){
				update[i]=list->header;
			}
			list->level=level;
		}
#ifdef USINGSLAB
	//	x=(snode*)slab_alloc(&snode_slab);
		x=(snode*)kmem_cache_alloc(snode_slab,KM_NOSLEEP);
#else
		x=(snode*)malloc(sizeof(snode));
#endif
		x->list=(snode**)malloc(sizeof(snode*)*(level+1));

		x->key=key;
		x->isvalid=deletef;

		x->ppa=UINT_MAX;
		x->value=value;

#ifdef KVSSD
		list->all_length+=KEYLEN(key);
#endif

		// ++ ctoc
		x->t_ppa = -1;
		x->bypass = false;
		x->write_flying = false;
		// -- ctoc

		for(int i=1; i<=level; i++){
			x->list[i]=update[i]->list[i];
			update[i]->list[i]=x;
		}
		x->level=level;
		list->size++;
	}
	return x;
}

#ifdef Lsmtree
//static int make_value_cnt=0;
value_set **skiplist_make_valueset(skiplist *input, level *from){
//	printf("make_value_cnt:%d\n",++make_value_cnt);
//	if(make_value_cnt==89){
//		printf("break\n");
//	}
	value_set **res=(value_set**)malloc(sizeof(value_set*)*(input->size+1));
	memset(res,0,sizeof(value_set*)*(input->size+1));
	l_bucket b;
	memset(&b,0,sizeof(b));

	snode *target;
	sk_iter* iter=skiplist_get_iterator(input);
	int total_size=0;
	while((target=skiplist_get_next(iter))){
		if(target->value==0) continue;
		b.bucket[target->value->length][b.idx[target->value->length]++]=target;
		total_size+=target->value->length;
	}
	free(iter);
	bool flag=0;
	if(from->idx!=0){
		printf("start fuck!\n");
		flag=1;
	}
	int res_idx=0;
	for(int i=0; i<b.idx[PAGESIZE/PIECE]; i++){//full page
		target=b.bucket[PAGESIZE/PIECE][i];
		res[res_idx]=target->value;
		res[res_idx]->ppa=LSM.lop->moveTo_fr_page(from);//real physical index
		/*checking new ppa in skiplist_valuset*/
#ifdef DVALUE
		//oob[res[res_idx]->ppa/(PAGESIZE/PIECE)]=PBITSET(target->key,true);//OOB setting
		PBITSET(res[res_idx]->ppa,PAGESIZE/PIECE);
#else
		oob[res[res_idx]->ppa]=PBITSET(target->key,true);
#endif
		target->ppa=LSM.lop->get_page(from,(PAGESIZE/PIECE));//64byte chunk index
		target->value=NULL;
		res_idx++;
	}
	b.idx[PAGESIZE/PIECE]=0;
	if(from->idx!=0){
		printf("%d----------end fuck!\n",flag);
	}
	
	for(int i=1; i<PAGESIZE/PIECE+1; i++){
		if(b.idx[i]!=0)
			break;
		if(i==PAGESIZE/PIECE){
			return res;
		}
	}

#ifdef DVALUE
	variable_value2Page(from,&b,&res,&res_idx,false);
	/*
	while(1){
		PTR page=NULL;
		int ptr=0;
		int remain=PAGESIZE-sizeof(footer);
		footer *foot=(footer*)malloc(sizeof(footer));

		res[res_idx]=inf_get_valueset(page,FS_MALLOC_W,PAGESIZE); 
		res[res_idx]->ppa=LSM.lop->moveTo_fr_page(from);
		page=res[res_idx]->value;//assign new dma in page	
		uint8_t used_piece=0;
		while(remain>0){
			int target_length=remain/PIECE;
			while(b.idx[target_length]==0 && target_length!=0) --target_length;
			if(target_length==0){
				break;
			}
			target=b.bucket[target_length][b.idx[target_length]-1];
			target->ppa=LSM.lop->get_page(from,target->value->length);
			PBITSET(target->ppa,target_length);
			foot->map[target->ppa%(PAGESIZE/PIECE)]=target_length;
			used_piece+=target_length;
			memcpy(&page[ptr],target->value->value,target_length*PIECE);
			b.idx[target_length]--;

			ptr+=target_length*PIECE;
			remain-=target_length*PIECE;
		}
		memcpy(&page[PAGESIZE-sizeof(footer)],foot,sizeof(footer));

		res_idx++;

		free(foot);
		bool stop=0;
		for(int i=0; i<PAGESIZE/PIECE+1; i++){
			if(b.idx[i]!=0)
				break;
			if(i==PAGESIZE/PIECE) stop=true;
		}
		if(stop) break;
	}*/
#endif
	res[res_idx]=NULL;
	return res;
}
#endif

snode *skiplist_at(skiplist *list, int idx){
	snode *header=list->header;
	for(int i=0; i<idx; i++){
		header=header->list[1];
	}
	return header;
}

int skiplist_delete(skiplist* list, KEYT key){
	if(list->size==0)
		return -1;
	snode *update[MAX_L+1];
	snode *x=list->header;
	for(int i=list->level; i>=1; i--){
#if defined(KVSSD) && defined(Lsmtree)
		while(KEYCMP(x->list[i]->key,key)<0)
#else
		while(x->list[i]->key<key)
#endif
			x=x->list[i];
		update[i]=x;
	}
	x=x->list[1];

#if defined(KVSSD) && defined(Lsmtree)
	if(KEYCMP(x->key,key)!=0)
#else
	if(x->key!=key)
#endif
		return -2; 

	for(int i=x->level; i>=1; i--){
		update[i]->list[i]=x->list[i];
		if(update[i]==update[i]->list[i])
			list->level--;
	}

//   inf_free_valueset(x->value, FS_MALLOC_W);
	free(x->list);
#ifdef USINGSLAB
	//slab_free(&snode_slab,x);
	kmem_cache_free(snode_slab,x);
#else
	free(x);
#endif
	list->size--;
	return 0;
}

sk_iter* skiplist_get_iterator(skiplist *list){
	sk_iter *res=(sk_iter*)malloc(sizeof(sk_iter));
	res->list=list;
	res->now=list->header;
	return res;
}

snode *skiplist_get_next(sk_iter* iter){
	if(iter->now->list[1]==iter->list->header){ //end
		return NULL;
	}
	else{
		iter->now=iter->now->list[1];
		return iter->now;
	}
}
// for test
void skiplist_dump(skiplist * list){
	sk_iter *iter=skiplist_get_iterator(list);
	snode *now;
	while((now=skiplist_get_next(iter))!=NULL){
		for(uint32_t i=1; i<=now->level; i++){
#if defined(KVSSD) && defined(Lsmtree)
#else
			printf("%u ",now->key);
#endif
		}
		printf("\n");
	}
	free(iter);
}

void skiplist_clear(skiplist *list){
	snode *now=list->header->list[1];
	snode *next=now->list[1];
	while(now!=list->header){
		if(now->value){
			inf_free_valueset(now->value,FS_MALLOC_W);//not only length<PAGESIZE also length==PAGESIZE, just free req from inf
		}

		free(now->list);
		/*
		if(now->req){
			free(now->req->params);
			free(now->req);
		}*/
#ifdef USINGSLAB
	//	slab_free(&snode_slab,now);
		kmem_cache_free(snode_slab,now);
#else
		free(now);
#endif
		now=next;
		next=now->list[1];
	}
	list->size=0;
	list->level=0;
	for(int i=0; i<MAX_L; i++) list->header->list[i]=list->header;
#if defined(KVSSD) && defined(Lsmtree)
	list->header->key=key_max;
#else
	list->header->key=INT_MAX;
#endif
}
skiplist *skiplist_copy(skiplist* src){
	skiplist* des=skiplist_init();
	snode *now=src->header->list[1];
	snode *n_node;
	while(now!=src->header){
		n_node=skiplist_insert(des,now->key,now->value,now->isvalid);
		n_node->ppa=now->ppa;
		now=now->list[1];
	}

	return des;
}
#ifdef Lsmtree
skiplist *skiplist_merge(skiplist* src, skiplist *des){
	snode *now=src->header->list[1];
	while(now!=src->header){
		skiplist_insert_wP(des,now->key,now->ppa,now->isvalid);
		now=now->list[1];
	}
	return des;
}
#endif
void skiplist_free(skiplist *list){
	if(list==NULL) return;
	skiplist_clear(list);
	free(list->header->list);
#ifdef USINGSLAB
	//slab_free(&snode_slab,list->header);
	kmem_cache_free(snode_slab,list->header);
#else
	free(list->header);
#endif
	free(list);
	return;
}
snode *skiplist_pop(skiplist *list){
	if(list->size==0) return NULL;
	KEYT key=list->header->list[1]->key;
	int i;
	snode *update[MAX_L+1];
	snode *x=list->header;
	for(i=list->level; i>=1; i--){
		update[i]=list->header;
	}
	x=x->list[1];
#if defined(KVSSD) && defined(Lsmtree)
	if(KEYCMP(x->key,key)==0)
#else
	if(x->key==key)
#endif
	{
		for(i =1; i<=list->level; i++){
			if(update[i]->list[i]!=x)
				break;
			update[i]->list[i]=x->list[i];
		}

		while(list->level>1 && list->header->list[list->level]==list->header){
			list->level--;
		}
		list->all_length-=KEYLEN(key);
		list->size--;
		return x;
	}
	return NULL;	
}

void skiplist_save(skiplist *input){
	return;
}
skiplist *skiplist_load(){
	skiplist *res=skiplist_init();
	return res;
}

/*
   int main(){
   skiplist * temp=skiplist_init(); //make new skiplist
   char cont[VALUESIZE]={0,}; //value;
   for(int i=0; i<INPUTSIZE; i++){
   memcpy(cont,&i,sizeof(i));
   skiplist_insert(temp,i,cont); //the value is copied
   }

   snode *node;
   int cnt=0;
   while(temp->size != 0){
   sk_iter *iter=skiplist_get_iterator(temp); //read only iterator
   while((node=skiplist_get_next(iter))!=NULL){ 
   if(node->level==temp->level){
   skiplist_delete(temp,node->key); //if iterator's now node is deleted, can't use the iterator! 
//should make new iterator
cnt++;
break;
}
}
free(iter); //must free iterator 
if(cnt==10)
break;
}

for(int i=INPUTSIZE; i<2*INPUTSIZE; i++){
memcpy(cont,&i,sizeof(i));
skiplist_insert(temp,i,cont);
}


skiplist_dump(temp); //dump key and node's level
snode *finded=skiplist_find(temp,100);
printf("find : [%d]\n",finded->key);
skiplist_free(temp);
return 0;
}*/
