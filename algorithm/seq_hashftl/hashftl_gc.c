#include "hashftl.h"


Block *hash_block_gc(uint32_t lba, int32_t virtual_idx, int32_t segment_idx){

	Block *victim;
	Block *s_block;
	Block *res;
	
	uint32_t ppa;
	volatile int idx = 0;
	bool select_gc = 0;
	int16_t p_idx;
	victim = g_table[virtual_idx].p_block;
	s_block = shared_block[segment_idx];
	uint32_t old_block;

	data_gc_poll = 0;

	SRAM *d_ram;
	value_set **temp_set;


	if(victim->p_offset != ppb){
		g_table[virtual_idx].share = 0;
		res = victim;
		return res;
	}else{
	
		d_ram = (SRAM *)malloc(sizeof(SRAM) * ppb);
		temp_set = (value_set **)malloc(sizeof(value_set *) * ppb);
		for(int i = 0 ; i < _PPB; i++){
			d_ram[i].oob_ram = -1;
			d_ram[i].ptr_ram = NULL;
		}

		if(victim->Invalid != 0){
			for(int i = 0 ; i < victim->p_offset; i++){
				ppa = (victim->PBA *ppb) + i;
				if(BM_IsValidPage(bm, ppa)){
					temp_set[idx] = SRAM_load(d_ram, ppa, idx, GCDR);
					d_ram[idx].oob_ram = hash_oob[ppa].lba;
					d_ram[idx].ptr_ram = temp_set[idx]->value;
					hash_oob[ppa].lba = -1;
					idx++;
				}
			}

			while(data_gc_poll != idx){};

			block_erase_cnt++;
			old_block = victim->PBA * ppb;
			__hashftl.li->trim_block(old_block, false);
			BM_InitializeBlock(bm, victim->PBA);
			//enqueue(bm->free_b, victim);
			//res = (Block *)dequeue(bm->free_b);
			

			res = reserved_b;
			reserved_b = victim;
			

			for(int i = 0 ; i < idx; i++){
				ppa = (res->PBA * _PPB) + res->p_offset;
				SRAM_unload(d_ram, ppa, i, GCDW);
				p_idx = res->p_offset;
				BM_ValidatePage(bm, ppa);
				hash_update_mapping(d_ram[i].oob_ram, p_idx);
				res->p_offset++;
			}

			

			if(res->p_offset == _PPB){
				printf("GC error 1\n");
				exit(0);
			}
			g_table[virtual_idx].share = 0;
			g_table[virtual_idx].p_block = res;
			
			for(int i = 0 ; i < idx; i++){
				inf_free_valueset(temp_set[i], FS_MALLOC_R);
			}
			free(d_ram);
			free(temp_set);

			return res;

		}
	
		if(s_block->Invalid != 0){
			for(int i = 0 ; i < s_block->p_offset; i++){
				ppa = (victim->PBA * ppb) + i;
				if(BM_IsValidPage(bm, ppa)){
					temp_set[idx] = SRAM_load(d_ram, ppa, idx, GCDR);
					d_ram[idx].oob_ram = hash_oob[ppa].lba;
					d_ram[idx].ptr_ram = temp_set[idx]->value;
					hash_oob[ppa].lba = -1;
					idx++;
				}
			}

			while(data_gc_poll != idx){};

			block_erase_cnt++;
			old_block = s_block->PBA * ppb;
			__hashftl.li->trim_block(old_block, false);
			BM_InitializeBlock(bm, s_block->PBA);
			//enqueue(bm->free_b, victim);
			//res = (Block *)dequeue(bm->free_b);


			res = reserved_b;
			reserved_b = s_block;


			for(int i = 0 ; i < idx; i++){
				ppa = (res->PBA * ppb) + res->p_offset;
				SRAM_unload(d_ram, ppa, i, GCDW);
				p_idx = res->p_offset;
				BM_ValidatePage(bm, ppa);
				hash_update_mapping(d_ram[i].oob_ram, p_idx);
				res->p_offset++;
			}



			if(res->p_offset == ppb){
				printf("GC error 1\n");
				exit(0);
			}
			g_table[virtual_idx].share = 1;
			g_table[virtual_idx].p_block = res;

			for(int i = 0 ; i < idx; i++){
				inf_free_valueset(temp_set[i], FS_MALLOC_R);
			}
			free(d_ram);
			free(temp_set);

			return res;

		}

	}
}



