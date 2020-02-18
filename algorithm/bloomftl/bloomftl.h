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
#include "../../interface/bb_checker.h"

#ifdef W_BUFF
#include "../Lsmtree/skiplist.h"
#endif


#define TYPE uint8_t


//#define C_PAGES (PAGESIZE/4096) //coalescing pages for Logical to Physical page
#define C_PAGES 4

#define SYMMETRIC 1
#define PR_SUCCESS 0.9	  //PR ratio (1-fpr)
#define SUPERBLK_SIZE 4	  //Block per Superblock
#define MAX_RB (C_PAGES)  //In Reblooming, max num of coalesced entry
#define S_BIT (MAX_RB/2)  //log((logical page size)/(physical page size)) -> coalasing bit
#define HASH_MODE 1       //Decide superblock by hash, if 0 -> decide superblock by shift operation
#define REBLOOM 1          //Reblooming enable flag 
#define OOR (RANGE+1)      //initial value for reblooming


//Data structure for BF, this structure is used for statistics such as memory usage
typedef struct {
	uint32_t k; //# of functions , this must be 1
	uint32_t m; //# of bits in a bloomfilter
	int n; //# of entyr, this must be 1
	float p; //fpr
	char* body; //bits array
	uint32_t bits_per_entry; //symbolize bits per entry
	uint32_t total_s_bits;   //Total symbolized bit length
	uint32_t total_s_bytes;  //Total symbolized byte
} BF;


//BF table
//one bf table per one superblock

typedef struct {
	uint8_t *bf_arr;    //Array for set bloomfilter
	uint32_t bf_num;    //BF count of a superblock
	Block **b_bucket; //Single block pointer within superblock
	uint32_t c_block;   //Use single block index
	int32_t full;       //Used physical page count
#if REBLOOM
	/*this two variables are used only reblooming, they can be removed*/
	uint32_t pre_lba;   //Sequentiality check variable
	uint32_t rb_cnt;    //Current sequentaility count in superblock
#endif
}BF_TABLE;


/*
    * (S table in humantech paper)
	 * */
typedef struct {
	    bool *p_flag;       //If allocate BF on pyhsical page, set 1.
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
 *  write buffer for a physical page in GC
 */
typedef struct SRAM{
	int32_t oob_ram; //oob
	PTR ptr_ram; //data
}SRAM;



typedef struct {
	T_table *t_table; // change it not to use pointer
	value_set *value;
	uint32_t size;
}G_manager;


typedef struct bloom_params{ //paramater of algo_req
	value_set *value;  //not used maybe
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
extern bool *lba_flag;      //To check if LBA is set
extern bool *lba_bf;	    //To check if LBA has a BF
extern B_OOB *bloom_oob;    


/* I/O count variable */
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


extern uint32_t lnb;   //Num of logical block


/* Num of available pages in a superblock, rest is op pages.
 * e.g., single block = 128, superblock pages = 512, op = 25%, mask = 384 (512-128)
 *
 */
extern uint32_t mask;  

extern int32_t nob; //Num of physical block
extern int32_t ppb; //Page per block
extern int32_t nop; //Num of physical page
extern int32_t lnp; //Num of logical page
extern int32_t nos; //Num of superblock
extern int32_t pps; //Page per superblock

#if REBLOOM
extern uint32_t r_check;  //Reblooming trigger variable
#endif

//bloomftl_util.c

int lba_compare(const void *, const void *);
int gc_compare(const void *, const void *);


uint32_t ppa_alloc(uint32_t);
uint32_t table_lookup(uint32_t, bool); //Function for lookup table in a superblock
int64_t bf_lookup(uint32_t, uint32_t,uint32_t, uint32_t, uint8_t, bool); //Function for checking correct BF


algo_req* assign_pseudo_req(TYPE, value_set *, request *);
value_set* SRAM_load(int64_t, int ,TYPE);
void SRAM_unload(SRAM *, int64_t, int, TYPE);

uint32_t get_cur_block(uint32_t);  //Function to get current using single block
void reset_cur_idx(uint32_t);      //Among single blocks, pick empty block

/* Functions for set BFs in a superblock */
uint32_t set_bf_table(uint32_t, uint32_t);
void set_bf(uint32_t, uint32_t);


void reset_bf_table(uint32_t); //Function BF table reset for a superblock after GC or Reblooming

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
BF *bf_init(int entry, int ppb); //Function for statistics making global bloomfilter
void bf_free(BF *);
uint32_t bf_bytes(uint32_t);

//gc.c

/* Functions for GC */
uint32_t bloom_gc(uint32_t);				
int invalid_block(Block **, int, uint32_t); //Function to invalidate single block in a superblock

#if REBLOOM
//rebloom.c
/* Function for Reblooming */

void rebloom_op(uint32_t);
uint32_t rebloom_gc(SRAM *, int32_t *, int32_t *, uint32_t, uint32_t);
uint32_t set_rebloom_list(int32_t); //Function to make reblooming list
uint32_t check_rb_valid(SRAM *, int32_t); //Function to checking valid page in made reblooming list
#endif
/* Hash Functions */
//Use not this function
static inline uint32_t hashfunction(uint32_t key) {
    key ^= key >> 15;
    key *= 2246822519U;
    key ^= key >> 13;
    key *= 3266489917U;
    key ^= key >> 16;

    return key;
}

//Fibonach hash
static inline uint32_t hashing_key(uint32_t key) {
    return (uint32_t)((0.618033887 * key) * 1024);
}


//Function to check symbolized BF in a superblock
//This is core function, so it made by jiho.
static inline bool get_bf(uint32_t hashed_key, uint32_t superblk, uint32_t bf_idx) {
    uint32_t bf_bits, h;

    int start = bf_idx;
	int length = bf->bits_per_entry;
    int end_byte = (start*length + length - 1) / 8;
    int end_bit = (start*length + length - 1) % 8;
    int symb_arr_sz = end_byte - ((start*length) / 8) + 1;
    uint8_t chunk_sz = length > end_bit + 1 ? end_bit + 1 : length;
  
	bf_bits = bf->m;
	h = hashed_key % bf_bits;

    // 1
    if(end_bit == 7) {
        if(((h & ((1 << chunk_sz) - 1)) ^ (b_table[superblk].bf_arr[end_byte] >> (8 - chunk_sz)))) {
            goto not_exist;
        }
    }
    else{
        if((h ^ b_table[superblk].bf_arr[end_byte]) & ((1 << chunk_sz) - 1)){
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
    if((h & ((1 << chunk_sz) - 1)) ^ (b_table[superblk].bf_arr[end_byte] >> (8 - chunk_sz))){
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
    if((h & ((1 << chunk_sz) - 1)) ^ (b_table[superblk].bf_arr[end_byte] >> (8 - chunk_sz))){
        goto not_exist;
    }

exist:
    return true;

not_exist:
    return false;
}

#endif
