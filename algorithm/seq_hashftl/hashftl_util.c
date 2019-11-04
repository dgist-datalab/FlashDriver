#include "hashftl.h"
algo_req* assign_pseudo_req(TYPE type, value_set *temp_v, request *req){
    algo_req *res = (algo_req *)malloc(sizeof(algo_req));
    hash_params *params = (hash_params *)malloc(sizeof(hash_params));

    res->parents  = req;
    res->type     = type;
    params->type  = type;
    params->value = temp_v;

    /*          
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
    */
    res->end_req = hash_end_req;
    res->params  = (void *)params;
    return res;
}
value_set *SRAM_load(SRAM *sram, uint32_t ppa, int idx, TYPE type){
	value_set *temp_value;
	temp_value = inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
	__hashftl.li->read(ppa, PAGESIZE, temp_value, ASYNC, assign_pseudo_req(type, NULL, NULL));
	return temp_value;
}
void SRAM_unload(SRAM *sram, uint32_t ppa, int idx, TYPE type){
	value_set *temp_value;
	temp_value = inf_get_valueset((PTR)sram[idx].ptr_ram, FS_MALLOC_W, PAGESIZE);
	__hashftl.li->write(ppa, PAGESIZE, temp_value, ASYNC, assign_pseudo_req(type, temp_value, NULL));
	hash_oob[ppa].lba = sram[idx].oob_ram;
	return ;
}

uint32_t ppa_alloc(uint32_t lba){

	int32_t ppa;
	int32_t virtual_idx;
	int32_t segment_idx;
	uint64_t h;
	bool flag;

	Block *block;

	h = get_hashing(lba);

	virtual_idx = h % lnb;
	segment_idx = h % num_op_blocks;
//	segment_idx = virtual_idx / blocks_per_segment;
	flag = check_block(virtual_idx);
	if(!flag){
		block = g_table[virtual_idx].p_block;
		ppa = (block->PBA * ppb) + block->p_offset++;
		g_table[virtual_idx].share = 0;	
	}else{
		block = shared_block[segment_idx];
		if(block->p_offset == ppb){
			printf("GC trigger!\n");
			block = hash_block_gc(lba, virtual_idx, segment_idx);	
			ppa = (block->PBA * ppb) + block->p_offset++;
		}else{
			ppa = (block->PBA * ppb) + block->p_offset++;
			g_table[virtual_idx].share = 1;		
		}
	}

	check_mapping(lba, virtual_idx,segment_idx);	
	p_table[lba].share = g_table[virtual_idx].share;
	return ppa;

}

uint64_t get_hashing(uint32_t lba){

	int32_t res;
	int32_t segment_idx;
	uint32_t lba_md5 = lba;
	size_t len = sizeof(lba_md5);
	uint64_t res_md5;

	md5(&lba_md5, len, &res_md5);	
//	res_md5 = j_hashing(lba_md5,0);

//	res_md5 = b_hash(lba_md5);	
//	res_md5 = fibo_hash(res_md5);
	return res_md5;
}


bool check_block(int32_t virtual_idx){
	Block *checker = g_table[virtual_idx].p_block;


	//If block offset is empty, return ppa;
	if(checker->p_offset == ppb){
		return 1;
	}

	return 0;

}

int32_t check_mapping(uint32_t lba, int32_t virtual_idx, int32_t segment_idx){
	int16_t ppid;
	
	int8_t share;
	uint32_t ppa;
	ppid = p_table[lba].ppid;
	share = p_table[lba].share;
	Block *block;


	if(ppid != -1){
		if(share == 0){
			block = g_table[virtual_idx].p_block;
		}else if(share == 1){
			block = shared_block[segment_idx];
		}
		ppa = (block->PBA * ppb) + ppid;
		hash_oob[ppa].lba = -1;
		BM_InvalidatePage(bm, ppa);
	}

	return 1;
}

void hash_update_mapping(uint32_t lba, int16_t p_idx){
	p_table[lba].ppid = p_idx;
	return ;
}
