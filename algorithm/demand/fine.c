/*
 * Fine-grained Cache
 */

#include "cache.h"
#include "fine.h"
#include "page.h"
#include "utility.h"
#include "../../interface/interface.h"

extern struct algorithm __demand;

extern struct demand_env d_env;
extern struct demand_member d_member;
extern struct demand_stat d_stat;

struct cache_env *cenv;
struct cache_member *cmbr;
struct cache_stat *cstat;

struct demand_cache fg_cache = {
	.create = fg_create,
	.destroy = fg_destroy,
	.load = fg_load,
	.list_up = fg_list_up,
	.wait_if_flying = fg_wait_if_flying,
	.touch = fg_touch,
	.update = fg_update,
	.get_pte = fg_get_pte,
/*	.get_ppa = fg_get_ppa,
#ifdef STORE_KEY_FP
	.get_fp = fg_get_fp,
#endif */
	.is_hit = fg_is_hit,
	.is_full = fg_is_full,
};

static void print_cache_env(struct cache_env *const _env) {
	puts("");
	printf(" |---------- Demand Cache Log: Fine-grained Cache\n");
	printf(" | Total trans pages:          %d\n", _env->nr_valid_tpages);
	printf(" |       trans entries:        %d\n", _env->nr_valid_tentries);
	printf(" | Caching Ratio:              %0.3f%%\n", _env->caching_ratio * 100);
	printf(" |  - Max cached tentries:     %d\n", _env->max_cached_tentries);
	printf(" |  (PageFTL cached tentries:  %d)\n", _env->nr_tpages_optimal_caching * EPP);
	printf(" |---------- Demand Cache Log END\n");
	puts("");
}

static void fg_env_init(cache_t c_type, struct cache_env *const _env) {
	_env->c_type = c_type;

	_env->nr_tpages_optimal_caching = d_env.nr_pages * 4 / PAGESIZE;
	_env->nr_valid_tpages = d_env.nr_pages * ENTRY_SIZE / PAGESIZE;
	_env->nr_valid_tentries = _env->nr_valid_tpages * EPP;

	_env->caching_ratio = d_env.caching_ratio;
	_env->max_cached_tpages = _env->nr_tpages_optimal_caching * _env->caching_ratio;
	//_env->max_cached_tentries = 1024;
	_env->max_cached_tentries = _env->max_cached_tpages * EPP;
	//_env->max_cached_tentries = d_env.nr_pages * _env->caching_ratio;

#ifdef DVALUE
	_env->nr_valid_tpages *= GRAIN_PER_PAGE;
	_env->nr_valid_tentries *= GRAIN_PER_PAGE;
#endif

	print_cache_env(_env);
}

static void fg_member_init(struct cache_member *const _member) {
	struct cmt_struct **cmt = (struct cmt_struct **)calloc(cenv->nr_valid_tpages, sizeof(struct cmt_struct *));
	for (int i = 0; i < cenv->nr_valid_tpages; i++) {
		cmt[i] = (struct cmt_struct *)malloc(sizeof(struct cmt_struct));

		cmt[i]->t_ppa = UINT32_MAX;
		cmt[i]->idx = i;
		cmt[i]->pt = NULL;
		cmt[i]->lru_ptr = NULL;
		cmt[i]->state = CLEAN;
		cmt[i]->is_flying = false;

		q_init(&cmt[i]->retry_q, d_env.wb_flush_size);
		q_init(&cmt[i]->wait_q, d_env.wb_flush_size);

		cmt[i]->is_cached = (bool *)calloc(EPP, sizeof(bool));
		memset(cmt[i]->is_cached, 0, EPP * sizeof(bool));
		cmt[i]->cached_cnt = 0;
		cmt[i]->dirty_cnt = 0;
	}
	_member->cmt = cmt;

	_member->mem_table = (struct pt_struct **)calloc(cenv->nr_valid_tpages, sizeof(struct pt_struct *));
	for (int i = 0; i < cenv->nr_valid_tpages; i++) {
		_member->mem_table[i] = (struct pt_struct *)malloc(PAGESIZE);
		for (int j = 0; j < EPP; j++) {
			_member->mem_table[i][j].ppa = UINT32_MAX;
#ifdef STORE_KEY_FP
			_member->mem_table[i][j].key_fp = 0;
#endif
		}
	}

	_member->nr_cached_tentries = 0;
}

static void fg_stat_init(struct cache_stat *const _stat) {

}


int fg_create(cache_t c_type, struct demand_cache *dc) {
	cenv = &dc->env;
	cmbr = &dc->member;
	cstat = &dc->stat;

	fg_env_init(c_type, &dc->env);
	fg_member_init(&dc->member);
	fg_stat_init(&dc->stat);

	return 0;
}

static void fg_member_destroy(struct cache_member *_member) {
	for (int i = 0; i < cenv->nr_valid_tpages; i++) {
		q_free(_member->cmt[i]->retry_q);
		q_free(_member->cmt[i]->wait_q);
		free(_member->cmt[i]->is_cached);
		free(_member->cmt[i]);
	}
	free(_member->cmt);

	for (int i = 0; i < cenv->nr_valid_tpages; i++) {
		free(_member->mem_table[i]);
	}
}

static void print_cache_stat(struct cache_stat *_stat) {
	puts("===================");
	puts(" Cache Performance ");
	puts("===================");
	puts("");

	printf("Cache_Hit:\t%ld\n", _stat->cache_hit);
	printf("Cache_Miss:\t%ld\n", _stat->cache_miss);
	printf("Hit ratio:\t%.2f%%\n", (float)(_stat->cache_hit)/(_stat->cache_hit+_stat->cache_miss)*100);
	puts("");

	printf("Blocked miss:\t%ld\n", _stat->blocked_miss);
	puts("");

	printf("Clean evict:\t%ld\n", _stat->clean_evict);
	printf("Dirty evict:\t%ld\n", _stat->dirty_evict);
	puts("");

}

int fg_destroy() {
	fg_member_destroy(cmbr);
	print_cache_stat(cstat);
	return 0;
}

int fg_load(lpa_t lpa, request *const req, snode *wb_entry) {
	struct cmt_struct *cmt = cmbr->cmt[IDX(lpa)];
	struct inflight_params *i_params;

	if (IS_INITIAL_PPA(cmt->t_ppa)) {
		return 0;
	}

	i_params = get_iparams(req, wb_entry);
	i_params->jump = GOTO_LIST;

	value_set *_value_mr = inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
	__demand.li->read(cmt->t_ppa, PAGESIZE, _value_mr, ASYNC, make_algo_req_rw(MAPPINGR, _value_mr, req, wb_entry));

	cmt->is_flying = true;

	return 1;
}

int fg_list_up(lpa_t lpa, request *const req, snode *wb_entry) {
	int rc = 0;
	blockmanager *bm = __demand.bm;

	struct cmt_struct *cmt = cmbr->cmt[IDX(lpa)];
	struct cmt_struct *victim = NULL;

	struct inflight_params *i_params;

	/* evict if cache is full */
	if (fg_is_full()) {

		/* get a victim tpage as greedy, O(n) -> FIXME: O(log n) */
		int max_entry_cnt = 0;
		for (int i = 0; i < cenv->nr_valid_tpages; i++) {
			struct cmt_struct *iter = cmbr->cmt[i];
			if (iter->cached_cnt > max_entry_cnt) {
				max_entry_cnt = iter->cached_cnt;
				victim = iter;
			}
		}

		if (victim->state == DIRTY) {
			cstat->dirty_evict++;
			/* read victim page */
			if (!IS_INITIAL_PPA(victim->t_ppa)) {
				value_set *_value_mr = inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
				struct algo_req *a_req = make_algo_req_sync(MAPPINGR, _value_mr);
				dl_sync *sync_mutex = ((struct demand_params *)a_req->params)->sync_mutex;

				__demand.li->read(victim->t_ppa, PAGESIZE, _value_mr, ASYNC, a_req);
				dl_sync_wait(sync_mutex);
				free(sync_mutex);

				invalidate_page(bm, victim->t_ppa, MAP);
			}

			/* update victim page */
			// empty

			/* write victim page */
			i_params = get_iparams(req, wb_entry);
			i_params->jump = GOTO_COMPLETE;
			i_params->pte = cmbr->mem_table[IDX(lpa)][OFFSET(lpa)];

			victim->t_ppa = get_tpage(bm);
			victim->state = CLEAN;

			value_set *_value_mw = inf_get_valueset(NULL, FS_MALLOC_W, PAGESIZE);
			__demand.li->write(victim->t_ppa, PAGESIZE, _value_mw, ASYNC, make_algo_req_rw(MAPPINGW, _value_mw, req, wb_entry));
			set_oob(bm, victim->idx, victim->t_ppa, 0);

			rc = 1;
		} else {
			cstat->clean_evict++;
		}

		memset(victim->is_cached, 0, EPP * sizeof(bool));
		cmbr->nr_cached_tentries -= max_entry_cnt;
		victim->cached_cnt = 0;
	}

	/* list_up */
	cmt->is_cached[OFFSET(lpa)] = true;
	cmt->cached_cnt++;
	cmbr->nr_cached_tentries++;

	if (cmt->is_flying) {
		cmt->is_flying = false;

		if (req) {
			request *retry_req;
			while ((retry_req = (request *)q_dequeue(cmt->retry_q))) {
				struct inflight_params *i_params = get_iparams(retry_req, NULL);

				if (fg_is_full()) {
					i_params->jump = GOTO_LOAD;

				} else {
					lpa_t retry_lpa = get_lpa(retry_req->key, retry_req->hash_params);

					cmt->is_cached[OFFSET(retry_lpa)] = true;
					cmt->cached_cnt++;
					cmbr->nr_cached_tentries++;

					i_params->jump = GOTO_COMPLETE;
					i_params->pte = cmbr->mem_table[IDX(retry_lpa)][OFFSET(retry_lpa)];
				}
				inf_assign_try(retry_req);
			}
		} else if (wb_entry) {
			snode *retry_wbe;
			while ((retry_wbe = (snode *)q_dequeue(cmt->retry_q))) {
				struct inflight_params *i_params = get_iparams(NULL, retry_wbe);

				if (fg_is_full()) {
					i_params->jump = GOTO_LOAD;

				} else {
					lpa_t retry_lpa = get_lpa(retry_wbe->key, retry_wbe->hash_params);

					cmt->is_cached[OFFSET(retry_lpa)] = true;
					cmt->cached_cnt++;
					cmbr->nr_cached_tentries++;

					i_params->jump = GOTO_COMPLETE;
					i_params->pte = cmbr->mem_table[IDX(retry_lpa)][OFFSET(retry_lpa)];
				}
				q_enqueue((void *)wb_entry, d_member.wb_retry_q);
			}
		}
	}

	return rc;
}

int fg_wait_if_flying(lpa_t lpa, request *const req, snode *wb_entry) {
	struct cmt_struct *cmt = cmbr->cmt[IDX(lpa)];
	if (cmt->is_flying) {
		cstat->blocked_miss++;

		if (req) q_enqueue((void *)req, cmt->retry_q);
		else if (wb_entry) q_enqueue((void *)wb_entry, cmt->retry_q);
		else abort();

		return 1;
	}
	return 0;
}

int fg_touch(lpa_t lpa) {
	return 0;
}

int fg_update(lpa_t lpa, struct pt_struct pte) {
	struct cmt_struct *cmt = cmbr->cmt[IDX(lpa)];
	struct pt_struct *pt = cmbr->mem_table[IDX(lpa)];

	pt[OFFSET(lpa)] = pte;
	if (cmt->is_cached[OFFSET(lpa)]) {
		/* FIXME: to handle later update */
		cmt->state = DIRTY;
	} else {
		static int cnt = 0;
		printf("fg_update %d\n", ++cnt);
	}
	return 0;
}

bool fg_is_hit(lpa_t lpa) {
	struct cmt_struct *cmt = cmbr->cmt[IDX(lpa)];
	if (cmt->is_cached[OFFSET(lpa)]) {
		cstat->cache_hit++;
		return 1;
	} else {
		cstat->cache_miss++;
		return 0;
	}
}

bool fg_is_full() {
	return (cmbr->nr_cached_tentries >= cenv->max_cached_tentries);
}

struct pt_struct fg_get_pte(lpa_t lpa) {
	struct cmt_struct *cmt = cmbr->cmt[IDX(lpa)];
	struct pt_struct *pt = cmbr->mem_table[IDX(lpa)];

	if (!cmt->is_cached[OFFSET(lpa)]) abort();

	struct pt_struct pte = pt[OFFSET(lpa)];

	return pte;
}

ppa_t fg_get_ppa(lpa_t lpa) {
	struct cmt_struct *cmt = cmbr->cmt[IDX(lpa)];
	struct pt_struct *pt = cmbr->mem_table[IDX(lpa)];

	if (!cmt->is_cached[OFFSET(lpa)]) abort();

	ppa_t ppa = pt[OFFSET(lpa)].ppa;

	return ppa;
}

#ifdef STORE_KEY_FP
fp_t fg_get_fp(lpa_t lpa) {
	struct pt_struct *pt = cmbr->mem_table[IDX(lpa)];
	fp_t key_fp = pt[OFFSET(lpa)].key_fp;
	return key_fp;
}
#endif
