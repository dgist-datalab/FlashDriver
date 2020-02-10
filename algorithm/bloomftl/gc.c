#include "bloomftl.h"


uint32_t bloom_gc(uint32_t superblk){

	Block **bucket = b_table[superblk].b_bucket;
	Block *victim;
	SRAM *d_ram;

	value_set **temp_set;

	int32_t old_block;
	int64_t ppa;
	int32_t max_invalid, max_idx;

	volatile int k = 0;
	data_gc_poll = 0;
	d_ram = (SRAM *)malloc(sizeof(SRAM) * pps);
	temp_set = (value_set **)malloc(sizeof(value_set *) * pps);
	for(int i = 0 ; i < pps ; i++){
		d_ram[i].oob_ram = -1;
		d_ram[i].ptr_ram = NULL;
	}



	//Select Greedy block
	max_idx = 0;
	max_invalid = bucket[max_idx]->Invalid;


	for(int i = 1; i < SUPERBLK_SIZE; i++){
		if(max_invalid < bucket[i]->Invalid){
			max_invalid = bucket[i]->Invalid;
			max_idx = i;
		}
	}
	b_table[superblk].full -= max_invalid;
	//Shift block bucket	
	victim = bucket[max_idx];

	if(max_idx != SUPERBLK_SIZE-1){
		for(int i = max_idx; i < SUPERBLK_SIZE-1; i++){
			bucket[i] = bucket[i+1];
		}
		bucket[SUPERBLK_SIZE-1] = victim;
	}

	for(int i = 0 ; i < victim->p_offset; i++){
		ppa = (victim->PBA * ppb) + i;
		if(BM_IsValidPage(bm, ppa)){
			temp_set[k] = SRAM_load(ppa, k, GCDR);
			d_ram[k].oob_ram = bloom_oob[ppa].lba;
			d_ram[k].ptr_ram = temp_set[k]->value;

			bloom_oob[ppa].lba = -1;
			k++;
		}
	}

	while(data_gc_poll != k){};

	// Init block and bloomfilter
	old_block = victim->PBA * ppb;
	__bloomftl.li->trim_block(old_block, false);	
	BM_InitializeBlock(bm, victim->PBA);

	block_erase_cnt++;

	memset(b_table[superblk].bf_arr, 0, sizeof(uint8_t) * bf->base_s_bytes);
	memset(sb[superblk].bf_page, -1, sizeof(Index) * pps);
	memset(sb[superblk].p_flag, 0, sizeof(bool) * pps);	
#if REBLOOM
	b_table[superblk].rb_cnt = 0;
	//qsort(d_ram, k, sizeof(SRAM), gc_compare);
#endif

	b_table[superblk].bf_num = 0;

	for(int i = 0; i < k; i++){
		ppa = (victim->PBA * ppb) + victim->p_offset;
		SRAM_unload(d_ram, ppa, i, GCDW);
		BM_ValidatePage(bm,ppa);
		victim->p_offset++;
		free(d_ram[i].ptr_ram);
	}

	for(int i = 0 ; i < k ; i++){
		free(temp_set[i]);
	}
	reset_bf_table(superblk);
#if REBLOOM
	if(k != 0){
		b_table[superblk].pre_lba = d_ram[k-1].oob_ram;
	}
#endif
	b_table[superblk].c_block = SUPERBLK_SIZE-1;
	free(d_ram);


	return 0;
}
int invalid_block(Block **bucket, int b_idx, uint32_t superblk){

	G_manager check_g, invalid_g;
	Block *checker;
	uint32_t ppa;
	volatile int k = 0;
	int valid_page_num = 0;
	int weight;
	uint32_t idx;
	int invalid_cnt = 0;
	int start_idx;
	TYPE type;
	idx = 0, weight = 0;	
	data_gc_poll = 0;

	if(b_idx == -1){
		//When GC trigger
		start_idx = SUPERBLK_SIZE-1;
	}else{
		//When RB trigger
		start_idx = b_idx;
	}

	for(int i = start_idx; i>=0 ; i--){
		checker = bucket[i];
		for(int j = checker->p_offset-1; j>=0; j--){
			ppa = (checker->PBA * ppb) + j;
			if(BM_IsValidPage(bm, ppa)){
				if(b_idx != -1){
					type = RBR;
				}else{
					type = GCDR;
				}

				gm[k].t_table->lba = bloom_oob[ppa].lba;
				gm[k].t_table->ppa = ppa;
				gm[k].t_table->weight = weight++;
				gm[k].t_table->b_idx = i;
				gm[k].value = SRAM_load(ppa, k, type);

				k++;
				valid_page_num++;
			}
		}
	}


	while(data_gc_poll != k){} //polling for all page load	
	qsort(gm, k, sizeof(G_manager), lba_compare);
	check_g = gm[0];
	for(int i = 1; i < k; i++){
		if(check_g.t_table->lba != gm[i].t_table->lba){
			valid_p[idx++] = check_g;
			check_g = gm[i];
			if(i==k-1){
				valid_p[idx++] = gm[i];
			}
		}else{
			invalid_g = gm[i];
			if(check_g.t_table->weight > gm[i].t_table->weight){
				invalid_g = check_g;
				check_g = gm[i];
			}
			if(i==k-1){
				valid_p[idx++] = check_g;
			}
			ppa = invalid_g.t_table->ppa;
			BM_InvalidatePage(bm, ppa);
			invalid_cnt++;
		}
	}


	return idx;
}
