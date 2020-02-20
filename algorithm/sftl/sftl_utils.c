#include "dftl.h"


int32_t head_init(C_TABLE *c_table, int32_t ppa)
{
	struct head_node *now;
	if(c_table->head == NULL)
	{
		now = (struct head_node *)malloc(sizeof(struct head_node));
		now->head_ppa = ppa;
		now->next = NULL;
		c_table->head = c_table->tail = now;
		return 1;
	}

	return 0;

}

struct head_node* head_push(C_TABLE *c_table, struct head_node *find_node, int32_t ppa)
{
	struct head_node *now;
	now = (struct head_node *)malloc(sizeof(struct head_node));
	now->head_ppa = ppa;
	now->next = find_node->next;	
	find_node->next = now;

	if(find_node == c_table->tail){
		c_table->tail = now;
	}
	return now;

}

int32_t head_tail_push(C_TABLE *c_table, int32_t ppa)
{
	struct head_node *now;

	now = (struct head_node *)malloc(sizeof(struct head_node));
	now->head_ppa = ppa;
	now->next = NULL;


	if(c_table->head == NULL){
		c_table->head = c_table->tail = now;
		return 0;	
	}
	else{
		c_table->tail->next = now;
		c_table->tail = now;
	}

	return 1;

}

struct head_node* head_free(C_TABLE *c_table, struct head_node *pre_node)
{
	struct head_node *next_node = pre_node->next;
	struct head_node *tmp = c_table->head;

	if(next_node == c_table->tail){
		c_table->tail = pre_node;	
	}

	pre_node->next = next_node->next;


	free(next_node);
	return pre_node;


}
int32_t head_bit_set(int32_t t_index)
{
	C_TABLE *c_table = &CMT[t_index];
	D_TABLE *p_table = mem_arr[t_index].mem_p;

	int32_t head_ppa = p_table[0].ppa;
	int32_t next_ppa;
	int32_t idx = 1;	
	int32_t b_form_size = 0;
	int32_t cnt = 0;
	c_table->bitmap[0] = 1;

	b_form_size += ENTRY_SIZE;
	cnt++;
	for(int i = 0 ; i < EPP-1; i++){
		head_ppa = p_table[i].ppa;
		next_ppa = p_table[i+1].ppa;
		if(next_ppa == -1){
			c_table->bitmap[i+1] = 0;
			continue;
		}
		if(next_ppa == head_ppa + 1){
			c_table->bitmap[i+1] = 0;
		}else{
			cnt++;
			c_table->bitmap[i+1] = 1;
			b_form_size += ENTRY_SIZE;
		}
		head_ppa = next_ppa;
	}
	c_table->bit_cnt = cnt;
	b_form_size += BITMAP_SIZE;
	return b_form_size;



}


int32_t head_list_set(int32_t lpa){
	C_TABLE *c_table = &CMT[D_IDX];
	D_TABLE *p_table = c_table->p_table;
	int32_t head_ppa;
	for(int i = 0 ; i < EPP; i++){
		if(c_table->bitmap[i] == 1){	
			head_ppa = p_table[i].ppa;
			head_tail_push(c_table, head_ppa);	
		}
	}
	return 1;

}
struct head_node* sftl_pre_find(C_TABLE *c_table, int32_t offset){
	struct head_node *now = c_table->head;
	int i;
	for(i = 1 ; i < offset ; i++){
		if(c_table->bitmap[i] == 1){
			now = now->next;
		}
	}
	return now;
}


int32_t sftl_entry_set(int32_t lpa)
{
	C_TABLE *c_table = &CMT[D_IDX];
	D_TABLE *p_table = c_table->p_table;

	struct head_node *tmp = NULL;
	struct head_node *next_node = NULL;
	int32_t offset = P_IDX;					  //Offset of translation page
	int32_t head_entry = p_table[offset].ppa; //Current set ppa for a LBA
	int32_t next_ppa;
	int32_t pre_ppa;
	int32_t idx = 1;


	/* If entry offset of translation page is first */
	if(offset == 0){
		tmp = c_table->head;
		tmp->head_ppa = head_entry;
		next_ppa = p_table[offset+idx].ppa;
		
		/* If a next PPA is not set, return -1 */
		if(next_ppa == -1)
			return -1;
		
		/* next PPA and current PPA is not sequential, you have to add head entry */
		if(next_ppa != head_entry + idx){
			if(c_table->bitmap[offset+idx] == 0){
				c_table->bitmap[offset+idx] = 1;
				c_table->bit_cnt++;
				c_table->b_form_size += ENTRY_SIZE;
				head_push(c_table,tmp,next_ppa);
			}

		}
		return offset;
	}
	/* If entry offset of translation page is last */
	if(offset == EPP-1){
		pre_ppa = p_table[offset-idx].ppa;
		
		/* If previous PPA is not set, set entry for current request */
		if(pre_ppa == -1){
			/* If a entry set already, just update entry for current request
			 * Otherwise, add entry in linked-list
			 */
			if(c_table->bitmap[offset] == 1){
				c_table->tail->head_ppa = head_entry;
			}else{
				c_table->bitmap[offset] = 1;
				c_table->b_form_size += ENTRY_SIZE;
				c_table->bit_cnt++;
				head_tail_push(c_table,head_entry);
			}
		/* If entry for previous PPA set already, you have to check sequentiality */
		}else{
			if(head_entry == pre_ppa + idx){
				if(c_table->bitmap[offset] == 1){
					tmp = sftl_pre_find(c_table, offset);
					head_free(c_table, tmp);
					c_table->bitmap[offset] = 0;
					c_table->bit_cnt--;
					c_table->b_form_size -= ENTRY_SIZE;
				}
			}else{
				if(c_table->bitmap[offset] == 1){
					c_table->tail->head_ppa = head_entry;
				}else{
					c_table->bitmap[offset] = 1;
					c_table->bit_cnt++;
					c_table->b_form_size += ENTRY_SIZE;
					head_tail_push(c_table, head_entry);
				}
			}
		}
	}
	/* If entry offset of translation page is middle (e.g., 0 < offset < 2048) 
	 * you have to check sequentiality for previous PPA and next PPA
	 * If entry for previous PPA and next PPA is set, you have to update entry
	 */
	else{
		pre_ppa  = p_table[offset-idx].ppa;
		next_ppa = p_table[offset+idx].ppa;
		tmp = sftl_pre_find(c_table, offset);
		/* Check entry for previous PPA */
		if(pre_ppa == -1){
			if(c_table->bitmap[offset] == 1){	
				tmp = tmp->next;
				tmp->head_ppa = head_entry;
			}else{
				c_table->bitmap[offset] = 1;
				c_table->bit_cnt++;
				c_table->b_form_size += ENTRY_SIZE;
				tmp = head_push(c_table,tmp, head_entry);

			}
		}else{
			if(head_entry == pre_ppa + idx){
				if(c_table->bitmap[offset] == 1){

					tmp = head_free(c_table, tmp);
					c_table->bitmap[offset] = 0;
					c_table->bit_cnt--;
					c_table->b_form_size -= ENTRY_SIZE;

				}

			}else{
				if(c_table->bitmap[offset] == 1){
					tmp = tmp->next;
					tmp->head_ppa = head_entry;

				}else{
					c_table->bitmap[offset] = 1;
					c_table->bit_cnt++;
					c_table->b_form_size += ENTRY_SIZE;
					tmp = head_push(c_table,tmp,head_entry);

				}
			}
		}

		/* Check entry for Next PPA */
		if(next_ppa == -1) return -1;

		if(next_ppa == head_entry+idx){
			if(c_table->bitmap[offset+idx] == 1){
				head_free(c_table,tmp);
				c_table->bitmap[offset+idx] = 0;
				c_table->bit_cnt--;
				c_table->b_form_size -= ENTRY_SIZE;
			}
		}else{
			if(c_table->bitmap[offset+idx] == 1){
				tmp = tmp->next;
				tmp->head_ppa = next_ppa;
			}else{
				c_table->bitmap[offset+idx] = 1;
				c_table->bit_cnt++;
				c_table->b_form_size += ENTRY_SIZE;
				head_push(c_table,tmp,next_ppa);
			}
		}	


	}
	return offset;
}

int32_t sftl_entry_free(C_TABLE *evic_ptr)
{
	struct head_node *p_node = NULL;
	if(evic_ptr->head == NULL)
		return 0;

	while(evic_ptr->head != NULL)
	{
		p_node = evic_ptr->head;
		evic_ptr->head = evic_ptr->head->next;
		free(p_node);
	}
	evic_ptr->tail = evic_ptr->head;

	return 1;



}

int32_t sftl_bitmap_size(int32_t lpa)
{
	C_TABLE *c_table = &CMT[D_IDX];
	int32_t bitmap_form_size = 0;
	for(int i = 0 ; i < EPP; i++)
	{
		if(c_table->bitmap[i])
		{
			bitmap_form_size += ENTRY_SIZE;
		}
	}
	bitmap_form_size += BITMAP_SIZE;
	return bitmap_form_size;
}
int32_t get_mapped_ppa(int32_t lpa)
{
	C_TABLE *c_table = &CMT[D_IDX];
	struct head_node *now = c_table->head;
	int32_t offset = P_IDX;
	int32_t head_lpn = -1;
	int32_t head_ppn = -1;
	int32_t idx;
	int32_t ppa;

	int32_t i = 0;
	if(c_table->bit_cnt != 1){ 
		for(i = offset ; i > 0; i--){
			if(c_table->bitmap[i] == 1){
				now = now->next;
				if(head_lpn == -1){
					head_lpn = i;
				}
			}
		}
		if(now == c_table->head){
			head_lpn = 0;
		}
		idx = offset - head_lpn;
		head_ppn = now->head_ppa;
		ppa = head_ppn + idx;
	}
	else{
		now = c_table->head;
		head_ppn = c_table->head->head_ppa;
		ppa = head_ppn + offset;
	}
	return ppa;

}

int32_t cache_mapped_size()
{
	int32_t idx = max_cache_entry;
	int32_t cache_size = 0;
	int32_t cnt = 0;
	int32_t miss_cnt = 0;
	C_TABLE *c_table;
	D_TABLE *p_table;
	for(int i = 0; i < idx; i++)
	{
		c_table = &CMT[i];
		p_table = c_table->p_table;
		if(p_table)
		{
			printf("b_form_size[%d] = %d\n",i,c_table->b_form_size);
			cnt++;
		}else{
			miss_cnt++;
		}
	}
	cache_size = total_cache_size - free_cache_size;
	printf("not_caching : %d\n",miss_cnt);
	printf("num_caching : %d\n",cnt);
	return cache_size;


}




