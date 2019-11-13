#include "hashftl.h"

algo_req* assign_pseudo_req(TYPE type, value_set *temp_v, request *req){
	algo_req *pseudo_my_req = (algo_req*)malloc(sizeof(algo_req));
	if(pseudo_my_req == NULL){
		printf("algo_req failed\n");
	}
	
	hash_params *params = (hash_params*)malloc(sizeof(hash_params));
	if(params == NULL){
		printf("hash_params faild\n");
	}

	pseudo_my_req->parents = req;
	pseudo_my_req->type = type;
	params->type = type;
	params->value = temp_v;
	/*
	switch(type){
		case DATA_R:
			pseudo_my_req->rapid = true;
			break;
		case DATA_W:
			pseudo_my_req->rapid = true;
			break;
		case GC_R:
			pseudo_my_req->rapid = false;
			break;
		case GC_W:
			pseudo_my_req->rapid = false;
			break;
	}
	*/
	pseudo_my_req->end_req = hash_end_req;
	pseudo_my_req->params = (void*)params;
	return pseudo_my_req;
}

value_set* SRAM_load(SRAM* sram, int32_t ppa, int idx){
	value_set *temp_value_set;
	temp_value_set = inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
	__hashftl.li->read(ppa, PAGESIZE, temp_value_set, 1, assign_pseudo_req(GC_R, NULL, NULL)); // pull in gc is ALWAYS async
	sram[idx].PTR_RAM = (PTR)malloc(PAGESIZE);
	sram[idx].OOB_RAM.lpa = hash_OOB[ppa].lpa;
	return temp_value_set;
}

void SRAM_unload(SRAM* sram, int32_t ppa, int idx){
	value_set *temp_value_set;
	temp_value_set = inf_get_valueset((PTR)sram[idx].PTR_RAM, FS_MALLOC_W, PAGESIZE);
	__hashftl.li->write(ppa, PAGESIZE, temp_value_set, ASYNC, assign_pseudo_req(GC_W, temp_value_set, NULL));
	hash_OOB[ppa] = sram[idx].OOB_RAM;
	free(sram[idx].PTR_RAM);
}

/* extract pba from md5 */
int32_t get_pba_from_md5(uint64_t md5_result, int32_t hid){
	int32_t temp;
	uint64_t shifted;

	//shift result using hid;
	shifted = md5_result >> (hid&0x1ffff);

//	temp = shifted % _g_nob-1;
	temp = shifted % num_b_primary;

	return temp;
}

/* check corresponding can be written for given lpa
if so, return ppa
if not return -1
*/
int32_t check_written(int32_t pba, int32_t lpa, int32_t* cal_ppid)
{
	int32_t add_bit;
	int32_t b_index;
	int32_t ppa = -1;
	int32_t lpn_offset;
	int32_t b_offset;
	int32_t offset_checker;
	
	Block *block = &bm->barray[pba];
	add_bit = num_page_off - num_ppid;
	b_index = block->wr_off;


	// corresponding block is full;
	if(b_index >= _g_ppb){
		return -1;	
	}
	if(add_bit == 0){
		// ppid can represent all page offset
		if(block->hn_ptr == NULL){
			block->hn_ptr = BM_Heap_Insert(primary_b, block);
		}
		ppa = (pba * _g_ppb) + b_index;
		*cal_ppid = b_index;
		block->wr_off++;


	}
	else{
		//ppid cannot represent all page offset

		offset_checker = pow(2,add_bit);
		lpn_offset = lpa % offset_checker;
		b_offset = b_index % offset_checker;

		//see lsb of lpn can represent appropriate page offset
			
		if(lpn_offset == b_offset){
			//if so, return page no and ppid
			
			if(block->hn_ptr == NULL){
				block->hn_ptr = BM_Heap_Insert(primary_b, block);
			}
			
			ppa = (pba * _g_ppb) + b_index;
			*cal_ppid = b_index / offset_checker;
			block->wr_off++;
		}
		
		
		
	}

	return ppa;

}


/* get ppa from table using appropriate logic */
int32_t get_ppa_from_table(int32_t lpa){
	int32_t ppa;
	int32_t pba;
	int32_t idx_secondary;	
	int32_t hid;
	uint32_t lpa_md5;
	uint64_t md5_res;
	size_t len = sizeof(lpa);


	lpa_md5 = lpa;
	// this find logic is for convenience you should change it if you want real performance
	//ppa = pri_table[lpa].ppa;
	int add_bit = num_page_off - num_ppid;
	int g_id = pow(2, add_bit);
	if(pri_table[lpa].hid < hid_secondary){
		md5(&lpa_md5, len, &md5_res);
		hid = pri_table[lpa].hid;
		pba = get_pba_from_md5(md5_res, hid);
		if(add_bit == 0){
			ppa = pba*_g_ppb + pri_table[lpa].ppid;

		}else{
			ppa = pba*_g_ppb + (g_id*pri_table[lpa].ppid) + (lpa % g_id);
				
		}
	}
	else{
		idx_secondary = get_idx_for_secondary(lpa);
		if(lpa != sec_table[idx_secondary].lpa){
			printf("Different LPN !!!\n");
			exit(0);
		}
		ppa = sec_table[idx_secondary].ppa;
	}

	//Not found request
	if(pri_table[lpa].ppa == -1)
		return -1;

	if(ppa != pri_table[lpa].ppa){
		printf("ppa : %d pri_table[lpa].ppa : %d\n",ppa, pri_table[lpa].ppa);
		printf("Different ppa!!\n");
		exit(0);
	}

	return ppa;
}

int32_t set_idx_secondary(){
	static int32_t idx = 0;
	int32_t re_idx = -1;
	for(int i = 0; i < max_secondary; i++){
		if(sec_table[idx].state == CLEAN){
			re_idx = idx;
			break;
		}
		idx = (idx+1) % max_secondary;
	}
	if(re_idx == -1){
		printf("Not found emtry idx in secondary\n");
		exit(0);
	}
	
	return re_idx;
}



int32_t get_idx_for_secondary(int32_t lpa)
{
	int32_t num_segment;
	int32_t max_idx_search;
	int32_t ppid;
	int32_t re_idx = -1;
	int32_t i;
	
	ppid           = pri_table[lpa].ppid;
	num_segment    = max_secondary / pow(2,num_ppid);
	max_idx_search = (num_segment*ppid) + num_segment; 

	for(i = num_segment * ppid; i < max_idx_search; i++){
		if(sec_table[i].lpa == lpa){
			re_idx = i;
			break;
		}
	}
	
	if(re_idx == -1){
		printf("Not exist entry in secondary table\n");
		exit(0);
	}

	return re_idx;

	
}


int32_t alloc_page(int32_t lpa, int32_t* cal_ppid, int32_t* hid){
	int32_t ppa, pba, cnt;
	uint32_t lpa_md5;
	int32_t v_pba = -1;
	Block *block;
	size_t len = sizeof(lpa);
	uint64_t md5_res;
	int32_t t_ppid;
 	lpa_md5 = lpa;
	int32_t add_bit = num_page_off - num_ppid;
	bool flag;

	md5(&lpa_md5, len, &md5_res);
	while(*hid != hid_secondary){
		pba = get_pba_from_md5(md5_res, *hid);
		if((ppa = check_written(pba, lpa, cal_ppid)) != -1){
			return ppa;
		}
		*hid = *hid + 1;
	}
	
	//GC trigger by low watermark
	if(num_secondary >= low_watermark){
		printf("GC trigger by low watermark\n");
		//Check valid for primary blocks 
		for(int i = 0 ; i < num_b_primary; i++){
			block = &bm->barray[i];
			if(block->Invalid == 0){
				flag = 1;
			}else{
				flag = 0;
				break;
			}
		}
		while(num_secondary >= high_watermark){
			//If all valid for primary blocks, only operate remapping
			if(flag == 0)
				v_pba = hash_primary_gc();
			remap_sec_to_pri(v_pba,&t_ppid);
		}
	}

	//Secondary ppa allocation
	ppa = secondary_alloc(0);
	return ppa;
}

int32_t secondary_alloc(bool flag)
{

	static int32_t ppa = -1;
	Block *block = NULL;
	if(ppa != -1 && ppa % _g_ppb == 0){
		ppa = -1;
	}
	if(ppa == -1){
		if(secondary_b->idx == secondary_b->max_size){
			ppa = hash_secondary_gc();
			return ppa++;
		}
		block = BM_Dequeue(free_b);		
		if(block){
			block->hn_ptr = BM_Heap_Insert(secondary_b, block);
			ppa = block->PBA * _g_ppb;
		}else{
			ppa = hash_secondary_gc();
		}

	}
	return ppa++;
	
}

int32_t map_for_gc(int32_t lpa, int32_t ppa){
	int32_t idx_secondary,temp;
	int32_t sec_ppid;
	int32_t b_idx;
	Block *block;
	
	//If valid page is a secondary entry, Invalidate secondary entry
	if(pri_table[lpa].state == H_VALID){
		if(pri_table[lpa].hid == hid_secondary){
			printf("Never enter here in primary gc\n");
			exit(0);
			idx_secondary =	get_idx_for_secondary(lpa);
			sec_table[idx_secondary].state = CLEAN;
			sec_table[idx_secondary].ppa = -1;
			sec_table[idx_secondary].lpa = -1;
			num_secondary--;
		}
	}

	pri_count++;
	//find free entry at secondary table
	idx_secondary = set_idx_secondary();

	
	pri_table[lpa].hid = hid_secondary;
	pri_table[lpa].ppa = ppa;	
	pri_table[lpa].state = H_VALID;
  	//calculate ppid for secondary table
	sec_ppid = pow(2, num_ppid);
	sec_ppid = sec_ppid * idx_secondary;
	sec_ppid = sec_ppid / max_secondary;
	pri_table[lpa].ppid = sec_ppid;


 	//set lpa, ppa, status
  	sec_table[idx_secondary].ppa = ppa;
 	sec_table[idx_secondary].lpa = lpa;
  	sec_table[idx_secondary].state = H_VALID;
 	hash_OOB[ppa].lpa = lpa;
	BM_ValidatePage(bm, ppa);
	num_secondary++;

  	return 0;
}
int32_t map_for_s_gc(int32_t lpa, int32_t ppa){
	int32_t idx_secondary;
	int32_t pba = ppa / _g_ppb;
	Block *block;
	idx_secondary = get_idx_for_secondary(lpa);
	if(idx_secondary == -1){
		printf("Not found idx_secondary in secondary gc\n");
		exit(0);
	}

	//Update ppa and Block valid
	sec_table[idx_secondary].ppa = ppa;
	pri_table[lpa].ppa = ppa;
	hash_OOB[ppa].lpa = lpa;
	BM_ValidatePage(bm,ppa);

	return 0;
	

}

int32_t map_for_remap(int32_t lpa, int32_t ppa, int32_t hid, int32_t *cal_ppid, int32_t secondary_idx){

	Block *block;

	if(lpa != sec_table[secondary_idx].lpa){
		printf("\nmap for remap: lap mapping is wrong\n");
		exit(3);
	}

	if(sec_table[secondary_idx].state != H_VALID){
		printf("\nmap for remap: state is not valid\n");
		exit(3);
	}

	if(pri_table[lpa].hid != hid_secondary){
		printf("\nmap for remap: original hid is not secondary\n");
		exit(3);
	}

	BM_InvalidatePage(bm, sec_table[secondary_idx].ppa);
	sec_table[secondary_idx].state = CLEAN;
	sec_table[secondary_idx].ppa = -1;
	sec_table[secondary_idx].lpa = -1;
	num_secondary--;

	pri_table[lpa].ppa = ppa;
	pri_table[lpa].ppid = *cal_ppid;
	pri_table[lpa].hid = hid;
	pri_table[lpa].state = H_VALID;
	hash_OOB[ppa].lpa = lpa;
	BM_ValidatePage(bm, ppa);


	return 0;
}

int32_t remap_sec_to_pri(int32_t v_pba, int32_t* cal_ppid){
	int32_t lpa, ppa, prev_ppa, hid, pba_t;
	uint32_t lpa_md5;
	int count = 0;
	int invalid_cnt = 0;
	size_t len = sizeof(lpa);
	uint64_t md5_res;
	Block *block;
	value_set *temp_set;
	value_set *temp_value_set;
	for(int i = 0; i < max_secondary; i++){
		if(sec_table[i].state == H_VALID){
			lpa = sec_table[i].lpa;
			lpa_md5 = lpa;
			md5(&lpa_md5, len, &md5_res);
			//md5_res = md5_u(&lpa_md5, len);
			prev_ppa = sec_table[i].ppa;
			hid = 0;
			while(hid != hid_secondary){
				pba_t = get_pba_from_md5(md5_res, hid);
				if((ppa = check_written(pba_t, lpa, cal_ppid)) != -1){
					gc_load = 0;
					temp_set = inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
					__hashftl.li->read(prev_ppa, PAGESIZE, temp_set, 1, assign_pseudo_req(GC_R, NULL, NULL));
					while(gc_load == 0){}	
					
					temp_value_set = inf_get_valueset((PTR)temp_set->value, FS_MALLOC_W, PAGESIZE);	
					inf_free_valueset(temp_set, FS_MALLOC_R);	
					
					map_for_remap(lpa, ppa, hid, cal_ppid, i);
					__hashftl.li->write(ppa, PAGESIZE, temp_value_set,1,assign_pseudo_req(GC_W, temp_value_set, NULL));
					remap_write++;
					
					
					hash_OOB[prev_ppa].lpa = -1;
					temp_value_set = NULL;
					temp_set = NULL;
					count++;
					break;
				}
				hid = hid + 1;
			}
		}

	}
	remap_copy += count;
	//printf("remapped count: %d\n\n", count);
	return 0;
}




/*
int32_t remap_sec_to_pri(int32_t v_pba, int32_t* cal_ppid){
	int32_t lpa, ppa, prev_ppa, hid, pba_t;
	bool flag = 0;
	uint32_t lpa_md5;
	int count = 0;
	int invalid_cnt = 0;
	size_t len = sizeof(lpa);
	uint64_t md5_res;

	int32_t re_sec_idx;
	int32_t re_hid;
	int32_t re_ppid;

	Block *block = &bm->barray[v_pba];

	for(int i = 0; i < _g_ppb; i++){
		if(gc_block[i].state == 0){
			block->wr_off = i;
			for(int j = 0; j < max_secondary; j++){
				if(sec_table[j].state == H_VALID && sec_table[j].gc_flag == 0)
				{
					lpa = sec_table[j].lpa;
					lpa_md5 = lpa;
					md5_res = md5_u(&lpa_md5, len);
					hid = 0;
					while(hid != hid_secondary){
						pba_t = get_pba_from_md5(md5_res, hid);
						if(pba_t == v_pba && pba_t != reserved_pba){
							if((ppa = gc_written(pba_t, lpa, cal_ppid, hid)) != -1){
								gc_block[i].idx_secondary = j;
								sec_table[j].gc_flag = 1;
								flag = 1;
								break;

							}
						}
						hid = hid + 1;
					}
					if(flag){
						flag = 0;
						break;
					}
				}

			}

		}
	}

	for(int i = 0; i < max_secondary; i++){
		if(sec_table[i].gc_flag)
			sec_table[i].gc_flag = 0;
	}	

	ppa = block->PBA * _g_ppb;
	int32_t wr_off;
	for(int i = 0 ; i < _g_ppb; i++){
		if(gc_block[i].state){
			value_set *temp_set = NULL;
			value_set *temp_value_set = NULL;
			gc_load = 0;
			re_hid = gc_block[i].hid;
			re_ppid = gc_block[i].ppid;
			re_sec_idx = gc_block[i].idx_secondary;

			prev_ppa = sec_table[re_sec_idx].ppa;
			temp_set = inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
			__hashftl.li->read(prev_ppa, PAGESIZE, temp_set, 1,assign_pseudo_req(GC_R, NULL, NULL));
			while(gc_load == 0){}
			temp_value_set = inf_get_valueset((PTR)temp_set->value, FS_MALLOC_W, PAGESIZE);
			if(temp_set->value != NULL) free(temp_set->value);
			free(temp_set);

			map_for_remap(ppa+i, re_hid, re_ppid, re_sec_idx);
			__hashftl.li->write(ppa+i, PAGESIZE,temp_value_set,ASYNC,assign_pseudo_req(GC_W, temp_value_set, NULL));

			hash_OOB[prev_ppa].lpa = -1;
			count++;
			wr_off = i;
			sec_table[re_sec_idx].gc_flag = 0;
		}

		gc_block[i].hid = -1;
		gc_block[i].ppid = -1;
		gc_block[i].idx_secondary = -1;
		gc_block[i].state = 0;

	
	}
	block->wr_off = wr_off;
	printf("remapped count: %d\n\n", count);
	return 0;
}
*/
