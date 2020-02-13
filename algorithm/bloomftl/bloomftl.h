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
#include "../../interface/bb_checker.h"

#ifdef W_BUFF
#include "../Lsmtree/skiplist.h"
#endif


#define TYPE uint8_t

#define SYMMETRIC 1		
#define PR_SUCCESS 0.9	      //PR ratio (1-fpr)
#define SUPERBLK_SIZE 4	      //Block per Superblock
#define MAX_RB 4	      //In Reblooming, max num of entry
#define S_BIT (MAX_RB/2)      //log((logical page size)/(physical page size)) -> coalasing bit

#define HASH_MODE 1	       //Decide superblock by hash, if 0 -> decide superblock by shift operation
#define REBLOOM 1	       //Reblooming enable flag 
#define OOR (RANGE+1)	      //initial value for reblooming

/* bit pattern for BF lookup */

//BF_INFO for each physical page
typedef struct {
	uint32_t bf_bits;	//number of total Bloomfilter bits
	uint32_t s_bits;	//number of bits per entry
}BF_INFO;


//Data structure for BF, this structure is used for statistics such as memory usage
typedef struct {
	uint32_t k; //# of functions , this must be 1
	uint32_t m; //# of bits in a bloomfilter
	uint64_t targetsize; //not use
	int n; //# of entyr, this must be 1
	float p; //fpr
	char* body; //bits array
	//uint64_t start;
	BF_INFO *base_bf; 
	uint32_t base_s_bits;
	uint32_t base_s_bytes;
} BF;


//BF table
//oen bf table per one superblock

typedef struct {
	uint8_t *bf_arr;	//Array for set bloomfilter
	uint32_t bf_num;	//BF count of a superblock
	__block **b_bucket;	//Single block pointer within superblock
	uint32_t c_block;	//Use single block index
	int32_t full;		//Used physical page count
#if REBLOOM
	/*this two variables are used only reblooming, they can be removed*/
	//uint32_t pre_lba;	//Sequentiality check variable
	//uint32_t rb_cnt;	//Current sequentaility count in superblock
#endif
	//bool first;		//When no reblooming, use this variable
}BF_TABLE;


//typedef struct {
//	int32_t s_idx;
//}Index;

/*
 * (S table in humantech paper)
 * */
typedef struct {
	//Index *bf_page;		//A BF location for physical page ????????
	bool *p_flag;		//If allocate BF on pyhsical page, set 1.
	uint32_t num_s_bits;	//Num of total symbol bits (This is a total bits for Physical(Superblock) block)
	uint32_t num_s_bytes;	//Num of total bytes converted total symbol bits into bytes
}SBmanager;


typedef struct{
	int32_t lba;
}B_OOB;

/*************Global struct for GC & Reblooming*********/

/*
 * one per lpa
 * this is used at GC
 * temporal buffer for data in a superblock
 * */
typedef struct {
	int32_t lba; 
	int32_t ppa; 
	int32_t weight;// for checking latest data
	int32_t b_idx; // block index in superblock 
}T_table;

/*
 *	write buffer for a physical page in GC
 * */
typedef struct SRAM{
	int32_t oob_ram; //oob
	PTR ptr_ram; //data
}SRAM;



typedef struct {	
	// T_table *t_table; // change it not to use pointer
	value_set *value;
	//uint32_t size; //not used maybe
}G_manager;


typedef struct bloom_params{ //paramater of algo_req
	//value_set *value;  //not used maybe
	//dl_sync bloom_mutex;
	TYPE type;
}bloom_params;

/* maybe not used
struct prefetch_struct {
	uint32_t ppa;
	snode *sn;
};
*/

extern algorithm __bloomftl;
extern struct blockmanager *bloom_bm;
extern __segment **g_seg;


extern BF *bf;	
extern SBmanager *sb;
extern G_manager *gm;
extern G_manager *valid_p;

#if REBLOOM
extern G_manager *rb;
extern G_manager *re_list;
#endif

extern BF_TABLE *b_table;
extern bool *lba_flag;
extern bool *lba_bf;
extern B_OOB *bloom_oob;

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
extern int32_t n_superblocks;
extern int32_t n_segments;
extern int32_t pps;

#if REBLOOM
extern uint32_t r_check;
#endif
extern uint32_t g_cnt;

//bloomftl_util.c
int lba_compare(const void *, const void *);
int gc_compare(const void *, const void *);



uint32_t ppa_alloc(uint32_t);
uint32_t table_lookup(uint32_t, bool);	//Function for BFs of superblock
int64_t bf_lookup(uint32_t, uint32_t, uint32_t, uint8_t, bool); //Function to find correct BF

//When not reblooming trigger, use this function
/*
 *  
 *
*/
uint32_t check_first(uint32_t); 




algo_req* assign_pseudo_req(TYPE, value_set *, request *);

/* Functions for valid copy in gc */
value_set* SRAM_load(int64_t, int ,TYPE);
void SRAM_unload(SRAM *, int64_t, int, TYPE);


uint32_t get_cur_block(uint32_t); //Function to get current using single block 
void reset_cur_idx(uint32_t);     //Among single blocks, pick empty block


//Functions for bloomfilter set
uint32_t set_bf_table(uint32_t, uint32_t, uint32_t); 
void set_bf(uint32_t, uint32_t, uint32_t);

//Function Bf table reset for a superblock after GC or Reblooming
void reset_bf_table(uint32_t);

//debugging.c
void all_ppa_flag(void);
void single_ppa_flag(uint32_t);

//bloomftl.c
uint32_t bloom_create(lower_info *, blockmanager *bm, algorithm *);
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

//Function to erase victim single block in superblock, victim block selects in round-robin
uint32_t rebloom_gc(SRAM *, int32_t *, int32_t *, uint32_t, uint32_t); 
//Function to make reblooming lists in superblock
uint32_t set_rebloom_list(int32_t);
//Function to check valid/invalid for reblooming lists
uint32_t check_rb_valid(SRAM *, int32_t);
#endif

static inline uint32_t hashfunction(uint32_t key) {
    key ^= key >> 15;
    key *= 2246822519U;
    key ^= key >> 13;
    key *= 3266489917U;
    key ^= key >> 16;

    return key;
}
/* Fibonach hash */
static inline uint32_t hashing_key(uint32_t key) {
    return (uint32_t)((0.618033887 * key) * 1024);
}

/* Function to check Symbolized BF in BF array, This made by Jiho */ 
static inline bool get_bf(uint32_t hashed_key, uint32_t pbn, uint32_t p_idx) {
    uint32_t bf_bits, h;

    int start = sb[pbn].bf_page[p_idx].s_idx;
    int length = bf->base_bf[p_idx].s_bits;
    int end_byte = (start*length + length - 1) / 8;
    int end_bit = (start*length + length - 1) % 8;
    int symb_arr_sz = end_byte - ((start*length) / 8) + 1;
    uint8_t chunk_sz = length > end_bit + 1 ? end_bit + 1 : length;
    bf_bits = bf->base_bf[p_idx].bf_bits;

#if OFFSET_MODE
	h = hashfunction(hashed_key) % bf_bits;
#else
	h = hashed_key % bf_bits;
#endif
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
