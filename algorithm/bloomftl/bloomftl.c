#include "bloomftl.h"

algorithm __bloomftl = {
	.create  = bloom_create,
	.destroy = bloom_destroy,
	.read    = bloom_read,
	.write   = bloom_write,
	//.remove  = bloom_remove
	.remove = NULL
};

BF *bf;
struct blockmanager *bloom_bm;
__segment **g_seg;

BF_TABLE *b_table;	//Global BF table
B_OOB *bloom_oob;	//For OOB, but this will change by new blockmanager

SBmanager *sb;		//BFs information for a superblock

//Data structure for GC
G_manager *gm;		
G_manager *valid_p;

//Data structure for Reblooming
#if REBLOOM
G_manager *rb;
G_manager *re_list;
#endif

//Variable to check set LBA, this can remove
bool *lba_flag;
bool *lba_bf;

#if W_BUFF
skiplist *write_buffer;
uint64_t max_write_buf;
uint32_t buf_idx;
struct prefetch_struct *prefetcher;
#endif

/* Variables for I/O count */
uint32_t algo_write_cnt;
uint32_t algo_read_cnt;
uint32_t gc_write;
uint32_t gc_read;
volatile int32_t data_gc_poll;
uint32_t block_erase_cnt;
uint32_t found_cnt;
uint32_t not_found_cnt;
uint32_t rb_read_cnt;
uint32_t rb_write_cnt;
uint32_t sub_lookup_read;
uint32_t remove_read;


uint32_t lnb;	       //Total num of logical block
uint32_t mask;	       //Available num of page in superblock
int32_t nob;	       //Num of block
int32_t ppb;           //Page per block
int32_t nop;	       //Num of physical page
int32_t lnp;	       //Num of logical page
int32_t n_superblocks; //Total num of superblock
int32_t n_segments;    //Total num of segment
int32_t pps;	       //Page per superblock



#if REBLOOiM
uint32_t r_check;
#endif

uint32_t g_cnt;

uint32_t bloom_create(lower_info *li, blockmanager *bm,  algorithm *algo){


	algo->li = li;
	bloom_bm = bm;
	
	/* Set block configuration & Global bloomfilter */
	ppb = _PPB;
	pps = SUPER_PAGES
	bf = bf_init(1, pps);

	lnp = L_PAGES;
	mask = MASK;
#if REBLOOM
	r_check = pps/2;
	bf->base_s_bytes = (bf->base_s_bytes);
#endif

#if W_BUFF
	write_buffer = skiplist_init();
	max_write_buf = 1024;
	prefetcher = (struct prefetch_struct *)malloc(sizeof(struct prefetch_struct) * max_write_buf);
	for(size_t i = 0 ; i < max_write_buf; i++){
		prefetcher[i].ppa = 0;
		prefetcher[i].sn = NULL;
	}
#endif

	n_segments = _NOS;			
	nob = _NOB;		 		
	n_superblock = nob / SUPERBLK_SIZE;    
	nop = _NOP;		   	

	/* Set table for BloomFTL */

	b_table = (BF_TABLE *)malloc(sizeof(BF_TABLE) * n_superblocks);
	sb = (SBmanager *)malloc(sizeof(SBmanager) * n_superblocks);

	lba_flag = (bool *)malloc(sizeof(bool) * lnp);	
	lba_bf   = (bool *)malloc(sizeof(bool) * lnp);	
	bloom_oob = (B_OOB *)malloc(sizeof(B_OOB) * nop);

	memset(lba_flag, 0, sizeof(bool) * lnp);
	memset(lba_bf, 0, sizeof(bool) * lnp);
	memset(bloom_oob, -1, sizeof(B_OOB) * nop);

	/* Set data structure for GC */
	gm = (G_manager *)malloc(sizeof(G_manager) * pps);	
	valid_p = (G_manager *)malloc(sizeof(G_manager) * pps);


	for(int i = 0 ; i < pps; i++){		
		gm[i].t_table = (T_table *)malloc(sizeof(T_table));
		memset(gm[i].t_table, 0, sizeof(T_table));
		gm[i].value = NULL;
		gm[i].size = 0;
		valid_p[i].t_table = NULL;
		valid_p[i].value = NULL;
	}


#if REBLOOM
	/* Set data structure for Reblooming */

	//When reblooming is trigger, use a bucket for coalesced pages 
	rb = (G_manager *)malloc(sizeof(G_manager) * pps);
	for(int i = 0 ; i < pps ; i++){
		rb[i].t_table = (T_table *)malloc(sizeof(T_table) * MAX_RB);
		rb[i].value   = (value_set *)malloc(sizeof(value_set) * MAX_RB);
		memset(rb[i].t_table, -1, sizeof(T_table) * MAX_RB);
		rb[i].size = 0;
	}
	//When reblooming is trigger, use a bucket for remain pages 
	re_list = (G_manager *)malloc(sizeof(G_manager) * SUPERBLK_SIZE);
	for(int i = 0; i < SUPERBLK_SIZE; i++){
		re_list[i].t_table = (T_table *)malloc(sizeof(T_table) * ppb);
		re_list[i].value   = (value_set *)malloc(sizeof(value_set) * ppb);
		memset(re_list[i].t_table, -1, sizeof(T_table) * ppb);
		re_list[i].size = 0;
	}

#endif

	printf("---- BloomFTL (SINGLE) ----\n");
	printf("Storage total Logical pages        : %d\n",lnp);
	printf("Storage total Physical pages       : %d\n",nop);
	printf("Storage total segments		   : %d\n",n_segments);
	printf("Storage total Superblocks          : %d\n",n_superblocks);
	printf("Storage total Physical blocks      : %d\n",nob);
	printf("Storage Data page per Singleblock  : %d\n",ppb);
	printf("Storage Data page per Superblock   : %d\n",mask);
	printf("Storage OP   page per Superblock   : %d\n",pps - mask);
	printf("Storage Bloomfilter per Superblock : %d\n",bf->base_s_bytes);
#if REBLOOM
	printf("Storage Rebloom trigger count      : %d\n",r_check);
#endif

	printf("Out-of-Range : %ld\n",OOR);
	//Init BF table for physical blocks
	for(int i = 0 ; i < n_superblocks; i++){
		b_table[i].bf_arr = (uint8_t *)malloc(sizeof(uint8_t) * bf->base_s_bytes);	
		memset(b_table[i].bf_arr, 0, sizeof(uint8_t) * bf->base_s_bytes);
		b_table[i].bf_num = 0;	
		b_table[i].b_bucket = (__block **)malloc(sizeof(__block *) * SUPERBLK_SIZE);
		b_table[i].c_block = 0;
		b_table[i].full = 0;
		b_table[i].first = 0;
#if REBLOOM
		b_table[i].pre_lba = OOR;
		b_table[i].rb_cnt = 0;
#endif
		sb[i].bf_page = (Index *)malloc(sizeof(Index) * pps);
		sb[i].p_flag = (bool *)malloc(sizeof(bool) * pps);

		memset(sb[i].bf_page, -1, sizeof(Index) * pps);
		memset(sb[i].p_flag, 0, sizeof(bool) * pps);

		sb[i].num_s_bits = bf->base_s_bits;
		sb[i].num_s_bytes = bf->base_s_bytes;

	}



	//Set blockmanager and physical blocks
	bloom_bm->create(bm, li);
	g_seg = (__segment **)malloc(sizeof(__segment *) * n_segments);
	for(int i = 0 ; i < n_segments; i++){
		g_seg[i] = get_segment(bm, 0);
	}

	int check_cnt=0, seg_idx=0;
	for(int i = 0 ; i < n_superblocks; i++){
		for(int j = 0 ; j < SUPERBLK_SIZE; j++){
			if(check_cnt==BPS){
				seg_idx++;
				check_cnt = 0;
			}
			__block *g_block = bm->get_block(bm, g_seg[seg_idx]);
			b_table[i].bucket[j] = g_block;
			check_cnt++;
			printf("block_id : %d\n",g_block->block_num);

		}
	}


	return 1;

}
void bloom_destroy(lower_info *li, algorithm *algo){

	double bf_memory    = 0.0;
	double flag_memory  = 0.0;
	double total_memory = 0.0; 
	double max_memory = 0.0;
	printf("--------- Benchmark Result ---------\n\n");
	printf("Total request  I/O count   : %d\n",algo_write_cnt+algo_read_cnt);
	printf("Total write    I/O count   : %d\n",algo_write_cnt);
	printf("Total read     I/O count   : %d\n",algo_read_cnt);
	printf("Total found count          : %d\n",found_cnt);
	printf("Total Not found count      : %d\n",not_found_cnt);
	printf("Total GC write I/O count   : %d\n",gc_write);
	printf("Total GC read  I/O count   : %d\n",gc_read);
#if REBLOOM
	printf("Total Remove read I/O count: %d\n",remove_read);
	printf("Total sub lookup I/O count : %d\n",sub_lookup_read);
	printf("Total RB write I/O count   : %d\n",rb_write_cnt);
	printf("Total RB read  I/O count   : %d\n",rb_read_cnt);
#endif
	printf("Total block erase count    : %d\n",block_erase_cnt);
	if(algo_read_cnt != 0){
		printf("Total RAF Result       : %.2lf\n", (float) (sub_lookup_read+found_cnt+not_found_cnt) / algo_read_cnt);
	}
	if(algo_write_cnt != 0){
#if REBLOOM	
		printf("Total WAF Result       : %.2lf\n", (float) (algo_write_cnt+gc_write+rb_write_cnt)/algo_write_cnt);
#else
		printf("Total WAF Result       : %.2lf\n", (float) (algo_write_cnt+gc_write) / algo_write_cnt);
#endif
	}

	//Free table
	bf_free(bf);

	//Calculate memory usage for BFs
	for(int i = 0 ; i < n_superblocks; i++){
		bf_memory   += (b_table[i].bf_num * 12) / 8; // for BF
		flag_memory += (ppb*SUPERBLK_SIZE) / 8;
		max_memory  += (512*12)/8;

		//free(b_table[i].bf_arr);
		free(sb[i].bf_page);
		free(sb[i].p_flag);
	}
#if REBLOOM

	total_memory = bf_memory + flag_memory;
	max_memory += flag_memory;
#else
	total_memory = bf_memory;
#endif

	printf("Cached BF memory       (MB) : %.2lf\n",bf_memory/1024/1024);
#if REBLOOM
	printf("Sequential bit memory  (MB) : %.2lf\n",flag_memory/1024/1024);
#endif
	printf("Memory requirement     (MB) : %.2lf\n",total_memory/1024/1024);
	printf("Max memory requirement (MB) : %.2lf\n",max_memory/1024/1024);

	free(b_table);
	free(sb);

	free(lba_flag);
	free(lba_bf);

	for(int i = 0 ; i < pps; i++){

		free(gm[i].t_table);
		gm[i].value = NULL;
		gm[i].size = 0;
	}


#if REBLOOM
	free(rb);
	free(re_list);
#endif

	free(gm);
	free(valid_p);

	//block Free
	free(bloom_oob);
	bm->destory(bm);

#if W_BUFF
	skiplist_free(write_buffer);
#endif

	return ;
}



uint32_t bloom_write(request* const req){
	uint32_t ppa;
	uint32_t lba = req->key;
	uint32_t flush_lba;
	algo_req *my_req;

	snode *temp;
	static bool is_flush = false;

#if W_BUFF

	/* Use write buffer, this is same write buffer of DFTL */
	if(write_buffer->size == max_write_buf){
		for(size_t i = 0; i < max_write_buf; i++){
			temp = prefetcher[i].sn;
			flush_lba = temp->key;
			ppa = ppa_alloc(flush_lba);
			my_req = assign_pseudo_req(DATAW, temp->value, NULL);
			__bloomftl.li->write(ppa, PAGESIZE, temp->value, ASYNC, my_req);
			temp->value = NULL;
		}

		for(size_t i = 0 ; i < max_write_buf; i++){
			prefetcher[i].ppa = 0;
			prefetcher[i].sn = NULL;
		}
		buf_idx = 0;
		skiplist_free(write_buffer);
		write_buffer = skiplist_init();

		__bloomftl.li->lower_flying_req_wait();
		is_flush=true;

	}

	temp = skiplist_insert(write_buffer, lba, req->value, true);

	if(write_buffer->size == buf_idx+1){
		prefetcher[buf_idx++].sn = temp;
	}
#endif
	req->value = NULL;
	req->end_req(req);

	algo_write_cnt++;

	return 1;


}

uint32_t bloom_read(request* const req){

#if W_BUFF
	snode *temp;
#endif

	algo_req *my_req;
	uint32_t ppa;
	uint32_t lba = req->key;
	if(lba > RANGE+1){
		printf("range error %d\n",lba);
		exit(0);
	}


	algo_read_cnt++;
	if(lba_flag[lba] == 0){
		req->end_req(req);
		return UINT32_MAX;
	}

	if((temp = skiplist_find(write_buffer, lba))){
		memcpy(req->value->value, temp->value->value, PAGESIZE);
		req->type_ftl = 0;
		req->type_lower = 0;
		req->end_req(req);
		return 1;
	}



	//printf("read_cnt : %d\n",read_cnt++);
	my_req = assign_pseudo_req(DATAR, NULL, req);
#if !REBLOOM
	if(!lba_bf[lba]){
		ppa = check_first(lba);
		__bloomftl.li->read(ppa, PAGESIZE, req->value, ASYNC, my_req);
		return 1;
	}
#endif

	ppa = table_lookup(lba, 1);



	__bloomftl.li->read(ppa, PAGESIZE, req->value, ASYNC, my_req);

	return 1;

}

uint32_t bloom_remove(request* const req){

	algo_req *my_req;
	uint32_t lba = req->key;
	uint32_t ppa;
	if(lba_flag[lba] == 0){
		req->end_req(req);
		return UINT32_MAX;
	}


	if(lba_flag[lba] == 0){
		req->end_req(req);
		return UINT32_MAX;
	}


	my_req = assign_pseudo_req(DATAR, NULL, req);
	ppa = table_lookup(lba,1);	
	__bloomftl.li->read(ppa, PAGESIZE, req->value, ASYNC, my_req);

	remove_read++;
//	BM_InvalidatePage(bm, ppa);
	bloom_oob[ppa].lba = -1;


	req->end_req(req);
	return 1;




}


void *bloom_end_req(algo_req *input){
	bloom_params *params = (bloom_params *)input->params;
	value_set *temp_set = params->value;
	request *res = input->parents;

	switch(params->type){
		case DATAR:
			if(res){
				res->end_req(res);
			}
			break;
		case DATAW:
#if W_BUFF
			inf_free_valueset(temp_set, FS_MALLOC_W);
#endif
			if(res){
				res->end_req(res);
			}
			break;
		case GCDR:
			gc_read++;
			data_gc_poll++;
			break;
		case GCDW:
			gc_write++;
			inf_free_valueset(temp_set,FS_MALLOC_W);
			break;
		case RBR:
			rb_read_cnt++;
			data_gc_poll++;
			break;
		case RBW:
			rb_write_cnt++;	
			inf_free_valueset(temp_set,FS_MALLOC_W);
			break;
	}
	free(params);
	free(input);
	return NULL;
}


