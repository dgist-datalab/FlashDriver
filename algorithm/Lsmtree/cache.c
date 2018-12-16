#include "lsmtree.h"
#include "../../include/lsm_settings.h"
#include "cache.h"
#include<stdlib.h>
#include<string.h>
#include<stdio.h>
cache *cache_init(uint32_t noe){
	cache *c=(cache*)malloc(sizeof(cache));
	c->m_size=noe;
	c->n_size=0;
	c->top=NULL;
	c->bottom=NULL;
	pthread_mutex_init(&c->cache_lock,NULL);
	return c;
}

cache_entry * cache_insert(cache *c, run_t *ent, int dmatag){
	if(c->m_size==c->n_size){
		cache_delete(c,cache_get(c));
	}
	cache_entry *c_ent=(cache_entry*)malloc(sizeof(cache_entry));

	c_ent->entry=ent;
	if(c->bottom==NULL){
		c->bottom=c_ent;
		c->top=c_ent;
		c->bottom->down=NULL;
		c->top->up=NULL;
		c->n_size++;
		return c_ent;
	}

	c->top->up=c_ent;
	c_ent->down=c->top;

	c->top=c_ent;
	c_ent->up=NULL;
	c->n_size++;
	return c_ent;
}
int delete_cnt_check;
bool cache_delete(cache *c, run_t * ent){
	if(c->n_size==0){
		return false;
	}
	cache_entry *c_ent=ent->c_entry;
	if(ent->header){
		free(ent->header->sets);
		free(ent->header);
	}
	ent->header=NULL;
	c->n_size--;
	free(c_ent);
	ent->c_entry=NULL;
	return true;
}

bool cache_delete_entry_only(cache *c, run_t *ent){
	if(c->n_size==0){
		return false;
	}
	cache_entry *c_ent=ent->c_entry;
	if(c_ent==NULL) {
		return false;
	}
	if(c->bottom==c->top && c->top==c_ent){
		c->top=c->bottom=NULL;
	}
	else if(c->top==c_ent){
		cache_entry *down=c_ent->down;
		down->up=NULL;
		c->top=down;
	}
	else if(c->bottom==c_ent){
		cache_entry *up=c_ent->up;	
		up->down=NULL;
		c->bottom=up;
	}
	else{
		cache_entry *up=c_ent->up;
		cache_entry *down=c_ent->down;
		
		up->down=down;
		down->up=up;
	}
	c->n_size--;
	free(c_ent);
	ent->c_entry=NULL;
	return true;
}
void cache_update(cache *c, run_t* ent){
	cache_entry *c_ent=ent->c_entry;
	if(c->top==c_ent){ 
		return;
	}
	if(c->bottom==c_ent){
		cache_entry *up=c_ent->up;
		up->down=NULL;
		c->bottom=up;
	}
	else{
		cache_entry *up=c_ent->up;
		cache_entry *down=c_ent->down;
		up->down=down;
		down->up=up;
	}

	c->top->up=c_ent;
	c_ent->up=NULL;
	c_ent->down=c->top;
	c->top=c_ent;
}

run_t* cache_get(cache *c){
	if(c->n_size==0){
		return NULL;
	}
	cache_entry *res=c->bottom;
	cache_entry *up=res->up;

	if(up==NULL){
		c->bottom=c->top=NULL;
	}
	else{
		up->down=NULL;
		c->bottom=up;
	}
	if(!res->entry->c_entry){
		printf("hello\n");
	}
	return res->entry;
}
void cache_free(cache *c){
	run_t *tmp_ent;
	while((tmp_ent=cache_get(c))){
		free(tmp_ent->c_entry);
		tmp_ent->c_entry=NULL;
		c->n_size--;
	}
	free(c);
}
int print_number;
void cache_print(cache *c){
	cache_entry *start=c->top;
	print_number=0;
	run_t *tent;
	while(start!=NULL){
		tent=start->entry;
		if(start->entry->c_entry!=start){
			printf("fuck!!!\n");
		}
		printf("[%d]c->entry->key:%d c->entry->pbn:%d d:%p\n",print_number++,tent->key,tent->pbn,tent->header);
		start=start->down;
	}
}

