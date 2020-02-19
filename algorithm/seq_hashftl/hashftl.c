#include "hashftl.h"

algorithm __hashftl = {
	.create  = hash_create,
	.destroy = hash_destroy,
	.read	 = hash_read,
	.write   = hash_write,
	.remove  = NULL
};


BM_T *bm;
Block *reserved_b;
H_OOB *hash_oob;

Block **shared_block;

h_table *p_table;
v_table *g_table;

int32_t algo_write_cnt;
int32_t algo_read_cnt;
int32_t gc_write;
int32_t gc_read;
int32_t block_erase_cnt;
int32_t not_found_cnt;

uint32_t lnb;
uint32_t lnp;
int num_op_blocks;
volatile int data_gc_poll;

#if W_BUFF
skiplist *write_buffer;
uint64_t max_write_buf;
uint32_t buf_idx;
struct prefetch_struct *prefetcher;
#endif

int nob;
int ppb;
int nop;





uint32_t hash_create(lower_info *li, algorithm *algo){

	algo->li = li;


	//Set Physical pages and blocks
	nob = _NOB;
	ppb = _PPB;
	nop = _NOP;
	
	//Set Logical pages and blocks
	lnp = (GIGAUNIT*G) / PAGESIZE;
	lnb = (GIGAUNIT*G) / (ppb * PAGESIZE);


	printf("------------------- Hash-based FTL ------------------\n");

	//Set mapping table (Index table and block table)
	p_table = (h_table *)malloc(sizeof(h_table) * lnp);
	g_table = (v_table *)malloc(sizeof(v_table) * lnb);
	hash_oob = (H_OOB *)malloc(sizeof(H_OOB) * nop);
	
	for(int i = 0 ; i < lnb; i++){
		g_table[i].p_block = NULL;
		g_table[i].share = 0;
	}
	for(int i = 0 ; i < lnp; i++){
		p_table[i].ppid = -1;
		p_table[i].share = 0;
	}
	num_op_blocks = nob - lnb - 1;

	/* Set Global pointers for shared block
	 * When not empty primary block, mapped to shared block using hash 
	 */
	shared_block = (Block **)malloc(sizeof(Block *) * num_op_blocks);

#if W_BUFF
	write_buffer = skiplist_init();
	max_write_buf = 1024;
	prefetcher = (struct prefetch_struct *)malloc(sizeof(struct prefetch_struct) * max_write_buf);
	for(size_t i = 0 ; i < max_write_buf; i++){
		prefetcher[i].ppa = 0;
		prefetcher[i].sn = NULL;
	}
#endif

	
	printf("Total Num op blocks : %d\n", num_op_blocks);
	printf("Total logical num of pages  : %d\n",lnp);
	printf("Total logical num of blocks : %d\n",lnb); 
	printf("Total Physical num of blocks : %d\n",nob);
	printf("Page per block : %d\n",ppb);

	bm = BM_Init(nob, ppb, 0, 0);

	//Virtual to Physical block mapping
	for(int i = 0 ; i < lnb; i++){
		g_table[i].p_block = &bm->barray[i];
	}

	//Set shared blocks
	int idx=0;	
	for(int i = lnb; i < nob-1; i++){		
		shared_block[idx++] = &bm->barray[i];
	}
	reserved_b = &bm->barray[nob-1];
	
	
	return 1;

	

}


void hash_destroy(lower_info *li, algorithm *algo){
	double memory;
	printf("--------- Benchmark Result ---------\n\n");
	printf("Total request  I/O count : %d\n",algo_write_cnt+algo_read_cnt);
	printf("Total write    I/O count : %d\n",algo_write_cnt);
	printf("Total read     I/O count : %d\n",algo_read_cnt);
	printf("Total GC write I/O count : %d\n",gc_write);
	printf("Total GC read  I/O count : %d\n",gc_read);
	printf("Total Not found count    : %d\n",not_found_cnt);
	printf("Total erase count        : %d\n",block_erase_cnt);
	if(algo_write_cnt != 0)
		printf("Total WAF  : %.2lf\n",(float) (algo_write_cnt+gc_write) / algo_write_cnt);

	memory = (double) ((lnp * 10)/8 + (lnb * 4));
	printf("Mapping memory Requirement (MB) : %.2lf\n",memory/1024/1024);

	free(p_table);
	free(g_table);
	free(shared_block);

	free(hash_oob);
	BM_Free(bm);

	return ;
}

uint32_t hash_write(request* const req){

	Block *block;
	uint32_t pba, ppa;
	int16_t p_idx;
	uint32_t flush_lba;

	uint32_t lba = req->key;
	algo_req *my_req;
	snode *temp;
	static bool is_flush = false;

	if(write_buffer->size == max_write_buf){
		for(size_t i = 0 ; i < max_write_buf; i++){
			temp = prefetcher[i].sn;
			flush_lba = temp->key;
			ppa = ppa_alloc(flush_lba);			
			my_req = assign_pseudo_req(DATAW, temp->value, NULL);
			__hashftl.li->write(ppa, PAGESIZE, req->value, ASYNC, my_req);	
			p_idx = ppa % ppb;
			p_table[flush_lba].ppid = p_idx;
			BM_ValidatePage(bm, ppa);
			hash_oob[ppa].lba = flush_lba;
			
			temp->value = NULL;
		}
		for(size_t i = 0 ; i < max_write_buf; i++){
			prefetcher[i].ppa = 0;
			prefetcher[i].sn = NULL;
		}
		buf_idx = 0;
		skiplist_free(write_buffer);
		write_buffer = skiplist_init();

		__hashftl.li->lower_flying_req_wait();
		is_flush=true;
	}


	temp = skiplist_insert(write_buffer, lba, req->value, true);

	if(write_buffer->size == buf_idx+1){
		prefetcher[buf_idx++].sn = temp;
	}

	req->value = NULL;
	req->end_req(req);

	algo_write_cnt++;
	return 1;
}


uint32_t hash_read(request* const req){
	Block *block;
	int16_t p_idx;
	int32_t virtual_idx;
	int32_t second_idx;
	uint64_t h;

	uint32_t lba = req->key;
	algo_req *my_req;
	bool share = p_table[lba].share;
#if W_BUFF
	snode *temp;
#endif



	uint32_t check_ppa;

	h = get_hashing(lba);
	virtual_idx = h % lnb;
	second_idx = h % num_op_blocks;

	/* If share = 0, this means that it is mapped to primary block
	 * Otherwise, it is mapped to shared block 
	 */
	if(!share){
		block = g_table[virtual_idx].p_block;
	}else{
		block = shared_block[second_idx];
	}
	p_idx = p_table[lba].ppid;
	if((temp = skiplist_find(write_buffer, lba))){
		memcpy(req->value->value, temp->value->value, PAGESIZE);
		req->type_ftl = 0;
		req->type_lower = 0;
		req->end_req(req);
		return 1;
	}


	
	if(p_idx == -1){
		
		req->end_req(req);
		return 1;
	}
	

	my_req = assign_pseudo_req(DATAR, NULL, req);

	check_ppa = (block->PBA * ppb) + p_idx;

	//To check correct data, you have to check OOB
	if(hash_oob[check_ppa].lba != lba){	
		printf("oob : %d lba : %d\n",hash_oob[check_ppa].lba, lba);
		printf("Page offset : %d\n",p_idx);
		printf("ppa allocation error!\n");
		exit(0);
	}

	__hashftl.li->read(check_ppa, PAGESIZE, req->value, ASYNC, my_req);



	algo_read_cnt++;
	return 1;
}


void *hash_end_req(algo_req *input){
    hash_params *params = (hash_params *)input->params;
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

    }
    free(params);
    free(input);
    return NULL;
}















