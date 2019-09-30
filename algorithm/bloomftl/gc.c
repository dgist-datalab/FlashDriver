#include "bloomftl.h"

//Superblock unit GC
#if SUPERBLK_GC
uint32_t bloom_gc(uint32_t superblk){


	Block *victim;
	Page *valid_p, *sort_p;
	victim = &bm->block[superblk];
	int idx, k = 0;
	uint32_t hashkey, lba, bf_idx;

	valid_p = (Page *)malloc(sizeof(Page) * mask);
	//Invalid date pages. This is read operations
	for(int i = victim->p_offset-1; i>=0; i--){
		if(victim->valid[i]){
			gc_read++;
			lba = victim->page[i].oob;
			idx = lba % mask;
			if(!gm[idx].update){
				valid_p[k++].oob = lba;
				gm[idx].update = 1;
			}else{
				victim->valid[i] = 0;
				victim->invalid_cnt++;
			}
		}
	}

	memset(gm,0, sizeof(GCmanager) * mask);

	
	sort_p = (Page *)malloc(sizeof(Page) * k);
	for(int i = 0; i < k; i++){
		sort_p[i].oob = valid_p[i].oob;
		lba_flag[sort_p[i].oob] = 0;
	}

	free(valid_p);

	qsort(sort_p, k, sizeof(Page), compare);
	
	//Init block and bloomfilter
	block_reset(victim);
	block_erase_cnt++;
	b_table[superblk].bf_num = 0;
	memset(b_table[superblk].bf_arr, 0, sizeof(uint8_t) * bf->base_s_bytes);
	memset(sb[superblk].bf_page, 0, sizeof(Index) * _PPB);
	memset(sb[superblk].p_flag, 0, sizeof(bool) * _PPB);


	//Rewrite page and bloomfilter
	for(int i = 0 ; i < k; i++){
		gc_write++;
		lba = sort_p[i].oob;
		if(i != 0){
			hashkey = hashing_key(lba);
			bf_idx = b_table[superblk].bf_num;
			sb[superblk].bf_page[i].s_idx = bf_idx;
			set_bf(hashkey+i, superblk, i);

			sb[superblk].p_flag[i] = 1;
			b_table[superblk].bf_num++;
			lba_flag[lba] = 1;
		}
		victim->valid[i] = 1;
		victim->page[i].oob = lba;
		victim->p_offset++;
	}

	free(sort_p);
	return victim->p_offset;

}
//Single block unit GC
#else
uint32_t bloom_gc(uint32_t superblk){

	Block **bucket = b_table[superblk].b_bucket;
	Block *victim;
	SRAM *d_ram;

	uint32_t valid_cnt;
	uint32_t idx = 0;
	uint32_t max_invalid, max_idx;
	uint32_t pre_ppa;
	int64_t ppa;


	clock_t start,end;

	/*
	valid_p = (G_manager *)malloc(sizeof(G_manager) * pps);
	
	for(int i = 0 ; i < pps; i++){
		valid_p[i].t_table = NULL;
		valid_p[i].value = NULL;
	}
	*/
	/*This fuction copies valid pages for a superblock. global pointer(gm) is pointing at them */

//	start = clock();
	valid_cnt = invalid_block(bucket, -1); 
//	end = clock();

//	printf("Total invalid time : %lf\n",(double)(end-start)/CLOCKS_PER_SEC);
//	sleep(1);
	//	printf("valid_cnt : %d\n",valid_cnt);
	d_ram = (SRAM *)malloc(sizeof(SRAM) * valid_cnt);

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
	
	// Init block and bloomfilter
	__bloomftl.li->trim_block(victim->PBA * ppb, false);	
    BM_InitializeBlock(bm, victim->PBA);


	block_erase_cnt++;
	
	memset(b_table[superblk].bf_arr, 0, sizeof(uint8_t) * bf->base_s_bytes);
    memset(sb[superblk].bf_page, -1, sizeof(Index) * pps);
    memset(sb[superblk].p_flag, 0, sizeof(bool) * pps);	

	b_table[superblk].bf_num = 0;

	for(int i = 0 ; i < valid_cnt ; i++){
		if(valid_p[i].t_table->b_idx == max_idx){
			d_ram[idx].ptr_ram = (PTR)malloc(PAGESIZE);
			memcpy(d_ram[idx].ptr_ram, valid_p[i].value->value, PAGESIZE);
			d_ram[idx].oob_ram = valid_p[i].t_table->lba;
			pre_ppa = valid_p[i].t_table->ppa;
			bloom_oob[pre_ppa].lba = -1;			
			idx++;
		}
	}
	for(int i = 0; i < idx; i++){
		ppa = (victim->PBA * ppb) + victim->p_offset;
		SRAM_unload(d_ram, ppa, i, GCDW);
		BM_ValidatePage(bm,ppa);
		victim->p_offset++;
	}

	/*
	while(page_unload != 0){} // write polling
	*/

	//start = clock();	
	reset_bf_table(superblk);
	//end = clock();


#if REBLOOM
	if(idx != 0)
		b_table[superblk].pre_lba = d_ram[idx-1].oob_ram;
#endif
	b_table[superblk].c_block = SUPERBLK_SIZE-1;

	//Free data structures for GC
	for(int i = 0 ; i < pps ; i++){
		memset(gm[i].t_table, -1, sizeof(T_table));
		if(gm[i].value != NULL)
			inf_free_valueset(gm[i].value, FS_MALLOC_R);
		else{
			free(gm[i].value);
		}
		gm[i].value = NULL;
	}

//	free(valid_p);
//	free(gm);
	free(d_ram);

//	valid_p = NULL;
//	gm = NULL;

	
	return 0;
}
#endif
int invalid_block(Block **bucket, int b_idx){

	G_manager *temp_g;
	G_manager check_g, invalid_g;
	Block *checker, *invalid_b;
	uint32_t lba, pbn, p_idx;
	int64_t ppa;
	int k,weight;
	uint32_t idx = 0;
	int copy_cnt, invalid_cnt = 0;
	int start_idx;
	TYPE type;
	k = 0, idx = 0, weight = 0;	
	page_load = 0;
	clock_t start, end;


	if(b_idx == -1){
		//When GC trigger
		start_idx = SUPERBLK_SIZE-1;
	}else{
		//When RB trigger
		start_idx = b_idx;
	}

	/*
	temp_g = (G_manager *)malloc(sizeof(G_manager) * pps);
	for(int i = 0; i < pps; i++){
		temp_g[i].t_table = (T_table *)malloc(sizeof(T_table));
		memset(temp_g[i].t_table, -1, sizeof(T_table));
		temp_g[i].value = NULL;
	}
	*/
//	start = clock();
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
				gm[k].value = SRAM_load(gm, ppa, k, type);
				gm[k].t_table->weight = weight++;
				gm[k].t_table->b_idx = i;
				k++;
			}
		}
	}

	while(page_load != k){
#ifdef LEAKCHECK
	//	sleep(1);
#endif
	} //polling for all page load	
#if GC_POLL
	page_load = 0;
#endif
//	end = clock();

//	printf("GC Read time : %lf\n",(double)(end-start)/CLOCKS_PER_SEC);
	
	start = clock();
	qsort(gm, k, sizeof(G_manager), lba_compare);
	end = clock();

	//printf("sorting time: %lf\n",(double)(end-start)/CLOCKS_PER_SEC);

	check_g = gm[0];


//	start = clock();
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
			pbn = ppa / ppb;
			BM_InvalidatePage(bm, ppa);
			
			invalid_b = &bm->barray[pbn];
			invalid_cnt++;
		}
	}

//	end = clock();
//	printf("Invalid time: %lf\n",(double)(end-start)/CLOCKS_PER_SEC);
	/*	
	for(int i = 0 ; i < idx; i++){
		printf("valid_p[%d].oob: %d ppa : %d\n",i,valid_p[i].t_table->lba, valid_p[i].t_table->ppa);
	}
	exit(0);	
	*/
	//gm = temp_g;

	return idx;
}
