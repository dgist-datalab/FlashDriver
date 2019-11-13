#include "hashftl.h"
#include "../../bench/bench.h"


/* HASHFTL methods */
algorithm __hashftl = {
    .create  = hash_create,
    .destroy = hash_destroy,
    .read    = hash_get,
    .write   = hash_set,
    .remove  = hash_remove
};

//heap globals.
b_queue *free_b;                 //Set queue to manage free blocks
b_queue *victim_b;		 //Set queue to manage victim blocks
Heap *b_heap;			 //Set heap structure to manage primary blocks
Heap *primary_b;
Heap *secondary_b;


struct gc_block *gc_block;


PRIMARY_TABLE *pri_table;        //primary table
SECONDARY_TABLE *sec_table;      //secondary table
H_OOB *hash_OOB;                 //OOB area.

//blockmanager globals.
BM_T *bm;
Block *reserved;    	         //reserved.
int32_t reserved_pba;
int32_t empty_idx;




//global for macro.
int32_t _g_nop;				// number of page
int32_t _g_nob;				// number of block
int32_t _g_ppb;				// page per block

int32_t num_b_primary;

int32_t max_secondary;        // secondary table size
int32_t num_secondary;        // allocated secondary table
int32_t num_b_secondary;      // Max num of secondary block
int32_t num_hid;              // number of hid bits
int32_t num_ppid;             // number of ppid bits
int32_t num_page_off;          // number of page offset bits
int32_t hid_secondary;        // hid that indicate mapping is at secondary table
int32_t loop_gc;			// number of gc to immitate chip level parallelism

volatile int32_t gc_load;
int32_t gc_count;			// number of total gc
int32_t re_gc_count;		// number of gc due to remap
int32_t re_page_count;		// number of moved page during remap

// info during remap
int32_t gc_val;
int32_t re_number;
int32_t gc_pri;

int32_t num_write;
int32_t num_copy;

int32_t valid_copy;
int32_t remap_copy;

int32_t low_watermark;
int32_t high_watermark;
int32_t num_entry_segment;
int32_t pba_secondary;
int32_t secondary_ppa;
int32_t start_b_secondary;

int32_t pri_count;
int32_t sec_count;


//Count variable

int32_t gc_w;
int32_t gc_r;
int32_t data_w;
int32_t data_r;
int32_t remap_write;

static void print_algo_log() {
	printf("\n");
	printf(" |---------- algorithm_log : Hash FTL\n");

	printf(" | Total Blocks(Segments)   : %d\n", _g_nob);
	printf(" | Total Pages              : %d\n", _g_nop);
	printf(" | Num of primary blocks    : %d\n", num_b_primary);
	printf(" | Num of secondary blocks  : %d\n", num_b_secondary);
	printf(" | Num of reserved block    : %d\n", 1);
	printf(" | Init reserved_pba        : %d\n",reserved_pba);
	printf(" | start secondary block    : %d\n", start_b_secondary);
	printf(" | Last  secondary block    : %d\n", _g_nob-2);
	printf(" |  -Page per Block         : %d\n\n", _g_ppb);

	printf(" | Hash ID bits             : %d\n", num_hid);
	printf(" | ppid bits                : %d\n", num_ppid);
	printf(" | page offset bits         : %d\n", num_page_off);
	printf(" | Total secondary entries  : %d\n", max_secondary);
	printf(" | GC low watermark         : %d\n", low_watermark);
	printf(" | GC high watermark        : %d\n", high_watermark);
	printf(" | Hid secondary            : %d\n", hid_secondary);
	printf(" | Num of entry segment (Secondary): %d\n",num_entry_segment); 
}

uint32_t hash_create(lower_info *li, algorithm *algo){
	int temp, bit_cnt;

	printf("Start create hash FTL\n");
	/* Initialize pre-defined values by using macro */
	_g_nop = _NOP;
	_g_nob = _NOS;
	_g_ppb = _PPS;
/*
	_g_nop = _NOP;
	_g_nob = _NOB;
	_g_ppb = _PPB;
*/
	gc_count = 0;
	re_gc_count = 0;
	re_page_count = 0;
	loop_gc = 8;


//	gc_block = (struct gc_block *)malloc(sizeof(struct gc_block) * _g_ppb);
	

	// Setting secondary
	num_b_secondary = ceil(_g_nob * 0.05);
	num_b_primary = _g_nob - num_b_secondary - 1;

	start_b_secondary = num_b_primary;
	max_secondary = num_b_secondary * _g_ppb;	
	low_watermark = max_secondary * 0.7;		      //Set GC trigger low watermark
	high_watermark = max_secondary * 0.5;		      //Set GC stop high watermark
	pba_secondary = -1;

	empty_idx = 0;
		
	temp = _g_ppb;
	bit_cnt = 0;
	while(temp != 0){
		temp = temp >> 1;
		bit_cnt++;
	}

	num_secondary = 0;                                    //init # of secondary table entry
	num_hid = 3;					      //Set h_bit (3)
	num_page_off = bit_cnt - 1;			      //Set p_bit (8)
	//num_ppid = num_page_off;
	num_ppid = num_page_off;			      //Set m_bit (6)
	hid_secondary = pow(2, num_hid) - 1;

	num_write = 0;
	num_copy = 0;

	// Map lower info
	algo->li = li;
	num_entry_segment = max_secondary / pow(2,num_ppid);
	//print information

	//Set block information
	bm = BM_Init(_g_nob, _g_ppb, 2, 1);		      //Init Physical blocks
	/*
	
	Block *t_block;
	for(int i = 0; i < num_b_primary; i++){
		t_block = &bm->barray[i];
		t_block->Invalid = _g_ppb;
		t_block->type = 1;
	}
	*/
	/*
	for(int i=1;i<_g_nob;i++){
		BM_Enqueue(free_b, &bm->barray[i]);
	}
	*/


	//Reserved block for secondary block
	reserved = &bm->barray[_g_nob-1];			      //reserved block for GC
	reserved_pba = _g_nob-1;				      //Set index of reserved physical block
	BM_Queue_Init(&free_b);				      	      //Enqueue free blocks to queue
	
	print_algo_log();
	//Free secondary blocks 
	
	//Secondary range
	for(int i = start_b_secondary; i < _g_nob-1; i++){
		BM_Enqueue(free_b, &bm->barray[i]);
	}




	//b_heap = BM_Heap_Init(_g_nob-1);//total size == NOB - 1.
	primary_b = BM_Heap_Init(num_b_primary);
	secondary_b = BM_Heap_Init(num_b_secondary);

	bm->harray[0] = primary_b;
	bm->harray[1] = secondary_b;
	bm->qarray[0] = free_b;


	pri_table = (PRIMARY_TABLE*)malloc(sizeof(PRIMARY_TABLE) * _g_nop);
	hash_OOB = (H_OOB*)malloc(sizeof(H_OOB) * _g_nop);
	sec_table = (SECONDARY_TABLE*)malloc(sizeof(SECONDARY_TABLE) * max_secondary);

	// initialize primary table & oob
	for(int i=0;i<_g_nop;i++){
		pri_table[i].hid = -1;
		pri_table[i].ppid = -1;
		pri_table[i].ppa = -1;
		pri_table[i].state = CLEAN;
		hash_OOB[i].lpa = -1;
	}

	// initialize primary table & oob
	for(int i=0;i<max_secondary;i++){
		sec_table[i].ppa = -1;
		sec_table[i].lpa = -1;
		sec_table[i].state = CLEAN;
//		sec_table[i].gc_flag = 0;
	}
/*
	for(int i = 0; i < _g_ppb; i++){
		gc_block[i].hid = -1;
		gc_block[i].ppid = -1;
		gc_block[i].idx_secondary = -1;
		gc_block[i].state = 0;
	}
*/
	return 0;
}


void *hash_end_req(algo_req* input){
	/*
	 * end req differs according to type.
	 * frees params and input.
	 * in some case, frees inf_req.
	 */
	hash_params *params = (hash_params*)input->params;
	value_set *temp_set = params->value;
	request *res = input->parents;

	switch(params->type){
		case DATA_R:
			data_r++;
			if(res){
				res->end_req(res);
			}
			break;
		case DATA_W:
			data_w++;	
			if(res){
				res->end_req(res);
			}	
			break;
		case GC_R:
			gc_r++;
			gc_load++;
			//inf_free_valueset(temp_set, FS_MALLOC_R);
			//free(temp_set);
			break;
		case GC_W:
			gc_w++;
			if(temp_set == NULL){
				printf("\nhash_end_req: temp_set is NULL\n");
			}
			inf_free_valueset(temp_set, FS_MALLOC_W);
			//free(temp_set);
			break;
	}
	free(params);
	free(input);
	return NULL;
}

void hash_destroy(lower_info *li, algorithm *algo){

	puts("");

	Block *block;
	/* Print information */
	printf("Hash FTL summary start---------------------------------\n");
	printf("# of gc: %d\n", gc_count);
//	printf("# of gc due to remap: %d\n", re_gc_count);
//	printf("# of moved page due to remap: %d\n\n", re_page_count);


	printf("Total secondary entries: %d\n", max_secondary);
	printf("Used secondary entries: %d\n\n", num_secondary);
	
	printf("Total valid copy : %d\n",valid_copy);
	printf("Total remap copy : %d\n",remap_copy);
	
	printf("Total writes: %d\n", data_w);
	printf("Total reads : %d\n", data_r);
	printf("Total GC_W  : %d\n", gc_w);
	printf("Total GC_R  : %d\n", gc_r);

	//printf("Copyed writes: %d\n", num_copy);
	printf("Total WAF : %.2f\n", (float) (data_w + gc_w) / data_w);
	
	
	printf("Hash FTL summary end----------------------------------\n");

	
	printf("---------- Block info ----------\n");
	for(int i = 0 ; i < _g_nob; i++){
		block = &bm->barray[i];
		printf("block[%d] = %d\n",i,block->wr_off);
	}
	

	/* Clear modules */
	BM_Free(bm);
	/* Clear tables */
	free(pri_table);
	free(sec_table);
	free(hash_OOB);
}

uint32_t hash_get(request* const req){
	/*
	 * gives pull request to lower level.
	 * reads mapping data.
	 * !!if not mapped, does not pull!!
	 */
	int32_t lpa;
	int32_t ppa;

	lpa = req->key;
	ppa = get_ppa_from_table(lpa);
	if(ppa == -1){
		req->type = FS_NOTFOUND_T;
		req->end_req(req);
		return 1;
	}
	__hashftl.li->read(ppa, PAGESIZE, req->value, ASYNC, assign_pseudo_req(DATA_R, NULL, req));
	return 0;
}

uint32_t hash_set(request* const req){
	int32_t lpa;
	int32_t ppa;
	int32_t idx_secondary;
	int32_t sec_ppid;
	int32_t hid;
	int32_t cal_ppid;
	
	Block *block;
	algo_req *my_req;
	int cnt;

	lpa = req->key;
	if(lpa > RANGE + 1){
		printf("hash set: range error\n");
		exit(3);
	}

	hid = 0;
	ppa = alloc_page(lpa, &cal_ppid, &hid);
	if(ppa == -1){
		printf("ppa is not found error!\n");
		exit(0);
	}
	//Invalidate previous ppa (update case)
	if(pri_table[lpa].hid != -1){
		BM_InvalidatePage(bm ,get_ppa_from_table(lpa));
		if(pri_table[lpa].hid == hid_secondary){
			idx_secondary = get_idx_for_secondary(lpa);
			sec_table[idx_secondary].lpa = -1;	
			sec_table[idx_secondary].ppa = -1;
			sec_table[idx_secondary].state = CLEAN;
			num_secondary--;
		}
	}

	// put information to table and validate page
	pri_table[lpa].hid = hid;
	pri_table[lpa].state = H_VALID;

	/* if you want to check real hash ftl performance,
	   you should change find ppa logic (hashftl_util.c -> get_ppa_from_table)
	   for now, primary table contain ppa for convenience*/
	pri_table[lpa].ppa = ppa;

	
	if(hid < hid_secondary){          //if hid is primary
		pri_table[lpa].ppid = cal_ppid;
	}else{    //if hid is secondary
		//find free entry at secondary table
		idx_secondary = set_idx_secondary();
	
		//calculate ppid for secondary table	
		sec_ppid = pow(2,num_ppid);
		sec_ppid = sec_ppid * idx_secondary;
		sec_ppid = sec_ppid / max_secondary;
		pri_table[lpa].ppid = sec_ppid;

		//set lpa, ppa, status	
		sec_table[idx_secondary].lpa = lpa;
		sec_table[idx_secondary].ppa = ppa;
		sec_table[idx_secondary].state = H_VALID;
		num_secondary++;

	}
	my_req = assign_pseudo_req(DATA_W, NULL, req);
	__hashftl.li->write(ppa, PAGESIZE, req->value, ASYNC, my_req);
	

	BM_ValidatePage(bm, ppa);
	hash_OOB[ppa].lpa = lpa;
	return 0;
}


uint32_t hash_remove(request* const req){

	int32_t lpa;
	int32_t ppa, idx_secondary;
	//hash_params * params;

	bench_algo_start(req);
	lpa = req->key;
	if (lpa > RANGE + 1) {
		printf("range error %d\n",lpa);
		exit(3);
	}

	ppa = pri_table[lpa].ppa;

	if(pri_table[lpa].hid == hid_secondary){
		idx_secondary = get_idx_for_secondary(lpa);
		sec_table[idx_secondary].lpa = -1;
		sec_table[idx_secondary].ppa = -1;
		sec_table[idx_secondary].state = CLEAN;
	}


	pri_table[lpa].ppa = -1; //reset to default.
	hash_OOB[lpa].lpa = -1; //reset reverse_table to default.
	pri_table[lpa].hid = -1;
	pri_table[lpa].state = CLEAN;

	BM_InvalidatePage(bm, ppa);
	bench_algo_end(req);
	return 0;
}
