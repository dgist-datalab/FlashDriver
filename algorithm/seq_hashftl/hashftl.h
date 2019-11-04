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




typedef struct {
	int16_t ppid;
	int8_t share;
}h_table;

typedef struct {
	Block *p_block;
	int32_t segment_idx;	
	int8_t share;
}v_table;

typedef struct {
	int32_t lba;
}H_OOB;


typedef struct SRAM{
	int32_t oob_ram;
	PTR ptr_ram;
}SRAM;

typedef struct hash_params{
	value_set *value;
	dl_sync hash_mutex;
	TYPE type;
}hash_params;


struct prefetch_struct {
    uint32_t ppa;
    snode *sn;
};


extern algorithm __hashftl;


//hashftl.c
extern BM_T *bm;
extern Block **shared_block;
extern Block *reserved_b;
extern H_OOB *hash_oob;

extern h_table *p_table;
extern v_table *g_table;
extern int32_t write_cnt;
extern int32_t read_cnt;
extern int32_t gc_write;
extern int32_t gc_read;
extern int32_t block_erase_cnt;
extern int32_t not_found_cnt;
extern uint32_t lnb;
extern uint32_t lnp;
extern int num_op_blocks;
extern int blocks_per_segment;
extern int max_segment;
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
bool check_block(int32_t virtual_idx);
int32_t check_mapping(uint32_t lba, int32_t virtual_idx, int32_t segment_idx);
void hash_update_mapping(uint32_t lba, int16_t p_idx);

//hashftl_gc.c
Block *hash_block_gc(uint32_t lba, int32_t virtual_idx, int32_t segment_idx);


//md5.c
uint32_t j_hashing(uint32_t, uint32_t);
void md5(uint32_t *, size_t, uint64_t *);
uint32_t b_hash(uint32_t);
uint32_t fibo_hash(uint32_t);

#endif
