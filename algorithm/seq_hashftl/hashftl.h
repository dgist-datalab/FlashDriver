#ifndef __SEQ_HASHFTL_H_
#define __SEQ_HASHFTL_H_


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include "../../interface/interface.h"
#include "../../interface/queue.h"
#include "../../include/types.h"
#include "../../include/container.h"
#include "../../include/types.h"
#include "../../include/dl_sync.h"
#include "../../include/ftl_settings.h"
#include "../blockmanager/BM.h"

#include "../Lsmtree/skiplist.h"


#define H_BIT 2
#define NUM_BUCKET (1 << H_BIT)
#define TYPE uint8_t

#define LEFTROTATE(x, c) (((x) << (c)) | ((x) >> (32 - (c))))



/* Index table for LBAs
 * Each entry has a index (offset) of physical block
 * share = 0 --> Primary table, share = 1 --> Shared block (Secondary block)
 */
typedef struct {
	int16_t ppid;
	bool share;
}h_table;

/* Global block table for virtual to physical block */
/* Share variable is same the Index table share variable */
/* This table size is so small */
typedef struct {
	Block *p_block;
	bool share;
}v_table;

typedef struct {
	int32_t lba;
}H_OOB;

/* When GC, data structure for valid copy pages */
typedef struct SRAM{
	int32_t oob_ram;
	PTR ptr_ram;
}SRAM;

typedef struct hash_params{
	value_set *value;
	dl_sync hash_mutex;
	TYPE type;
}hash_params;

// this data structure uses in write buffer 
struct prefetch_struct {
    uint32_t ppa;
    snode *sn;
};


extern algorithm __hashftl;


//hashftl.c
extern BM_T *bm;
extern Block **shared_block;
extern Block *reserved_b;	//Reserved block pointer (This uses in GC)
extern H_OOB *hash_oob;

extern h_table *p_table;	 //Index table, called primary table
extern v_table *g_table;	 //Global block table
extern int32_t algo_write_cnt;
extern int32_t algo_read_cnt;
extern int32_t gc_write;
extern int32_t gc_read;
extern int32_t block_erase_cnt;
extern int32_t not_found_cnt;
extern uint32_t lnb;
extern uint32_t lnp;
extern int num_op_blocks;
extern volatile int data_gc_poll;

extern int nob;
extern int ppb;
extern int nop;


uint32_t hash_create(lower_info *, algorithm *);
void hash_destroy(lower_info *, algorithm *);
uint32_t hash_read(request *const);
uint32_t hash_write(request *const);
void *hash_end_req(algo_req*);

//hashftl_util.c
algo_req* assign_pseudo_req(TYPE, value_set*, request*);
value_set *SRAM_load(SRAM *sram, uint32_t ppa, int idx, TYPE type);
void SRAM_unload(SRAM *sram, uint32_t ppa, int idx, TYPE type);



uint32_t ppa_alloc(uint32_t);
uint64_t get_hashing(uint32_t);

/* This is a function which decides to set primary block or shared block */
bool check_block(int32_t virtual_idx);
/* This is a function that set invalid & index table */
int32_t check_mapping(uint32_t lba, int32_t virtual_idx, int32_t second_idx);
/* When GC, function to update index table */
void hash_update_mapping(uint32_t lba, int16_t p_idx);

//hashftl_gc.c
Block *hash_block_gc(uint32_t lba, int32_t virtual_idx, int32_t second_idx);


//md5.c
void md5(uint32_t *, size_t, uint64_t *);
uint32_t fibo_hash(uint32_t);

#endif
