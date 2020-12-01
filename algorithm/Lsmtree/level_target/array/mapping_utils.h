#ifndef __MAP_UTILS_H__
#define __MAP_UTILS_H__

#include <stdlib.h>
#include <stdio.h>
#include "array.h"
#include "pipe.h"
#include "../../../../interface/koo_hg_inf.h"
/*
static inline uint32_t __extract_ppa(KEYT key){
	return *(uint32_t*)(key.key-sizeof(ppa_t));
}
*/
static inline p_entry  __extract_p_entry(uint16_t idx, char *data, uint16_t* bitmap){
	if(bitmap[0] < idx){
		printf("access not populated area! %s:%u\n", __FILE__, __LINE__);
		abort();
	}
	char *body=&data[bitmap[idx]];
	uint32_t body_ptr=0;
	p_entry res;
	res.type=body[body_ptr++];
	switch(res.type){
		case KVSEP:
			res.info.ppa=*(uint32_t*)&body[body_ptr];
			res.key.len=bitmap[idx+1]-bitmap[idx]-sizeof(ppa_t)-1;
			res.key.key=&data[bitmap[idx]+sizeof(ppa_t)+1];
			break;
		case KVUNSEP:
			res.info.v_len=*(uint32_t*)&body[body_ptr];
			body_ptr+=sizeof(uint32_t);
			res.data=&body[body_ptr];
			body_ptr+=res.info.v_len;
			res.key.len=bitmap[idx+1]-bitmap[idx]-sizeof(uint32_t)-1-res.info.v_len;
			res.key.key=&body[body_ptr];
			break;
	}
	return res;
}

static inline KEYT __key_at(uint16_t idx, char *data, uint16_t *bitmap){
	if(bitmap[0] < idx){
		printf("access not populated area! %s:%u\n", __FILE__, __LINE__);
		abort();
	}
	char *body=&data[bitmap[idx]];
	uint32_t body_ptr=0;
	p_entry res;
	res.type=body[body_ptr++];
	switch(res.type){
		case KVSEP:
			res.key.len=bitmap[idx+1]-bitmap[idx]-sizeof(ppa_t)-1;
			res.key.key=&data[bitmap[idx]+sizeof(ppa_t)];
			break;
		case KVUNSEP:
			res.info.v_len=*(uint32_t*)&body[body_ptr];
			body_ptr+=res.info.v_len+sizeof(uint32_t);
			res.key.len=bitmap[idx+1]-bitmap[idx]-sizeof(uint32_t)-1-res.info.v_len;
			res.key.key=&body[body_ptr];
			break;
	}

	return res.key;
}

static inline KEYT __extract_start_key(char *data){
	return __key_at(1, data, (uint16_t*)data);
}

static inline KEYT __extract_end_key(char *data){
	return __key_at(*((uint16_t*)data), data, (uint16_t*)data);
}

static inline uint16_t __get_KP_pair_num(char *data){
	return *(uint16_t*)data;
}

static inline int __find_idx_boundary(char *data, KEYT lpa, KEYT lpa2){
	char *body=data;
	uint16_t *bitmap=(uint16_t*)body;
	int s=1, e=bitmap[0];
	int mid=0,res=0;
	KEYT target;
	while(s<=e){
		mid=(s+e)/2;
		target=__key_at(mid, data, bitmap);
		res=KEYCMP(target,lpa);
		if(res==0){
			return mid;
		}
		else if(res<0){
			s=mid+1;
		}
		else{
			e=mid-1;
		}
	}
	
	if(res<0) return mid;

	if(KEYCMP(__key_at(mid, body, bitmap), lpa2) >= 0){
		return mid-1;
	}
	return mid;
}

static inline char *__split_data(char *data, KEYT key, KEYT key2, bool debug){
	KEYT tt=__extract_end_key(data);
	if(KEYCMP(__extract_end_key(data), key2) < 0){
		return NULL;
	}
	uint16_t boundary=__find_idx_boundary(data, key, key2);
	if(boundary==0){
		return NULL;
	}

//	char buf1[100], buf2[100];
//	key_interpreter(key, buf1);
//	key_interpreter(key2, buf2);
//
//	printf("split called!!! k1:%s k2:%s\n", buf1, buf2);
	char *res=(char *)calloc(PAGESIZE,1);

	char *ptr=res;
	uint16_t *bitmap=(uint16_t*)res;
	memset(bitmap, -1, KEYBITMAP/sizeof(uint16_t));
	uint16_t data_start=KEYBITMAP;
	uint16_t idx=0;

	uint16_t *org_bitmap=(uint16_t*)data;
	p_entry pent;
	uint32_t added_length;
	char *target;
	for(uint16_t i=boundary+1; i<=org_bitmap[0]; i++){
		pent=__extract_p_entry(i, data, org_bitmap);
		added_length=0;
		target=&ptr[data_start];
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
				added_length+=pent.info.v_len;
				memcpy(&target[added_length],pent.key.key,pent.key.len);
				added_length+=pent.key.len;
				break;
			default:
				printf("unknown type! %s:%d\n", __FILE__, __LINE__);
				abort();
				break;
		}

		bitmap[idx+1]=data_start;
		data_start+=added_length;
		idx++;
	}
	bitmap[idx+1]=data_start;
	bitmap[0]=idx;

	p_entry boundary_pentry=__extract_p_entry(boundary, data, org_bitmap);
	org_bitmap[0]=boundary;
	uint32_t boundary_length=0;
	switch(boundary_pentry.type){
		case KVSEP:
			boundary_length+=1+sizeof(ppa_t)+boundary_pentry.key.len;
			break;
		case KVUNSEP:
			boundary_length+=1+sizeof(uint32_t)+boundary_pentry.info.v_len+boundary_pentry.key.len;
			break;
		default:
			printf("unknown type! %s:%d\n", __FILE__, __LINE__);
			abort();
			break;
	}
	org_bitmap[boundary+1]=org_bitmap[boundary]+boundary_length;

	/*
	printf("split front!\n");
	array_header_print(res);
	printf("split back!\n");
	array_header_print(data);*/
	return res;
}

static inline int __find_boundary_in_data_list(KEYT lpa, char **data, int data_num){
	int32_t s=0, e=data_num-1;
	int mid=-1, res;
	while(s<=e){
		mid=(s+e)/2;
		res=KEYCMP(__extract_start_key(data[mid]), lpa);
		if(res>0) e=mid-1;
		else if(res<0) s=mid+1;
		else{
			return mid;
		}
	}
	if(KEYCMP(__extract_start_key(data[mid]), lpa) > 0)
		return mid-1;
	else 
		return mid;
}

static inline int __header_overlap_chk(char *upper_data, char *lower_data){
	KEYT upper_start=__extract_start_key(upper_data);
	KEYT upper_end=__extract_end_key(upper_data);
	KEYT lower_start=__extract_start_key(lower_data);
	KEYT lower_end=__extract_end_key(lower_data);
	if(KEYCMP(upper_start, lower_end)>0) return 1;
	if(KEYCMP(upper_end, lower_start)<0) return -1;
	return 0;
}
#endif
