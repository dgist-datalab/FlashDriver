#ifndef __H_HASHFTL__
#define __H_HASHFTL__

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <unistd.h>
#include <pthread.h>
#include "../../interface/interface.h"
#include "../../interface/queue.h"
#include "../../include/container.h"
#include "../../include/types.h"
#include "../../include/dl_sync.h"
#include "../blockmanager/BM.h"

//#define BENCH
#define TYPE uint8_t
#define DATA_R 1
#define DATA_W 2
#define GC_R 3
#define GC_W 4

#define CLEAN 0
#define DIRTY 1
#define H_VALID 1
#define H_INVALID 2

typedef struct primary_table{
    int32_t hid;
    int32_t ppid;
    int32_t ppa;
    bool state; // CLEAN or DIRTY

} PRIMARY_TABLE;

typedef struct secondary_table{
    int32_t ppa;
    int32_t lpa;
    bool state; // CLEAN or DIRTY

} SECONDARY_TABLE;

// OOB data structure
typedef struct hash_OOB{
	int32_t lpa;
} H_OOB;

// SRAM data structure (used to hold pages temporarily when GC)
typedef struct SRAM{
	H_OOB OOB_RAM;
	PTR PTR_RAM;
} SRAM;

typedef struct hash_params{
	value_set *value;
	dl_sync hash_mutex;
	TYPE type;
} hash_params;

struct gc_block{
	int32_t hid;
	int32_t ppid;
	int32_t idx_secondary;
	bool state;
};



extern algorithm __hashftl;

extern b_queue *free_b;
extern Heap *b_heap;
extern Heap *primary_b;
extern Heap *secondary_b;
extern b_queue *victim_b;

extern struct gc_block *gc_block;



extern PRIMARY_TABLE *pri_table;     // primary table
extern SECONDARY_TABLE *sec_table;   // secondary table
extern H_OOB *hash_OOB;				 // Page level OOB.

extern BM_T *bm;
extern Block *reserved;				 //reserved.
extern int32_t reserved_pba;
extern int32_t empty_idx;

extern int32_t start_b_secondary;

extern int32_t _g_nop;
extern int32_t _g_nob;
extern int32_t _g_ppb;

extern int32_t num_b_primary;

extern int32_t max_secondary;
extern int32_t num_secondary;
extern int32_t num_b_secondary;


extern int32_t num_hid;
extern int32_t num_ppid;
extern int32_t num_page_off;
extern int32_t hid_secondary;
extern int32_t loop_gc;
extern int32_t pba_secondary;

extern volatile int32_t gc_load;
extern int32_t gc_count;
extern int32_t re_gc_count;
extern int32_t re_page_count;

extern int32_t remap_write;

extern int32_t gc_val;
extern int32_t re_number;
extern int32_t gc_pri;

extern int32_t num_write;
extern int32_t num_copy;

extern int32_t valid_copy;
extern int32_t remap_copy;

extern int32_t pri_count;
extern int32_t sec_count;

extern int32_t low_watermark;
extern int32_t high_watermark;

//hashftl.h
uint32_t hash_create(lower_info*, algorithm*);
void     hash_destroy(lower_info*, algorithm*);
uint32_t hash_get(request *const);
uint32_t hash_set(request *const);
uint32_t hash_remove(request *const);
void    *hash_end_req(algo_req*);


//hashftl_util.h
algo_req* assign_pseudo_req(TYPE type, value_set* temp_v, request* req);
value_set* SRAM_load(SRAM* sram, int32_t ppa, int idx); // loads info on SRAM.
void SRAM_unload(SRAM* sram, int32_t ppa, int idx); // unloads info from SRAM.
int32_t get_pba_from_md5(uint64_t md5_result, uint8_t hid);
int32_t check_written(int32_t	pba, int32_t lpa, int32_t* cal_ppid);

int32_t get_ppa_from_table(int32_t lpa);
int32_t set_idx_secondary();

int32_t alloc_page(int32_t lpa, int32_t* cal_ppid, int32_t* hid);
int32_t secondary_alloc(bool);


int32_t get_idx_for_secondary(int32_t lpa);
int32_t map_for_gc(int32_t lpa, int32_t ppa);
int32_t map_for_s_gc(int32_t lpa, int32_t ppa);

int32_t map_for_remap(int32_t lpa, int32_t ppa, int32_t hid, int32_t *cal_ppid, int32_t secondary_idx);


int32_t remap_sec_to_pri(int32_t v_pba, int32_t* cal_ppid);


//hashftl_gc.h
int32_t hash_primary_gc(); // hashftl GC function.
int32_t hash_secondary_gc();


// md5.h
void md5(uint32_t *initial_msg, size_t initial_len, uint64_t* result);
uint64_t md5_u(uint32_t *initial_msg, size_t initial_len);
#endif
