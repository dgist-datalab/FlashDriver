/*
 * Header for coarse.c
 */

#ifndef __COARSE_H__
#define __COARSE_H__

#include "demand.h"

int cg_create(cache_t, struct demand_cache *);
int cg_destroy();
int cg_load(lpa_t, request *const, snode *wb_entry);
int cg_list_up(lpa_t, request *const, snode *wb_entry);
int cg_wait_if_flying(lpa_t, request *const, snode *wb_entry);
int cg_touch(lpa_t);
int cg_update(lpa_t, struct pt_struct pte);
bool cg_is_hit(lpa_t);
bool cg_is_full();
struct pt_struct cg_get_pte(lpa_t lpa);
ppa_t cg_get_ppa(lpa_t lpa);
#ifdef STORE_KEY_FP
fp_t cg_get_fp(lpa_t lpa);
#endif

#endif
