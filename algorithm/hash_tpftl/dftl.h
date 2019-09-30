#ifndef __H_DFTL__
#define __H_DFTL__

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include "../../interface/interface.h"
#include "../../interface/queue.h"
#include "../../include/container.h"
#include "../../include/ftl_settings.h"
#include "../../include/dl_sync.h"
#include "../../include/types.h"
#include "../../include/data_struct/demand_hash.h"
#ifdef W_BUFF
#include "../Lsmtree/skiplist.h"
#endif
#include "../blockmanager/BM.h"
#include "lru_list.h"

#define TYPE uint8_t
#define DATA_R DATAR
#define DATA_W DATAW
#define MAPPING_R MAPPINGR  // async
#define MAPPING_W MAPPINGW
#define MAPPING_M ( MAPPINGR | 0x80 )  // polling
#define TGC_R GCMR
#define TGC_W GCMW
#define TGC_M ( GCMR | 0x80 )
#define DGC_R GCDR
#define DGC_W GCDW

#define EPP (PAGESIZE / 4)  // Number of table entries per page
#define D_IDX (lpa / EPP)   // Idx of directory table
#define P_IDX (lpa % EPP)   // Idx of page table

#define CLEAN 0
#define DIRTY 1

#if TPFTL
/* This defined value is redefined to NODE_SIZE on demand_hash.h */
//#define ENTRY_SIZE 20 	    // table idx, ppa, cnt, pointers (tree pointers) 
#define MAX_CNT    63
#define PF_MAX     32
#endif

#define B_TPFTL 1


// Page table data structure
typedef struct demand_mapping_table{
    int32_t ppa; //Index = lpa
} D_TABLE;

// Cache mapping table data strcuture
typedef struct cached_table{
    int32_t t_ppa;
    int32_t idx;
    D_TABLE *p_table;
    NODE *queue_ptr; // for dirty pages (or general use)
   
#if C_CACHE
    NODE *clean_ptr; // for clean pages
#endif
    bool state; // CLEAN or DIRTY
    bool flying;
    request **flying_arr;
    int32_t num_waiting;
    uint32_t read_hit;
    uint32_t write_hit;    
    bool evic_flag;
    bool gc_flag;
    int32_t entry_cnt;   // Count variable for eviction optimization
    int32_t flying_mapping_size; // Reserve cache space for flying request
    //LRU *entry_lru;      // Entry LRU pointer for TPFTL
    hash_node *last_ptr;      // Write pointer for last entry node
    hash_node *read_ptr;	 // Read pointer for last entry node
    bool *h_bitmap;      // hit check bitmap
	bool *s_bitmap;		 // bitmap for checking head entry
	hash_t *ht_ptr;		 // hash table pointer
	int32_t evic_idx;
} C_TABLE;

/* Normal TPFTL struct
struct entry_node{
	int16_t p_index; 
	int32_t ppa;
	int16_t  cnt;
	bool    state;
};
*/

// OOB data structure
typedef struct demand_OOB{
    int32_t lpa;
} D_OOB;

// SRAM data structure (used to hold pages temporarily when GC)
typedef struct demand_SRAM{
    int32_t origin_ppa;
    D_OOB OOB_RAM;
    int32_t *DATA_RAM;
} D_SRAM;

typedef struct demand_params{
    value_set *value;
    dl_sync dftl_mutex;
    TYPE type;
} demand_params;

typedef struct read_params{
    int32_t t_ppa;
    uint8_t read;
} read_params;

typedef struct mem_table{
    D_TABLE *mem_p;
} mem_table;

struct prefetch_struct {
    KEYT ppa;
    snode *sn;
};

/* extern variables */
extern algorithm __demand;

extern LRU *lru;

extern b_queue *free_b;
extern Heap *data_b;
extern Heap *trans_b;

extern C_TABLE *CMT; // Cached Mapping Table
extern uint8_t *VBM;
extern mem_table *mem_arr;
extern b_queue *mem_q;
extern D_OOB *demand_OOB; // Page level OOB

extern BM_T *bm;
extern Block *t_reserved;
extern Block *d_reserved;

volatile extern int32_t trans_gc_poll;
volatile extern int32_t data_gc_poll;

extern int32_t num_page;
extern int32_t num_block;
extern int32_t p_p_b;
extern int32_t num_tpage;
extern int32_t num_tblock;
extern int32_t num_dpage;
extern int32_t num_dblock;
extern int32_t max_cache_entry;
extern int32_t num_max_cache;
extern int32_t max_clean_cache;
extern int32_t max_dirty_cache;

extern int32_t num_caching;

extern int32_t tgc_count;
extern int32_t dgc_count;
extern int32_t read_tgc_count;
extern int32_t tgc_w_dgc_count;

extern int32_t free_cache_size;
extern int32_t flying_cache_size;
extern int32_t prefetch_cnt;
extern hash_node *g_last_ptr;
/* extern variables */

// dftl.c
uint32_t demand_create(lower_info*, algorithm*);
void     demand_destroy(lower_info*, algorithm*);
uint32_t demand_get(request *const);
uint32_t demand_set(request *const);
uint32_t demand_remove(request *const);
uint32_t demand_eviction(request *const, char, bool *, bool *);
uint32_t demand_hit_eviction(request *const, char, bool *, bool *);
void    *demand_end_req(algo_req*);

// dftl_utils.c
algo_req* assign_pseudo_req(TYPE type, value_set *temp_v, request *req);
D_TABLE* mem_deq(b_queue *q);
void mem_enq(b_queue *q, D_TABLE *input);
void merge_w_origin(D_TABLE *src, D_TABLE *dst);
int lpa_compare(const void *a, const void *b);
int32_t tp_alloc(char, bool*);
int32_t dp_alloc();
value_set* SRAM_load(D_SRAM* d_sram, int32_t ppa, int idx, char t);
void SRAM_unload(D_SRAM* d_sram, int32_t ppa, int idx, char t);
void cache_show(char* dest);

// garbage_collection.c
int32_t tpage_GC();
int32_t dpage_GC();


//tp_utils.c
int32_t find_head_idx(int32_t lpa);

void tp_init_hash(C_TABLE *);
hash_node* tp_entry_search(int32_t, int32_t);
hash_node* tp_entry_alloc(int32_t, int32_t, int32_t, int8_t, char);
void tp_entry_free(int32_t, int32_t);
hash_node* tp_entry_update(hash_node *,int32_t, int32_t, int32_t, int8_t, char);
hash_node* tp_entry_op(int32_t, int32_t);
hash_node* tp_entry_split(hash_node *, int32_t, int32_t, bool);
void tp_batch_update(C_TABLE *);

hash_node *hash_eviction(C_TABLE *, int32_t, bool);

//Redblack tp_get_entry(int32_t, int32_t);
hash_node* tp_fetch(int32_t,int32_t);

int32_t cache_print_entries();
int32_t cache_single_entries(C_TABLE *);

int32_t cached_entries();

#endif
