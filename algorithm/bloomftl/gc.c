#include "bloomftl.h"

//Superblock unit GC
#if SUPERBLK_GC
uint32_t bloom_gc(uint32_t superblk){


}
//Single block unit GC
#else
uint32_t bloom_gc(uint32_t superblk){

	Block **bucket = b_table[superblk].b_bucket;
	Block *victim;
	SRAM *d_ram;

	value_set **temp_set;

	int32_t old_block;
	int32_t valid_cnt;
	int32_t idx = 0;
	uint32_t pre_ppa;
	int64_t ppa;
	
	int32_t max_invalid, max_idx;
	clock_t start,end;

	volatile int k = 0;
	data_gc_poll = 0;
	/*
	valid_p = (G_manager *)malloc(sizeof(G_manager) * pps);
	
	for(int i = 0 ; i < pps; i++){
		valid_p[i].t_table = NULL;
		valid_p[i].value = NULL;
	}
	*/
	/*This fuction copies valid pages for a superblock. global pointer(gm) is pointing at them */

	//start = clock();
	//valid_cnt = invalid_block(bucket, -1, superblk); 
	/*
	if(superblk == 4318){
		for(int i = 0 ; i < valid_cnt; i++){
			printf("valid_p[%d].lba : %d\n",i, valid_p[i].t_table->lba);
		}
		}
	*/
	
	//end = clock();

	//printf("Total invalid time : %lf\n",(double)(end-start)/CLOCKS_PER_SEC);
//	sleep(1);
	//	printf("valid_cnt : %d\n",valid_cnt);
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
	
	/*
	for(int i = 0 ; i < valid_cnt ; i++){
		if(valid_p[i].t_table->b_idx == max_idx){
			pre_ppa = valid_p[i].t_table->ppa;
			bloom_oob[pre_ppa].lba = -1;	
			d_ram[idx].oob_ram = valid_p[i].t_table->lba;
			d_ram[idx].ptr_ram = valid_p[i].value->value;
			//d_ram[idx].ptr_ram = (PTR)malloc(PAGESIZE);
			//memcpy(d_ram[idx].ptr_ram, valid_p[i].value->value, PAGESIZE);
			idx++;
		}
	}
	*/
	
	
	/*
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
	*/
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
	/*
	for(int i = 0 ; i < pps ; i++){
		memset(gm[i].t_table, -1, sizeof(T_table));
		if(gm[i].value != NULL)
			inf_free_valueset(gm[i].value, FS_MALLOC_R);
		else{
			free(gm[i].value);
		}
		gm[i].value = NULL;

		valid_p[i].t_table = NULL;
		valid_p[i].value = NULL;
	}
	*/
	//Free data structures for GC
	free(d_ram);

	
	return 0;
}
#endif
int invalid_block(Block **bucket, int b_idx, uint32_t superblk){

	G_manager *temp_g;
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
	//clock_t start, end;

	uint32_t test_lba;
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
	gm = temp_g;
	*/
	/*
	for(int i = 0; i < pps; i++){
		memset(gm[i].t_table, 0, sizeof(T_table));
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

				gm[k].t_table->lba = bloom_oob[ppa].lba;
				gm[k].t_table->ppa = ppa;
				gm[k].t_table->weight = weight++;
				gm[k].t_table->b_idx = i;
				gm[k].value = SRAM_load(ppa, k, type);
			


				test_lba = gm[k].t_table->lba;

//				uint32_t superblk = (test_lba>>2) % nos;
//				printf("superblk : %d\n",superblk);
				/*
				if(superblk==4318){
					printf("---LBA---\n");
					printf("gm[%d].lba :  %d\n",valid_page_num,gm[k].t_table->lba);
				}
				*/			

				k++;
				valid_page_num++;
			}
		}
	}


	while(data_gc_poll != k){} //polling for all page load	
	
	//printf("valid_page_num : %d\n",k);
//	end = clock();

//	printf("GC Read time : %lf\n",(double)(end-start)/CLOCKS_PER_SEC);

//	start = clock();
	qsort(gm, k, sizeof(G_manager), lba_compare);
/*	
	printf("test_lba : %d\n", test_lba);
	printf("k : %d\n", k);
	for(int i = 0 ; i < pps; i++){
		printf("gm[%d].lba :  %d\n",i,gm[i].t_table->lba);
	}
	*/
	//	end = clock();

	//printf("sorting time: %lf\n",(double)(end-start)/CLOCKS_PER_SEC);

	//test_lba = gm[k].t_table->lba;
	/*
	if(superblk==4318){
		printf("test_lba : %d\n", test_lba);
		printf("superblk : %d\n",superblk);
		printf("k : %d\n", k);
		for(int i = 0 ; i < pps; i++){
			printf("gm[%d].lba :  %d\n",i,gm[i].t_table->lba);
		}
		sleep(1);
	}
	*/


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
			//pbn = ppa / ppb;
			BM_InvalidatePage(bm, ppa);
			
//			invalid_b = &bm->barray[pbn];
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
