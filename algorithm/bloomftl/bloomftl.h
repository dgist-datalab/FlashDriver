#ifndef __BLOOMFTL_H__
#define __BLOOMFTL_H_

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

#ifdef W_BUFF
#include "../Lsmtree/skiplist.h"
#endif


#define TYPE uint8_t

#define SYMMETRIC 1
#define PR_SUCCESS 0.9
#define SUPERBLK_SIZE 4
#define MAX_RB 2
#define S_BIT (MAX_RB/2)

#define SUPERBLK_GC 0
#define REBLOOM 1
#define OOR (RANGE+1)

/* bit pattern for BF lookup */

//BF_INFO for each physical page
typedef struct {
	uint32_t bf_bits;
	uint32_t s_bits;
}BF_INFO;


//Data structure for BF
typedef struct {
    uint32_t k;
    uint32_t m;
    uint64_t targetsize;
    int n;
    float p;
    char* body;
    uint64_t start;
	BF_INFO *base_bf;
	uint32_t base_s_bits;
	uint32_t base_s_bytes;
} BF;


//Physical BF table
typedef struct {
	uint8_t *bf_arr;
	uint32_t bf_num;
#if !SUPERBLK_GC
	Block **b_bucket;
	uint32_t c_block;
	int32_t full;
#endif
#if REBLOOM
	uint32_t pre_lba;
	uint32_t rb_cnt;
#endif
	bool first;
}BF_TABLE;


typedef struct {
	int32_t s_idx;
}Index;

typedef struct {
	Index *bf_page;
	bool *p_flag;			//If allocate BF on pyhsical page, set 1.
	uint32_t num_s_bits;	//Num of total symbol bits (This is a total bits for Physical(Superblock) block)
	uint32_t num_s_bytes;	//Num of total bytes converted total symbol bits into bytes
}SBmanager;


typedef struct{
	int32_t lba;
}B_OOB;

//Global struct for GC & Reblooming
typedef struct {
	int32_t lba;
	int32_t ppa;
	int32_t weight;
	int32_t b_idx;
}T_table;

typedef struct SRAM{
	int32_t oob_ram;
	PTR ptr_ram;
}SRAM;

typedef struct {	
	T_table *t_table;
	value_set *value;
	uint32_t size;
}G_manager;

typedef struct bloom_params{
	value_set *value;
	dl_sync bloom_mutex;
	TYPE type;
}bloom_params;

struct prefetch_struct {
	uint32_t ppa;
	snode *sn;
};


extern algorithm __bloomftl;


extern BF *bf;
extern BM_T *bm;
extern SBmanager *sb;
extern G_manager *gm;
extern G_manager *valid_p;
extern G_manager *write_p;

#if REBLOOM
extern G_manager *rb;
extern G_manager *re_list;

#endif
extern BF_TABLE *b_table;
extern bool *lba_flag;
extern bool *lba_bf;
extern B_OOB *bloom_oob;



extern bool check_flag;
extern int32_t check_cnt;


extern uint32_t algo_write_cnt;
extern uint32_t algo_read_cnt;
extern uint32_t gc_write;
extern uint32_t gc_read;

extern volatile int32_t data_gc_poll;

extern uint32_t block_erase_cnt;
extern uint32_t not_found_cnt;
extern uint32_t found_cnt;
extern uint32_t rb_read_cnt;
extern uint32_t rb_write_cnt;
extern uint32_t remove_read;
extern uint32_t sub_lookup_read;


extern uint32_t lnb;
extern uint32_t mask;

extern int32_t nob;
extern int32_t ppb;
extern int32_t nop;
extern int32_t lnp;
#if !SUPERBLK_GC
extern int32_t nos;
extern int32_t pps;
extern uint32_t r_count;

extern bool rb_flag;
extern bool gc_flag;
#endif
#if REBLOOM
extern uint32_t r_check;
#endif
extern uint32_t g_cnt;

//bloomftl_util.c

int lba_compare(const void *, const void *);
int gc_compare(const void *, const void *);
uint32_t ppa_alloc(uint32_t);

void set_bf(uint32_t, uint32_t, uint32_t);
void reset_cur_idx(uint32_t);

uint32_t table_lookup(uint32_t, bool);
int64_t bf_lookup(uint32_t, uint32_t, uint8_t, bool);
uint32_t check_first(uint32_t);


algo_req* assign_pseudo_req(TYPE, value_set *, request *);
value_set* SRAM_load(int64_t, int ,TYPE);
void SRAM_unload(SRAM *, int64_t, int, TYPE);


#if !SUPERBLK_GC
uint32_t get_cur_block(uint32_t);
uint32_t set_bf_table(uint32_t, uint32_t, uint32_t);
#endif

void reset_bf_table(uint32_t);
//debugging.c
void all_ppa_flag(void);
void single_ppa_flag(uint32_t);

//bloomftl.c
uint32_t bloom_create(lower_info *, algorithm *);
void bloom_destroy(lower_info *, algorithm *);
uint32_t bloom_write(request *const);
uint32_t bloom_read(request *const);
uint32_t hash_remove(request *const);
void *bloom_end_req(algo_req*);

//bloomfilter.c
BF *bf_init(int entry, int ppb);
void bf_free(BF *);
uint32_t bf_bytes(uint32_t);

//gc.c
uint32_t bloom_gc(uint32_t);
int invalid_block(Block **, int, uint32_t);

#if REBLOOM
//rebloom.c
void rebloom_op(uint32_t);
uint32_t rebloom_gc(SRAM *, int32_t *, int32_t *, uint32_t, uint32_t);
uint32_t set_rebloom_list(int32_t);
uint32_t check_rb_valid(SRAM *, int32_t);
#endif
//Hash functions
static inline uint32_t hashfunction(uint32_t key) {
    key ^= key >> 15;
    key *= 2246822519U;
    key ^= key >> 13;
    key *= 3266489917U;
    key ^= key >> 16;

    return key;
}
static inline uint32_t hashing_key(uint32_t key) {
    return (uint32_t)((0.618033887 * key) * 1024);
}

static inline bool get_bf(uint32_t hashed_key, uint32_t pbn, uint32_t p_idx) {
    uint32_t bf_bits, h;

    int start = sb[pbn].bf_page[p_idx].s_idx;

//	printf("start : %d\n",start);
	int length = bf->base_bf[p_idx].s_bits;
    int end_byte = (start*length + length - 1) / 8;
    int end_bit = (start*length + length - 1) % 8;
    int symb_arr_sz = end_byte - ((start*length) / 8) + 1;
    uint8_t chunk_sz = length > end_bit + 1 ? end_bit + 1 : length;
    bf_bits = bf->base_bf[p_idx].bf_bits;
    h = hashfunction(hashed_key) % bf_bits;

    // 1
    if(end_bit == 7) {
        if(((h & ((1 << chunk_sz) - 1)) ^ (b_table[pbn].bf_arr[end_byte] >> (8 - chunk_sz)))) {
            goto not_exist;
        }
    }
    else{
        if((h ^ b_table[pbn].bf_arr[end_byte]) & ((1 << chunk_sz) - 1)){
            goto not_exist;
        }
    }

    if(symb_arr_sz == 1) {
        goto exist;
    }

    end_byte--;
    h >>= chunk_sz;
    length -= chunk_sz;
    chunk_sz = length > 8 ? 8 : length;

    // 2
    if((h & ((1 << chunk_sz) - 1)) ^ (b_table[pbn].bf_arr[end_byte] >> (8 - chunk_sz))){
        goto not_exist;
    }

    if(symb_arr_sz == 2) {
        goto exist;
    }

    end_byte--;
    h >>= chunk_sz;
    length -= chunk_sz;
    chunk_sz = length > 8 ? 8 : length;

    // 3
    if((h & ((1 << chunk_sz) - 1)) ^ (b_table[pbn].bf_arr[end_byte] >> (8 - chunk_sz))){
        goto not_exist;
    }

exist:
    return true;

not_exist:
    return false;
}

#endif
