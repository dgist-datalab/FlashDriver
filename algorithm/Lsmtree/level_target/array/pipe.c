#include "pipe.h"
#include "array.h"
#include "../../lsmtree.h"
#include <stdio.h>
#include <stdlib.h>
#include "../../../../bench/bench.h"
extern MeasureTime write_opt_time[10];
extern lsmtree LSM;
p_body *pbody_init(char **data,uint32_t size, pl_run *pl_datas, bool read_from_run,BF *filter){
	p_body *res=(p_body*)calloc(sizeof(p_body),1);
	res->data_ptr=data;
	res->max_page=size;
	res->read_from_run=read_from_run;
	res->pl_datas=pl_datas;
#ifdef BLOOM
	if(filter){
		res->filter=filter;
	}
#endif
	res->prev_pent.type=INVALIDENT;
	return res;
}

bool print_test;
int lock_cnt=0;
void new_page_set(p_body *p, bool iswrite){
	if(p->read_from_run){
		if(fdriver_try_lock(p->pl_datas[p->pidx].lock)==-1){
			fdriver_lock(p->pl_datas[p->pidx].lock);
		}
		else{
		}
		p->now_page=data_from_run(p->pl_datas[p->pidx].r);
	}
	else{
		if(p->pidx>=p->max_page){
			printf("%d %d \n", p->pidx, p->max_page);
		}
		p->now_page=p->data_ptr[p->pidx];

	}
	if(iswrite && !p->now_page){
		p->now_page=(char*)malloc(PAGESIZE);
	}
	p->bitmap_ptr=(uint16_t *)p->now_page;
	p->kidx=1;
	p->max_key=p->bitmap_ptr[0];
	p->length=1024;
	p->pidx++;
}

extern bool amf_debug_flag;
p_entry pbody_get_next_pentry(p_body *p){
	if((!p->now_page && p->pidx<p->max_page) || (p->pidx<p->max_page && p->kidx>p->max_key)){
		new_page_set(p,false);
	}

	if(!p->now_page && p->pidx >= p->max_page && p->kidx==0){
		p->kidx=1;
	}

	p_entry res={0,};
	if(p->pidx>=p->max_page && p->kidx>p->max_key){
		res.key.len=-1;
		return res;
	}

	char* data=&p->now_page[p->bitmap_ptr[p->kidx]];
	uint32_t data_idx=1;
	res.type=data[0];
	switch(res.type){
		case KVSEP:
			memcpy(&res.info.ppa,&data[data_idx],sizeof(uint32_t)); 
			data_idx+=sizeof(uint32_t);
			res.key.len=p->bitmap_ptr[p->kidx+1]-p->bitmap_ptr[p->kidx]-sizeof(uint32_t)-1;
			res.key.key=&data[data_idx];
			break;
		case KVUNSEP:
			memcpy(&res.info.v_len,&data[data_idx],sizeof(uint32_t));
			data_idx+=sizeof(uint32_t);
			res.data=&data[data_idx];
			data_idx+=res.info.v_len;

			res.key.len=p->bitmap_ptr[p->kidx+1]-p->bitmap_ptr[p->kidx]-sizeof(uint32_t)-1-res.info.v_len;
			res.key.key=&data[data_idx];
			break;
		default:
			printf("unknown type! %s:%d\n", __FILE__, __LINE__);
			abort();
			break;
	}

	p->kidx++;
	return res;
}

bool test_flag;
char *pbody_insert_new_pentry(p_body *p, p_entry pent, bool flush)
{
	if(!flush){
		if(p->prev_pent.type==INVALIDENT){
			p->prev_pent=pent;
		}
		else{
			static int cnt=0;
			if(KEYCMP(p->prev_pent.key, pent.key) >=0){
				printf("order is failed! %d %.*s~%.*s\n", cnt++, KEYFORMAT(p->prev_pent.key), KEYFORMAT(pent.key));
				abort();
			}
			p->prev_pent=pent;
		}
	}

	char *res=NULL;
	if((flush && p->kidx>1) || !p->now_page || p->kidx>=(PAGESIZE-1024)/sizeof(uint16_t)-2 || 
			(p->length+(pent.key.len+sizeof(uint32_t)+pent.type==KVSEP?0:pent.info.v_len))>PAGESIZE){
		if(p->now_page){
			p->bitmap_ptr[0]=p->kidx-1;
			p->bitmap_ptr[p->kidx]=p->length;
			p->data_ptr[p->pidx-1]=p->now_page;
			res=p->now_page;
		}
		if(flush){
			return res;
		}
		new_page_set(p,true);
	}

	char *target=&p->now_page[p->length];
	uint32_t added_length=0;
	switch(pent.type){
		case KVSEP:
			target[added_length++]=KVSEP;
			memcpy(&target[added_length],&pent.info.ppa,sizeof(uint32_t));
			added_length+=sizeof(uint32_t);
			memcpy(&target[added_length],pent.key.key,pent.key.len);
			added_length+=pent.key.len;
			break;
		case KVUNSEP:
			target[added_length++]=KVUNSEP;
			memcpy(&target[added_length],&pent.info.v_len,sizeof(uint32_t));
			added_length+=sizeof(uint32_t);
			memcpy(&target[added_length],pent.data, pent.info.v_len);
			added_length=pent.info.v_len;
			memcpy(&target[added_length],pent.key.key,pent.key.len);
			added_length+=pent.key.len;
			break;
		default:	
			printf("unknown type! %s:%d\n", __FILE__, __LINE__);
			abort();
	}

	p->bitmap_ptr[p->kidx++]=p->length;
	p->length+=added_length;

#ifdef BLOOM
	if(p->filter)
		bf_set(p->filter,pent.key);
#endif
	return res;
}


char *pbody_get_data(p_body *p, bool init)
{
	if(init){
		p->max_page=p->pidx;
		p->pidx=0;
	}

	if(p->pidx<p->max_page){
		return p->data_ptr[p->pidx++];
	}
	else{
		return NULL;
	}
}


p_body *pbody_move_dummy_init(char **data, uint32_t data_num){
	p_body *res=(p_body*)calloc(sizeof(p_body),1);
	res->pidx=data_num;
	res->data_ptr=data;
	return res;
}

char *pbody_clear(p_body *p){
	free(p);
	return NULL;
}
