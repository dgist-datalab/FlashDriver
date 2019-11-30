#include "iterator.h"
#include "lsmtree.h"
#include "../../include/utils/kvssd.h"
#include "../../include/data_struct/redblack.h"
#include "../../include/settings.h"

Redblack iter_rb;
extern lsmtree LSM;

typedef struct lsm_iter_type{
	bool isfirst;
	int idx;
	KEYT prefix;
	iter **it_list;
}lsm_iter;

uint32_t lsm_iter_create(request *const req){
	static int cnt=0;
	if(!cnt){
		iter_rb=rb_create();
	}
	lsm_iter *lit=(lsm_iter *)malloc(sizeof(lsm_iter));
	lit->idx=cnt++;
	kvssd_cpy_key(&lit->prefix, &req->key);
	lit->it_list=(iter**)malloc(sizeof(iter*)*LSM.LEVELN);
	lit->isfirst=true;

	for(int i=0; i<LSM.LEVELN; i++){
		lit->it_list[i]=iter_init(LSM.disk[i],lit->prefix);
		iter_read_next_data(lit->it_list[i],lit->prefix);
	}

	cnt++;
	rb_insert_int(iter_rb,lit->idx,(void*)lit);
	req->end_req(req);
	return 1;
}

uint32_t lsm_iter_next(request *const req){
	Redblack node;
	rb_find_int(iter_rb, req->spe.iter_idx, &node);
	lsm_iter *it=(lsm_iter*)node->item;
	int idx=0;

	char *data=req->iter_result;
	int res;
	if(it->isfirst){
		for(int i=0;i<LSM.LEVELN; i++){
			iter_read_next_data(it->it_list[i],it->prefix);
		}
	}

	int nodata_cnt;
	bool is_nospace=false;
	while(1){
		//memtable search
		nodata_cnt=0;
		is_nospace=false;
		for(int i=0; i<LSM.LEVELN; i++){
			res=iter_copy_next(it->it_list[i],it->prefix,&data[idx], &idx, PAGESIZE);
			switch(res){
				case IT_R_FLYING:
					fdriver_lock(&it->it_list[i]->mtx);
					fdriver_unlock(&it->it_list[i]->mtx);
					i--;
				case IT_R_SUCCESS:
					continue;
				case IT_R_NODATA:
					nodata_cnt++;
					continue;
				case IT_R_READNEED:
					iter_read_next_data(it->it_list[i], it->prefix);
					break;
				case IT_R_NOSPACE:
					is_nospace=true;
					goto finish;
			}
		}
		if(nodata_cnt==LSM.LEVELN) break;
	}
finish:
	if(is_nospace){
		data[idx]=0xff;
	}
	req->end_req(req);
	return 1;
}

uint32_t lsm_iter_release(request *const req){
	Redblack node;
	rb_find_int(iter_rb,req->spe.iter_idx,&node);
	rb_delete_item(node,0,1);

	lsm_iter *lit=(lsm_iter*)node->item;
	for(int i=0; i<LSM.LEVELN; i++){
		iter_free(lit->it_list[i]);
	}
	free(lit->it_list);
	free(lit);
	req->end_req(req);
	return 1;
}
