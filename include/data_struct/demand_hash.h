/***************************************************************************
*cr
*cr            (C) Copyright 1995-2019 The Board of Trustees of the
*cr                        University of Illinois
*cr                         All Rights Reserved
*cr
***************************************************************************/
 
/***************************************************************************
* RCS INFORMATION:
*
*      $RCSfile: hash.h,v $
*      $Author: johns $        $Locker:  $             $State: Exp $
*      $Revision: 1.17 $      $Date: 2019/01/17 21:21:03 $
****************************************************************************
* DESCRIPTION:
*   A simple hash table implementation for integers, contributed by John Stone,
*   derived from his ray tracer code.
*   Modfied by yumin won in DGIST's Datalab
***************************************************************************/

#ifndef __DEMAND_HASH_H_
#define __DEMAND_HASH_H_
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../settings.h"
#include "../ftl_settings.h"

#define HASH_FAIL   -1
#define HASH_UPDATE  0
#define HASH_SUCCESS 1
#define HASH_FULL    2
#define HASH_REBUILD 3


#define HASH_LIMIT  4
#define BUCKET_SIZE 2


#define CHANGE_SIZE (PAGESIZE * 0.8)	 //Threshold bitmap_form and original form
#define EOP	    (PAGESIZE / 4)			 //Number entries of page
#define BITMAP_SIZE (EOP / 8)            //Default bitmap size for head entry
#if TPFTL
#define NODE_SIZE  11
#else
#define NODE_SIZE  10					 //The size of hash_node
#endif
#define P_SIZE      4					 //Default pointer size of bucket


typedef struct hash_node{
	uint16_t key;            /* key for hash lookup */
	int32_t  data;           /* data(ppn) in hash node */
#if TPFTL
	int8_t cnt;
	bool state;
#endif
	struct hash_node *next;  /* next node in hash chain */
} hash_node;

typedef struct hash_t {
	hash_node **bucket;      /* array of hash nodes */
	int32_t *bucket_cnt;	 /* number of entries for each bucket */ 
	int32_t size;		 /* size of the array */
	int32_t entries;	 /* number of entries in table */			
} hash_t;


void hash_init(hash_t *ht_ptr, int buckets);
void hash_destroy(hash_t *ht_ptr);

hash_node *hash_lookup(hash_t *ht_ptr, uint32_t key);
#if TPFTL
hash_node *hash_insert(hash_t *ht_ptr, uint32_t key, int data, int8_t cnt);
#else
int32_t hash_insert(hash_t *ht_ptr, uint32_t key, int data);
#endif

#if TPFTL
int32_t hash_delete(hash_t *ht_ptr, hash_node *d_node);
#else
int32_t hash_delete(hash_t *ht_ptr, uint32_t key);
#endif
int32_t hash_update_delete(hash_t *ht_ptr, hash_node *d_node);


int32_t hash_rebuild_table(hash_t *ht_ptr);

int32_t hash_entries(hash_t *ht_ptr);
int32_t hash_buckets(hash_t *ht_ptr);
int32_t hash_table_size(hash_t *ht_ptr);


char *hash_stats(hash_t *ht_ptr);

#endif
