#include "bloomftl.h"

void all_ppa_flag(void){
	uint32_t superblk;
	uint32_t p_idx;

	for(int i = 0; i < nob; i++){
		for(int j = 0; j < ppb; j++){
			superblk = ((i*ppb) + j) / pps;
			p_idx = ((i*ppb) + j) % pps;
			printf("sb[%d].p_flag[%d] = %d\n",superblk,p_idx,sb[superblk].p_flag[p_idx]);
		}
	}
}


void single_ppa_flag(uint32_t superblk){
	Block *block;
	uint32_t s_p_idx, p_idx;
	uint32_t ppa;
	for(int i = 0 ; i < SUPERBLK_SIZE; i++){
		block = b_table[superblk].b_bucket[i];
		for(int j = 0; j < ppb; j++){
			p_idx = ((i*ppb) + j) % pps;
			s_p_idx = ((block->PBA * ppb) + j) % pps;
			ppa = (block->PBA * ppb) + j;
			printf("block[%d][%d][%d].oob : %d\n",s_p_idx, sb[superblk].p_flag[p_idx],BM_IsValidPage(bm,ppa), bloom_oob[ppa].lba);
		}
	}
}
