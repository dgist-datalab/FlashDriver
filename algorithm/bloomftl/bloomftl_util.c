#include "bloomftl.h"
algo_req* assign_pseudo_req(TYPE type, value_set *temp_v, request *req){
	algo_req *res = (algo_req *)malloc(sizeof(algo_req));
	bloom_params *params = (bloom_params *)malloc(sizeof(bloom_params));

	res->parents  = req;
	res->type     = type;
	params->type  = type;
	params->value = temp_v;

			
	switch(type){
		case DATAR:
			res->rapid=true;
			break;
		case DATAW:
			res->rapid=true;
			break;
		case GCDR:
			res->rapid=false;
			break;
		case GCDW:
			res->rapid=false;
			break;
		case RBR:
			res->rapid=false;
			break;
		case RBW:
			res->rapid=false;
			break;
	}
	
//	res->type_lower = 0;
	res->end_req = bloom_end_req;
	res->params  = (void *)params;
	return res;
}	

value_set *SRAM_load(G_manager *g_sram, int64_t ppa , int idx, TYPE type){
	value_set *temp_value;
	temp_value = inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
	__bloomftl.li->read(ppa, PAGESIZE, temp_value, ASYNC, assign_pseudo_req(type, NULL, NULL));
	g_sram[idx].t_table->lba = bloom_oob[ppa].lba;
	g_sram[idx].t_table->ppa = ppa;
	return temp_value;
}

void SRAM_unload(SRAM *sram, int64_t ppa, int idx, TYPE type){
	value_set *temp_value;
	temp_value = inf_get_valueset((PTR)sram[idx].ptr_ram, FS_MALLOC_W, PAGESIZE);
	__bloomftl.li->write(ppa, PAGESIZE, temp_value, ASYNC, assign_pseudo_req(type, temp_value, NULL));
	bloom_oob[ppa].lba = sram[idx].oob_ram;
	return ;
}




int lba_compare(const void *a, const void *b){
	G_manager *c1 = (G_manager *)a;
	G_manager *c2 = (G_manager *)b;

	T_table *num1 = c1->t_table;
	T_table *num2 = c2->t_table;


	if (num1->lba < num2->lba)
		return -1;

	if(num1->lba > num2->lba)
		return 1;

	return 0;
}



#if SUPERBLK_GC
uint32_t ppa_alloc(uint32_t lba){
	uint32_t hashkey;
	uint32_t superblk, offset, ppa;
	uint32_t bf_idx;
	Block *block;
	superblk = lba / mask;
	block = &bm->block[superblk];
	offset = block->p_offset;

	//Reblooming trigger
	/*
	   if(b_table[superblk].bf_num >= r_check){
	   rebloom(superblk);
	   }
	 */

	if(offset == _PPB){
		//GC trigger
		offset = bloom_gc(superblk);
	}	
	if(offset != 0){
		hashkey = hashing_key(lba);
		bf_idx = b_table[superblk].bf_num;
		sb[superblk].bf_page[offset].s_idx = bf_idx;
		set_bf(hashkey + offset, superblk, offset);
		sb[superblk].p_flag[offset] = 1;
		b_table[superblk].bf_num++;
	}

	if(!lba_flag[lba])
		lba_flag[lba] = 1;

	block->valid[offset] = 1;
	block->page[offset].oob = lba;
	ppa = (block->pba_idx * _PPB) + offset;
	block->p_offset++;


	return ppa;


}
#else
uint32_t ppa_alloc(uint32_t lba){
	Block *block;
	uint32_t superblk, cur_idx;
	uint32_t p_idx, s_p_idx;
	uint32_t ppa;
//	uint32_t lba_offset;
	uint32_t f_idx;
	int32_t block_full;
#if REBLOOM
	uint32_t pre_lba, rb_cnt;
#endif
	//superblk = lba / mask;
	superblk = (lba >> 2) % nos;
	block_full = b_table[superblk].full;
//	struct timeval start, end;


	check_cnt++;
	if(check_cnt >= RANGE){
		check_flag = 1;
	}
#if REBLOOM

	//Reblooming trigger
	if(b_table[superblk].bf_num >= r_check){
		b_table[superblk].first = 0;
		rb_flag = 1;
		static int b = 0;
		if(superblk == 4318){
			printf("RB - %d\n",b++);
		}
		rebloom_op(superblk);
		rb_flag = 0;
	}
#endif

	if(block_full == pps){
		b_table[superblk].first = 0;
//		gettimeofday(&start, NULL);
		gc_flag = 1;

		static int a = 0;
		if(superblk == 4318){
			printf("gc - %d\n",a++);
		}
		bloom_gc(superblk);
		gc_flag = 0;
//		gettimeofday(&end, NULL);
//		printf("%lf (sec)\n",(double)(end.tv_usec-start.tv_usec)/1000000);
	}else{
		get_cur_block(superblk);
	}
	//Set real write location

	f_idx   = b_table[superblk].full;
	cur_idx = b_table[superblk].c_block;
	block   = b_table[superblk].b_bucket[cur_idx];
	p_idx   = block->p_offset;
	s_p_idx = ((block->PBA * ppb) + p_idx) % pps;

#if REBLOOM
	pre_lba = b_table[superblk].pre_lba;
	rb_cnt  = b_table[superblk].rb_cnt;
	if(pre_lba+1 != lba || rb_cnt == MAX_RB-1){
		set_bf_table(lba, f_idx, s_p_idx);
		b_table[superblk].rb_cnt = 0;
	}else{
		b_table[superblk].rb_cnt++;
		sb[superblk].p_flag[f_idx] = 0;
		lba_bf[lba] = 0;

	}
	b_table[superblk].pre_lba = lba;
#else
	if(!b_table[superblk].first){
		b_table[superblk].first = 1;
		sb[superblk].p_flag[f_idx] = 1;
		lba_bf[lba] = 0;
	}else{
		set_bf_table(lba, f_idx, s_p_idx);
	}
#endif

	if(!lba_flag[lba])
		lba_flag[lba] = 1;


	ppa = (block->PBA * ppb) + p_idx;
	BM_ValidatePage(bm, ppa);
	//Set valid & oob for block
	bloom_oob[ppa].lba = lba;
	
	block->p_offset++;
	b_table[superblk].full++;
	
	return ppa;

}
#endif



uint32_t set_bf_table(uint32_t lba, uint32_t f_idx, uint32_t s_p_idx){
	//uint32_t superblk = lba / mask;
	uint32_t superblk = (lba >> 2) % nos;
	uint32_t hashkey, bf_idx;
	hashkey = hashing_key(lba);


	bf_idx  = b_table[superblk].bf_num;	
	
	sb[superblk].p_flag[f_idx] = 1;

	sb[superblk].bf_page[s_p_idx].s_idx = bf_idx;
	
	set_bf(hashkey + s_p_idx, superblk, s_p_idx);
	
	b_table[superblk].bf_num++;

	lba_bf[lba] = 1;
	return 1;
}

uint32_t check_first(uint32_t lba){

	Block *block;
	uint32_t superblk;
	uint32_t b_idx, b_offset;
	uint32_t f_offset = 0;
	uint32_t ppa = 0;
	uint32_t oob;
	//superblk = lba / mask;
	superblk = (lba >> 2) % nos;
	
	int32_t full = b_table[superblk].full;
	for(int i = 0; i < full; i++){
		if(sb[superblk].p_flag[i]){
			f_offset = i;
			break;
		}
	}
	b_idx    = f_offset / ppb;
	b_offset = f_offset % ppb;
	
	block = b_table[superblk].b_bucket[b_idx];
	ppa   = (block->PBA * ppb) + b_offset;
	oob   = bloom_oob[ppa].lba;

	if(oob != lba){
		printf("[READ FAIL] Check first !!\n");
		exit(0);
	}

	return ppa;
}

uint32_t table_lookup(uint32_t lba){
	Block *block;	
	int64_t ppa =-1;
	uint32_t superblk, full, oob;
#if REBLOOM
	uint8_t bits_len;
#else
	register uint32_t hashkey;
	uint32_t b_idx, b_offset, s_p_idx;
#endif	
	
	//superblk = lba / mask;
	superblk = (lba >> 2) % nos;
	full = b_table[superblk].full;

#if !REBLOOM
	hashkey = hashing_key(lba);
	for(int i = full-1; i >=0 ; i--){
		if(sb[superblk].p_flag[i]){
			b_idx    = i / ppb;
			b_offset = i % ppb;
			block    = b_table[superblk].b_bucket[b_idx];
			s_p_idx  = ((block->PBA * ppb) + b_offset) % pps;
			ppa      = ((block->PBA * ppb) + b_offset);
			if(get_bf(hashkey+s_p_idx, superblk, s_p_idx)){
				oob = bloom_oob[ppa].lba;
				if(oob == lba){
					found_cnt++;
					break;
				}else{
					not_found_cnt++;
				}
			}
		}
	}
#else
	bits_len = 0;
	for(int i = full-1; i >=0; i--){
		if(sb[superblk].p_flag[i]){
			ppa = bf_lookup(lba, i, bits_len);
			if(ppa != -1){
				break;
			}
			bits_len = 0;
		}
		else{
			bits_len++;
		}
	}

#endif

	if(ppa == -1){
		single_ppa_flag(superblk);
		printf("LBA : %d lba_bf : %d\n",lba,lba_bf[lba]);
		exit(0);
	}
	oob = bloom_oob[ppa].lba;
	if(oob != lba){
		
		single_ppa_flag(superblk);
		printf("[READ FAIL] OOB : %d LBA : %d\n",bloom_oob[ppa].lba, lba);
		exit(0);
	}

	return ppa;	

}
int64_t bf_lookup(uint32_t lba, uint32_t f_idx,  uint8_t bits_len)
{
	Block *block;
	register uint32_t hashkey;
	uint32_t superblk, oob;
	uint32_t b_idx, b_offset;
	uint32_t s_p_idx;
	int32_t f_offset = f_idx;
	int64_t ppa;	
	uint8_t lsb_value;
	
	superblk = (lba >> 2) % nos;
	hashkey  = hashing_key(lba);
	b_idx    = f_offset / ppb;
	b_offset = f_offset % ppb;
	block    = b_table[superblk].b_bucket[b_idx];
	s_p_idx  = ((block->PBA * ppb) + b_offset) % pps;
	ppa      = (block->PBA * ppb) + b_offset;
	/*CASE 1 : If lba is head of coalesced lba */
	if(get_bf(hashkey+s_p_idx, superblk, s_p_idx)){
		oob = bloom_oob[ppa].lba;
		if(oob == lba){
			found_cnt++;
			return ppa;
		}else{
			not_found_cnt++;
		}
	}

	lsb_value = lba % MAX_RB;

	for(int i = 1; i < lsb_value+1; i++){
		hashkey = hashing_key(lba-i);
		if(get_bf(hashkey+s_p_idx, superblk, s_p_idx)){
			oob = bloom_oob[ppa].lba;
			if(oob == lba-i){
				sub_lookup_read++;
				found_cnt++;
				f_offset = f_offset + i;
				if(f_offset >= pps){
					continue;
				}
				b_idx = f_offset / ppb;
				b_offset = f_offset % ppb;
				block = b_table[superblk].b_bucket[b_idx];
				ppa = (block->PBA * ppb) + b_offset;
				oob = bloom_oob[ppa].lba;
				if(oob == lba){
					found_cnt++;
					return ppa;
				}else{
					not_found_cnt++;
				}

			}else{
				not_found_cnt++;
			}
		}
	}
	return -1;

}
#if !SUPERBLK_GC
uint32_t get_cur_block(uint32_t superblk){
	Block *checker;
	uint32_t cur_idx = b_table[superblk].c_block;
	checker = b_table[superblk].b_bucket[cur_idx];
	if(checker->p_offset == ppb){
		cur_idx = (cur_idx+1) % SUPERBLK_SIZE;
		b_table[superblk].c_block = cur_idx;
	}
	
	return 0;

}
#endif
void reset_cur_idx(uint32_t superblk){

	for(int i = 0; i < SUPERBLK_SIZE; i++){
		if(b_table[superblk].b_bucket[i]->p_offset != ppb)
		{
			b_table[superblk].c_block = i;
			return ;
		}
	}

}
void reset_bf_table(uint32_t superblk){
	Block **bucket = b_table[superblk].b_bucket;
	Block *block;
	uint32_t lba;
	uint32_t s_p_idx;
	uint32_t f_offset = 0;
	int64_t ppa = -1;
	uint32_t b_full = b_table[superblk].full;
#if REBLOOM
	uint32_t pre_lba = OOR;
	uint32_t rb_cnt;
#endif
	for(int i = 0 ; i < SUPERBLK_SIZE; i++){
		block = bucket[i];
		for(int j = 0 ; j < block->p_offset; j++){
			ppa = (block->PBA * ppb) + j;
			if(BM_IsValidPage(bm,ppa)){
				lba = bloom_oob[ppa].lba;
				s_p_idx = ((block->PBA * ppb) + j) % pps;
#if REBLOOM
				rb_cnt = b_table[superblk].rb_cnt;
				if(pre_lba+1 != lba || rb_cnt == MAX_RB-1){
					set_bf_table(lba, f_offset, s_p_idx);
					b_table[superblk].rb_cnt = 0;
				}else{
					b_table[superblk].rb_cnt++;
					sb[superblk].p_flag[f_offset] = 0;
					//sb[superblk].lba_bf[lba_offset] = 0;
					lba_bf[lba] = 0;
				}
				pre_lba = lba;
#else
				if(!b_table[superblk].first){
					b_table[superblk].first = 1;
					sb[superblk].p_flag[f_offset] = 1;
					lba_bf[lba] = 0;
				}else{
					set_bf_table(lba, f_offset, s_p_idx);
				}
#endif
			}else{
#if REBLOOM
				pre_lba = OOR;
#endif
				sb[superblk].p_flag[f_offset] = 0;
			}
			f_offset++;
		}
	}
	/*
	if(rb_flag){
		printf("flag set!\n");
		printf("superblk : %d\n",superblk);
		printf("f_offset : %d full : %d\n",f_offset, b_table[superblk].full);
	}else if(gc_flag){
		printf("gc set!\n");
		printf("superblk : %d\n",superblk);
		printf("f_offset : %d full : %d\n",f_offset, b_table[superblk].full);
	}
	*/
	if(f_offset != b_full){
		printf("superblk : %d\n",superblk);
		printf("f_offset : %d full : %d\n",f_offset, b_table[superblk].full);
		printf("[FLAG OFFSET ERROR!]\n");
		exit(0);
	}
	return ;
}

void set_bf(uint32_t hashed_key, uint32_t pbn, uint32_t p_idx){
    uint32_t bf_bits, h;
    int start = sb[pbn].bf_page[p_idx].s_idx;
    int length = bf->base_bf[p_idx].s_bits;
    int end_byte, end_bit, arr_sz, remain_chunk;
    uint8_t chunk_sz;


    end_byte = (start*length + length-1)/8;
    end_bit = (start*length + length-1)%8;
    arr_sz = end_byte - ((start*length)/8) + 1;
    remain_chunk = length;
    chunk_sz = length > end_bit + 1 ? end_bit + 1 : length;


    bf_bits = bf->base_bf[p_idx].bf_bits;
    h = hashfunction(hashed_key) % bf_bits;


    if(end_bit == 7){
        b_table[pbn].bf_arr[end_byte] |= h << (8 - chunk_sz);
    }else{
        b_table[pbn].bf_arr[end_byte] |= h & ((1 << chunk_sz) -1);
    }
    if(arr_sz == 1){
        return ;
    }

    h >>= chunk_sz;
    remain_chunk -= chunk_sz;
    chunk_sz = remain_chunk > 8 ? 8 : remain_chunk;

    b_table[pbn].bf_arr[end_byte-1] |= h << (8 - chunk_sz);
    if(arr_sz == 2){
        return ;
    }
    h >>= chunk_sz;
    remain_chunk -= chunk_sz;
    chunk_sz = remain_chunk > 8 ? 8 : remain_chunk;
    b_table[pbn].bf_arr[end_byte-2] |= h << (8 - chunk_sz);

    return ;
}


