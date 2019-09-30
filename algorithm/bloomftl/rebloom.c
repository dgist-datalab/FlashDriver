#include "bloomftl.h"


#if REBLOOM
void rebloom_op(uint32_t superblk){
	
	Block **bucket = b_table[superblk].b_bucket;
	Block *block, *m_block;
	SRAM *d_ram;

	uint32_t p_idx;
	uint32_t cur_idx;
	uint32_t g_range, rb_valid_cnt;
	uint32_t valid_cnt;
	
	uint32_t free_space;
	uint32_t erase_idx = 0;
	int64_t ppa;

	page_load = page_unload = 0;

	/* init a global data structure for reblooming */
	//When reblooming is trigger, use a bucket for coalesced pages 
	valid_p = (G_manager *)malloc(sizeof(G_manager) * pps);
	rb = (G_manager *)malloc(sizeof(G_manager) * pps);	
	re_list = (G_manager *)malloc(sizeof(G_manager) * SUPERBLK_SIZE);

	for(int i = 0 ; i < pps ; i++){
		valid_p[i].t_table = NULL;
		valid_p[i].value = NULL;
		rb[i].t_table = (T_table *)malloc(sizeof(T_table) * MAX_RB);
		rb[i].value   = (value_set *)malloc(sizeof(value_set) * MAX_RB);
		rb[i].size = 0;
	}
	//When reblooming is trigger, use a bucket for remain pages 
	for(int i = 0; i < SUPERBLK_SIZE; i++){
		re_list[i].t_table = (T_table *)malloc(sizeof(T_table) * ppb);
		re_list[i].value   = (value_set *)malloc(sizeof(value_set) * ppb);
		re_list[i].size = 0;
	}

	



	//Invalid temporal table for blocks	
	cur_idx  = b_table[superblk].c_block;
	valid_cnt = invalid_block(bucket, cur_idx);

	d_ram = (SRAM *)malloc(sizeof(SRAM) * valid_cnt);


	//Make reblooming list
	
	g_range = set_rebloom_list(valid_cnt);		
	rb_valid_cnt = check_rb_valid(d_ram, g_range);	
	
	free_space = pps - b_table[superblk].full;
	
	//If free space is not enough, GC trigger
	while(free_space < rb_valid_cnt){
		rb_valid_cnt += rebloom_gc(d_ram, &free_space, &erase_idx, superblk, rb_valid_cnt);
	}

	
	//Shift bucket pointers for blocks
	if(erase_idx != SUPERBLK_SIZE){
		while(erase_idx--){
			m_block = bucket[0];
			for(int i = 0 ; i < SUPERBLK_SIZE-1; i++){
				bucket[i] = bucket[i+1];
			}
			bucket[SUPERBLK_SIZE-1] = m_block;
		}

	}
	
	//Reset current block pointer
	reset_cur_idx(superblk);
	for(int i = 0; i < rb_valid_cnt; i++){
		get_cur_block(superblk);
		cur_idx = b_table[superblk].c_block;
		block = b_table[superblk].b_bucket[cur_idx];
		p_idx = block->p_offset;
		ppa = (block->PBA * ppb) + p_idx;
		SRAM_unload(d_ram, ppa, i, RBW);
		BM_ValidatePage(bm, ppa);	
		block->p_offset++;
		
		b_table[superblk].full++;
	}

	//while(page_unload != 0){}


	b_table[superblk].pre_lba = d_ram[rb_valid_cnt-1].oob_ram;

	//Reset bloomfilter
	b_table[superblk].bf_num = 0;
	b_table[superblk].rb_cnt = 0;
	memset(b_table[superblk].bf_arr, 0, sizeof(uint8_t) * bf->base_s_bytes);
	memset(sb[superblk].bf_page, -1, sizeof(Index) * pps);
	memset(sb[superblk].p_flag, 0, sizeof(bool) * pps);
	
	reset_bf_table(superblk);	
	

	for(int i = 0 ; i < pps ; i++){
		free(gm[i].t_table);
		if(gm[i].value != NULL)
			inf_free_valueset(gm[i].value, FS_MALLOC_R);
		else{
			free(gm[i].value);
		}
	}


	free(gm);
	free(rb);
	free(re_list);
	free(d_ram);
	gm = NULL;
	rb = NULL;
	re_list = NULL;
	valid_p = NULL;
	//printf("After BF count : %d\n",b_table[superblk].bf_num);

	return ;
}

uint32_t rebloom_gc(SRAM *sram, uint32_t *free_space, uint32_t *erase_idx, uint32_t superblk, uint32_t full){
    Block **bucket = b_table[superblk].b_bucket;
    Block *victim;
    uint32_t idx = full;
   // uint32_t max_invalid;
	//uint32_t max_idx, check_idx;
    uint32_t add_cnt;
    uint32_t remove_idx = *erase_idx;

	victim = bucket[remove_idx];
	b_table[superblk].full -= victim->p_offset;
    *free_space += victim->p_offset;

	
    BM_InitializeBlock(bm, victim->PBA);
	__bloomftl.li->trim_block(victim->PBA * ppb, false);	
	block_erase_cnt++;

    add_cnt = re_list[remove_idx].size;
    //Add valid pages into write list
    if(add_cnt != 0){
        for(int i = 0 ; i < add_cnt; i++){
			sram[idx].ptr_ram = (PTR)malloc(PAGESIZE);
			memcpy(sram[idx].ptr_ram, &re_list[remove_idx].value->value, PAGESIZE);
			sram[idx].oob_ram = re_list[remove_idx].t_table[i].lba;
			idx++;
			
			free(&re_list[remove_idx].t_table[i]);
			inf_free_valueset(&re_list[remove_idx].value[i], FS_MALLOC_R);

		}
    }
	(*erase_idx)++;
    return add_cnt;
}

uint32_t set_rebloom_list(uint32_t arr_size){
	uint32_t pre_lba, next_lba;
	uint32_t g_idx, rb_idx;
	uint32_t max_group;
	g_idx = 0;
	
	memcpy(&rb[g_idx].t_table[0], valid_p[0].t_table, sizeof(T_table));
	memcpy(&rb[g_idx].value[0], valid_p[0].value, sizeof(value_set));
	
//	inf_free_valueset(valid_p[0].value, FS_MALLOC_R);
//	free(valid_p[0].t_table);

	rb[g_idx].size++;
	pre_lba = valid_p[0].t_table->lba;

	for(int i = 1; i < arr_size; i++){
		next_lba = valid_p[i].t_table->lba;
		if(pre_lba+1 != next_lba || rb[g_idx].size == MAX_RB){
			g_idx++;
		}
		rb_idx = rb[g_idx].size;
		memcpy(&rb[g_idx].t_table[rb_idx], valid_p[i].t_table, sizeof(T_table));
		memcpy(&rb[g_idx].value[rb_idx], valid_p[i].value, sizeof(value_set));

//		free(valid_p[i].t_table);
//		inf_free_valueset(valid_p[i].value, FS_MALLOC_R);

		rb[g_idx].size++;
		pre_lba = next_lba;
	}

	max_group = g_idx + 1;

	//free copy page memory 
	
	
	
	return max_group;
	

	
}

uint32_t check_rb_valid(SRAM *sram, uint32_t g_range){
	Block *block;
	uint32_t idx = 0;
	uint32_t b_idx, b_offset;
	uint32_t pbn,p_idx;
	int64_t ppa;

	for(int i = 0 ; i < g_range; i++){
		for(int j = 0 ; j < rb[i].size; j++){
			if(rb[i].size > 1){
				ppa   = rb[i].t_table[j].ppa;
				pbn   = ppa / ppb;
				block = &bm->barray[pbn];
				BM_InvalidatePage(bm, ppa);
				sram[idx].ptr_ram = (PTR)malloc(PAGESIZE);
				memcpy(sram[idx].ptr_ram, &rb[i].value[j].value, PAGESIZE);
				sram[idx].oob_ram = rb[i].t_table[j].lba;
				idx++;
			}

			if(rb[i].size == 1){
				b_idx = rb[i].t_table[j].b_idx;
				b_offset = re_list[b_idx].size;
				re_list[b_idx].t_table[b_offset] = rb[i].t_table[j];
				re_list[b_idx].value[b_offset]   = rb[i].value[j];
				re_list[b_idx].size++;

			}

			free(&rb[i].t_table[j]);
			inf_free_valueset(&rb[i].value[j], FS_MALLOC_R);


		}
	}
	
	return idx;
}

#endif
