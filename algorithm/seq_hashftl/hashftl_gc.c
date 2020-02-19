#include "hashftl.h"


Block *hash_block_gc(uint32_t lba, int32_t virtual_idx, int32_t second_idx){

	Block *victim;
	Block *p_block, *s_block;
	Block *res;

	uint32_t ppa;
	volatile int idx = 0;
	int16_t p_idx;
	uint32_t old_block;

	data_gc_poll = 0;

	SRAM *d_ram;
	value_set **temp_set;
	p_block = g_table[virtual_idx].p_block;
	s_block = shared_block[second_idx];


	if(p_block->p_offset != ppb){
		g_table[virtual_idx].share = 0;
		res = p_block;
		return res;
	}else{

		d_ram = (SRAM *)malloc(sizeof(SRAM) * ppb);
		temp_set = (value_set **)malloc(sizeof(value_set *) * ppb);
		for(int i = 0 ; i < _PPB; i++){
			d_ram[i].oob_ram = -1;
			d_ram[i].ptr_ram = NULL;
		}

		/* Select victim, if primary block has invalid page, victim is primary block
		 * Otherwise, victim is secondary block (shared block)
		 */

		if(p_block->Invalid != 0) victim = p_block;
		else victim = s_block;	

		for(int i = 0 ; i < victim->p_offset; i++){
			ppa = (victim->PBA * ppb) + i;
			if(BM_IsValidPage(bm, ppa)){
				temp_set[idx] = SRAM_load(d_ram, ppa, idx, GCDR);
				d_ram[idx].oob_ram = hash_oob[ppa].lba;
				d_ram[idx].ptr_ram = temp_set[idx]->value;
				idx++;
			}
		}

		while(data_gc_poll != idx){};

		block_erase_cnt++;
		old_block = victim->PBA * ppb;
		__hashftl.li->trim_block(old_block, false);
		BM_InitializeBlock(bm, victim->PBA);


		res = reserved_b;
		reserved_b = victim;
		for(int i = 0 ; i < idx; i++){
			ppa = (res->PBA * ppb) + res->p_offset;
			SRAM_unload(d_ram, ppa, i, GCDW);
			p_idx = res->p_offset;
			BM_ValidatePage(bm, ppa);
			hash_update_mapping(d_ram[i].oob_ram, p_idx);
			res->p_offset++;
		}

		/* If victim is primary block, share flag = 0
		 * Otherwise, share flag = 1
		 * This means that a request is mapped to primary block or shared block
		 */
		if(victim == p_block){
			g_table[virtual_idx].share = 0;
			g_table[virtual_idx].p_block = res;
		}else{
			g_table[virtual_idx].share = 1;
			shared_block[second_idx] = res;
		}


		for(int i = 0 ; i < idx; i++){
			inf_free_valueset(temp_set[i], FS_MALLOC_R);
		}
		free(d_ram);
		free(temp_set);

		return res;


	}
}



