#include "bloomftl.h"

algorithm __bloomftl = {
	.create  = bloom_create,
	.destroy = bloom_destroy,
	.read    = bloom_read,
	.write   = bloom_write,
	.remove  = NULL
};

BF *bf;
BM_T *bm;
BF_TABLE *b_table;
B_OOB *bloom_oob;

SBmanager *sb;
G_manager *gm;
G_manager *valid_p;
G_manager *write_p;
#if REBLOOM
G_manager *rb;
G_manager *re_list;
#endif
bool *lba_flag;
bool *lba_bf;

bool check_flag;
int32_t check_cnt;

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

uint32_t lnb;
uint32_t mask;

int32_t nob;
int32_t ppb;
int32_t nop;
int32_t lnp;

uint32_t r_count;

bool rb_flag;
bool gc_flag;

#if !SUPERBLK_GC
int32_t nos;
int32_t pps;
#endif
#if REBLOOM
uint32_t r_check;
#endif

uint32_t g_cnt;

#if SUPERBLK_GC
void bloom_create(void){

	ppb = _PPB;
	//Set global bloomfilter
	bf = bf_init(1, ppb);


	lnp = L_DEVICE / PAGESIZE;
	mask = ceil((ppb * (1 - 0.07)));

	nob = (lnp / mask) + 1;
	nop = nob * ppb;
#if REBLOOM
	r_check = ppb/2;
#endif


	b_table = (BF_TABLE *)malloc(sizeof(BF_TABLE) * nob);
	sb = (SBmanager *)malloc(sizeof(SBmanager) * nob);
	gm = (GCmanager *)malloc(sizeof(GCmanager) * mask);
	lba_flag = (bool *)malloc(sizeof(bool) * lnp);

	memset(gm, 0, sizeof(GCmanager) * mask);
	memset(lba_flag, 0, sizeof(bool) * lnp);

	printf("---- BloomFTL (SUPER) ----\n");
	printf("Storage total Logical pages     : %d\n",lnp);
	printf("Storage total Physical pages    : %d\n",nop);
	printf("Storage total Physical blocks   : %d\n",nob);
	printf("Storage Data page per block     : %d\n",mask);
	printf("Storage OP   page per block     : %d\n",ppb - mask);
#if REBLOOM
	printf("Storage Rebloom trigger count   : %d\n",r_check);
#endif


	//Set BF table for physical blocks
	for(int i = 0 ; i < nob; i++){
		b_table[i].bf_arr = (uint8_t *)malloc(sizeof(uint8_t) * bf->base_s_bytes);
		memset(b_table[i].bf_arr, 0, sizeof(uint8_t) * bf->base_s_bytes);
		b_table[i].bf_num = 0;
		
#if REBLOOM
		b_table[i].pre_lba = -1;
		b_table[i].rb_cnt = 0;
		sb[i].lba_bf = (bool *)malloc(sizeof(bool) * mask);
		memset(sb[i].lba_bf, 0, sizeof(bool) * mask);	
#endif
	
		sb[i].bf_page = (Index *)malloc(sizeof(Index) * ppb);
		sb[i].p_flag = (bool *)malloc(sizeof(bool) * ppb);
		memset(sb[i].bf_page, -1, sizeof(Index) * ppb);
		memset(sb[i].p_flag, 0, sizeof(bool) * ppb);
		
		sb[i].num_s_bits = bf->base_s_bits;
		sb[i].num_s_bytes = bf->base_s_bytes;
		
	}

	bm = storage_init(nob, ppb);

	return ;

}
#else
uint32_t bloom_create(lower_info *li, algorithm *algo){


	algo->li = li;

	ppb = _PPB;
	//pps = ppb * SUPERBLK_SIZE;
	pps = _PPS*SUPERBLK_SIZE;
	
	//Set global bloomfilter
	bf = bf_init(1, pps);


	//lnp = L_DEVICE / PAGESIZE;
	//mask = ceil((pps * (1 - 0.07)));
	lnp = L_PAGES;
	mask = MASK;

#if REBLOOM
	r_check = pps/2;
#endif


	if(_PPB != (1 << 7)){
		printf("PAGE PER BLOCK ERROR. Please reset macro _PPB variable\n");
		exit(0);
	}
//	printf("bf->base_s_bytes : %d\n", bf->base_s_bytes);

	//nob = (lnp / mask);
	//nob = (nob+1) * SUPERBLK_SIZE;
	nob = _NOS;
	nos = nob / SUPERBLK_SIZE;
	//nop = nob * ppb;
	nop = _NOP;

	b_table = (BF_TABLE *)malloc(sizeof(BF_TABLE) * nos);
	sb = (SBmanager *)malloc(sizeof(SBmanager) * nos);

	lba_flag = (bool *)malloc(sizeof(bool) * lnp);	
	lba_bf   = (bool *)malloc(sizeof(bool) * lnp);	
	bloom_oob = (B_OOB *)malloc(sizeof(B_OOB) * nop);

	memset(lba_flag, 0, sizeof(bool) * lnp);
	memset(lba_bf, 0, sizeof(bool) * lnp);
	memset(bloom_oob, -1, sizeof(B_OOB) * nop);

	//GC data structure
#if !REBLOOM
	gm = (G_manager *)malloc(sizeof(G_manager) * pps);	
#endif
	valid_p = (G_manager *)malloc(sizeof(G_manager) * pps);


	for(int i = 0 ; i < pps; i++){		
#if !REBLOOM
		gm[i].t_table = (T_table *)malloc(sizeof(T_table));
		memset(gm[i].t_table, 0, sizeof(T_table));
		gm[i].value = NULL;
		gm[i].size = 0;
#endif
		valid_p[i].t_table = NULL;
		valid_p[i].value = NULL;
	}


	
	/*
	valid_p = (G_manager *)malloc(sizeof(G_manager) * pps);
	write_p = (G_manager *)malloc(sizeof(G_manager) * pps);
	for(int i = 0 ; i < pps; i++){
		valid_p[i].t_table = (T_table *)malloc(sizeof(T_table));
		write_p[i].t_table = (T_table *)malloc(sizeof(T_table));
		memset(valid_p[i].t_table, 0, sizeof(T_table));
		memset(write_p[i].t_table, 0, sizeof(T_table));
		write_p[i].value = valie_p[i].value = NULL;
	}
	*/
#if REBLOOM
	
	//When reblooming is trigger, use a bucket for coalesced pages 
	/*
	rb = (G_manager *)malloc(sizeof(G_manager) * pps);
	for(int i = 0 ; i < pps ; i++){
		rb[i].t_table = (T_table *)malloc(sizeof(T_table) * MAX_RB);
		rb[i].value   = (value_set *)malloc(sizeof(value_set) * MAX_RB);
		memset(rb[i].t_table, 0, sizeof(T_table) * MAX_RB);
		rb[i].size = 0;
	}
	//When reblooming is trigger, use a bucket for remain pages 
	re_list = (G_manager *)malloc(sizeof(G_manager) * SUPERBLK_SIZE);
	for(int i = 0; i < SUPERBLK_SIZE; i++){
		re_list[i].t_table = (T_table *)malloc(sizeof(T_table) * ppb);
		re_list[i].value   = (value_set *)malloc(sizeof(value_set) * ppb);
		memset(re_list[i].t_table, 0, sizeof(T_table) * ppb);
		re_list[i].size = 0;
	}
	*/
#endif

	printf("---- BloomFTL (SINGLE) ----\n");
	printf("Storage total Logical pages        : %d\n",lnp);
	printf("Storage total Physical pages       : %d\n",nop);
	printf("Storage total Superblocks          : %d\n",nos);
	printf("Storage total Physical blocks      : %d\n",nob);
	printf("Storage Data page per Singleblock  : %d\n",ppb);
	printf("Storage Data page per Superblock   : %d\n",mask);
	printf("Storage OP   page per Superblock   : %d\n",pps - mask);
	printf("Storage Bloomfilter per Superblock : %d\n",bf->base_s_bytes);
#if REBLOOM
	printf("Storage Rebloom trigger count      : %d\n",r_check);
#endif

	printf("Out-of-Range : %ld\n",OOR);
	//Set BF table for physical blocks
	for(int i = 0 ; i < nos; i++){
		b_table[i].bf_arr = (uint8_t *)malloc(sizeof(uint8_t) * bf->base_s_bytes);	
		memset(b_table[i].bf_arr, 0, sizeof(uint8_t) * bf->base_s_bytes);
		b_table[i].bf_num = 0;	
		b_table[i].b_bucket = (Block **)malloc(sizeof(Block *) * SUPERBLK_SIZE);
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

	bm = BM_Init(nob, ppb, 0, 0);

	for(int i = 0 ; i < nos; i++){
		for(int j = 0; j < SUPERBLK_SIZE; j++){
			b_table[i].b_bucket[j] = &bm->barray[i*SUPERBLK_SIZE+j];
			if(b_table[i].b_bucket[j] == NULL)
				printf("NULL ERROR!\n");
		}
	}

	/*		
	for(int i = 0 ; i < nos; i++){
		printf("Superblk[%d] : ", i);
		for(int j = 0; j < SUPERBLK_SIZE; j++){
			printf("%d ",b_table[i].b_bucket[j]->PBA);
		}
		printf("\n");
	}
	exit(0);
	*/
	return 1;

}
#endif
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
	printf("Total sub lookup I/O count : %d\n",sub_lookup_read);
	printf("Total RB write I/O count   : %d\n",rb_write_cnt);
	printf("Total RB read  I/O count   : %d\n",rb_read_cnt);
#endif
	printf("Total block erase count    : %d\n",block_erase_cnt);
	if(algo_read_cnt != 0){
		printf("Total RAF Result       : %.2lf\n", (float) (found_cnt+not_found_cnt) / algo_read_cnt);
	}
	if(algo_write_cnt != 0){
#if REBLOOM	
		printf("Total WAF Result       : %.2lf\n", (float) (algo_write_cnt+gc_write+rb_write_cnt)/algo_write_cnt);
#else
		printf("Total WAF Result       : %.2lf\n", (float) (algo_write_cnt+gc_write) / algo_write_cnt);
#endif
	}

	//Table free
	bf_free(bf);
#if SUPERBLK_GC
	for(int i = 0 ; i < nob; i++){
#else
	for(int i = 0 ; i < nos; i++){
#endif
		bf_memory   += (b_table[i].bf_num * 12) / 8; // for BF
		flag_memory += (ppb*SUPERBLK_SIZE) / 8;
		max_memory  += (512*12)/8;

		free(b_table[i].bf_arr);
		free(sb[i].bf_page);
		free(sb[i].p_flag);
#if REBLOOM
#endif
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

#if !REBLOOM
	for(int i = 0 ; i < pps; i++){

		free(gm[i].t_table);
		gm[i].value = NULL;
		gm[i].size = 0;
	}
/*
#if REBLOOM
		free(rb[i].t_table);
		free(rb[i].value);
#endif
*/
	}

#endif
/*
#if REBLOOM
	for(int i = 0 ; i < SUPERBLK_SIZE; i++){
		free(re_list[i].t_table);
		free(re_list[i].value);
	}
#endif
*/

	free(gm);
	free(valid_p);

	//block Free
	free(bloom_oob);
	BM_Free(bm);

	
	
	return ;
}



uint32_t bloom_write(request* const req){
	uint32_t ppa;
	uint32_t lba = req->key;
	algo_req *my_req;


	ppa = ppa_alloc(lba);

	my_req = assign_pseudo_req(DATAW, NULL, req);
	__bloomftl.li->write(ppa, PAGESIZE, req->value, ASYNC, my_req);


	BM_ValidatePage(bm, ppa);
	bloom_oob[ppa].lba = lba;


	algo_write_cnt++;

	return 1;


}

#if SUPERBLK_GC
uint32_t bloom_read(request* const req){
	Block *block;
	uint32_t superblk, ppa;
	uint32_t lba;
	int idx;
	register uint32_t hashkey;
	lba = req->key;

	read_cnt++; // Total read requests
	superblk = lba / mask;
	//superblk = (lba>>2) % nos;
	block = &bm->block[superblk];
	hashkey = hashing_key(lba);
	if(lba_flag[lba] == 0){
		return 0;
	}

	for(idx = block->p_offset-1; idx>0; idx--){
		if(get_bf(hashkey+idx, superblk, idx)){
			//If true, check oob
			if(block->page[idx].oob == lba){
				found_cnt++;
				ppa = (block->pba_idx * _PPB) + idx;
				break;
			}else{
				not_found_cnt++;
			}
		}
	}

	if(idx != 0)
		return ppa;
	else{
		if(block->page[idx].oob != lba){
			printf("block->page[%d].oob : %d\n",idx, block->page[idx].oob);
			printf("LBA : %d\n",lba);
			printf("This shuold not happen!!\n");
			exit(0);
		}
		//printf("LBA : %d\n",lba);
		found_cnt++;
		ppa = (block->pba_idx * _PPB) + idx;
	}

	return ppa;
}
#else
uint32_t bloom_read(request* const req){
	

	algo_req *my_req;
	uint32_t ppa;
	uint32_t lba = req->key;
	
	
	
	algo_read_cnt++;
	if(lba_flag[lba] == 0)
		return 0;	
	//printf("read_cnt : %d\n",read_cnt++);
	my_req = assign_pseudo_req(DATAR, NULL, req);
#if !REBLOOM
	if(!lba_bf[lba]){
		ppa = check_first(lba);
		__bloomftl.li->read(ppa, PAGESIZE, req->value, ASYNC, my_req);
		return 1;
	}
#endif

	ppa = table_lookup(lba);
	__bloomftl.li->read(ppa, PAGESIZE, req->value, ASYNC, my_req);
	
	return 1;
	
}
#endif



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


