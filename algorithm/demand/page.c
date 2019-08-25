/*
 * Demand-based FTL Page Allocation including Garbage Collection
 */

#include "demand.h"
#include "page.h"
#include "utility.h"
#include "../../interface/interface.h"

extern algorithm __demand;

extern struct demand_env env;
extern struct demand_member member;
extern struct demand_stat d_stat;

__segment *d_active;
__segment *t_active;

__segment *d_reserve;
__segment *t_reserve;

int page_create(blockmanager *bm) {
	d_reserve = bm->pt_get_segment(bm, DATA_S, true);
	d_active = NULL;

	t_reserve = bm->pt_get_segment(bm, MAP_S, true);
	t_active = NULL;

	return 0;
}

static int _do_bulk_read_valid_pages(blockmanager *bm, struct gc_table_struct **bulk_table, __gsegment *target_seg, bool is_data) {
	__block *target_blk = NULL;
	ppa_t ppa = 0;
	int blk_idx = 0;
	int page_idx = 0;

	int i = 0;
	for_each_page_in_seg_blocks(target_seg, target_blk, ppa, blk_idx, page_idx) {
		if (bm->is_valid_page(bm, ppa)) {
			value_set *origin = inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
			__demand.li->read(ppa, PAGESIZE, origin, ASYNC, make_algo_req_default((is_data) ? GCDR : GCMR, origin));

			bulk_table[i] = (struct gc_table_struct *)malloc(sizeof(struct gc_table_struct));
			bulk_table[i]->origin   = origin;
			bulk_table[i]->lpa_list = (lpa_t *)bm->get_oob(bm, ppa);
			bulk_table[i]->ppa      = ppa;

			i++;
		}
	}
	int nr_valid_pages = i;
	return nr_valid_pages;
}

static void _do_wait_until_read_all(int nr_valid_pages) {
	while (member.nr_valid_read_done != nr_valid_pages) {}
	member.nr_valid_read_done = 0;
}

static int _do_bulk_write_valid_pages(blockmanager *bm, struct gc_table_struct **bulk_table, int nr_valid_pages, bool is_data) {
	for (int i = 0; i < nr_valid_pages; i++) {
		ppa_t new_ppa = bm->get_page_num(bm, (is_data) ? d_reserve : t_reserve);
		value_set *new_vs = inf_get_valueset(NULL, FS_MALLOC_W, PAGESIZE);
#ifdef HASH_KVSSD
		if (is_data) copy_value_onlykey(new_vs, bulk_table[i]->origin);
#endif
		__demand.li->write(new_ppa, PAGESIZE, new_vs, ASYNC, make_algo_req_default((is_data) ? GCDW : GCMW, new_vs));

		bulk_table[i]->ppa = new_ppa;

		bm->populate_bit(bm, new_ppa);
		bm->set_oob(bm, (char *)bulk_table[i]->lpa_list, sizeof(uint32_t), new_ppa);

		if (!is_data) {
			member.cmt[bulk_table[i]->lpa_list[0]]->t_ppa = new_ppa;
		}

		inf_free_valueset(bulk_table[i]->origin, FS_MALLOC_R);
	}
	return 0;
}

static int lpa_compare(const void *a, const void *b) {
	lpa_t a_lpa = (*(struct gc_table_struct **)a)->lpa_list[0];
	lpa_t b_lpa = (*(struct gc_table_struct **)b)->lpa_list[0];

	if (a_lpa < b_lpa) return -1;
	else if (a_lpa == b_lpa) return 0;
	else return 1;
}

static int _do_bulk_mapping_update(blockmanager *bm, struct gc_table_struct **bulk_table, int nr_valid_pages) {
	qsort(bulk_table, nr_valid_pages, sizeof(struct gc_table_struct *), lpa_compare);

	/* read mapping table which needs update */
	volatile int nr_update_tpages = 0;
	for (int i = 0; i < nr_valid_pages; i++) {
		struct cmt_struct *cmt = member.cmt[IDX(bulk_table[i]->lpa_list[0])];

		if (!CACHE_HIT(cmt->pt)) {
			value_set *_value_mr = inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
			__demand.li->read(cmt->t_ppa, PAGESIZE, _value_mr, ASYNC, make_algo_req_default(GCMR_DGC, _value_mr));

			if (bm->is_valid_page(bm, cmt->t_ppa)) {
				bm->unpopulate_bit(bm, cmt->t_ppa);
			}
			nr_update_tpages++;

			while (i+1 < nr_valid_pages && IDX(bulk_table[i+1]->lpa_list[0]) == cmt->idx) {
				i++;
			}
		}
	}

	/* wait */
	while (member.nr_tpages_read_done == nr_update_tpages) {}
	member.nr_tpages_read_done = 0;

	/* write */
	for (int i = 0; i < nr_valid_pages; i++) {
		struct cmt_struct *cmt = member.cmt[IDX(bulk_table[i]->lpa_list[0])];
		struct pt_struct *pt = member.mem_table[cmt->idx];

		pt[OFFSET(bulk_table[i]->lpa_list[0])].ppa = bulk_table[i]->ppa;
		while (i+1 < nr_valid_pages && IDX(bulk_table[i+1]->lpa_list[0]) == cmt->idx) {
			pt[OFFSET(bulk_table[i+1]->lpa_list[0])].ppa = bulk_table[i+1]->ppa;
			i++;
		}

		cmt->t_ppa = get_tpage(bm);
		value_set *_value_mw = inf_get_valueset(NULL, FS_MALLOC_W, PAGESIZE);
		__demand.li->write(cmt->t_ppa, PAGESIZE, _value_mw, ASYNC, make_algo_req_default(GCMW_DGC, _value_mw));

		bm->populate_bit(bm, cmt->t_ppa);
		bm->set_oob(bm, (char *)&cmt->idx, sizeof(cmt->idx), cmt->t_ppa);

		cmt->state = CLEAN;
	}
	return 0;
}


int dpage_gc(blockmanager *bm) {
	d_stat.dgc_cnt++;

	struct gc_table_struct **bulk_table = (struct gc_table_struct **)calloc(_PPS,sizeof(struct gc_table_struct *));

	/* get the target gsegment */
	__gsegment *target_seg = bm->pt_get_gc_target(bm, DATA_S);

	int nr_valid_pages = _do_bulk_read_valid_pages(bm, bulk_table, target_seg, true);

	_do_wait_until_read_all(nr_valid_pages);

	_do_bulk_write_valid_pages(bm, bulk_table, nr_valid_pages, true);

	_do_bulk_mapping_update(bm, bulk_table, nr_valid_pages);

	/* trim blocks on the gsegemnt */
	bm->pt_trim_segment(bm, DATA_S, target_seg, __demand.li);

	/* reserve -> active / target_seg -> reserve */
	d_active  = d_reserve;
	d_reserve = bm->change_pt_reserve(bm, DATA_S, d_reserve);

	for (int i = 0; i < nr_valid_pages; i++) free(bulk_table[i]);
	free(bulk_table);

	return nr_valid_pages;
}

ppa_t get_dpage(blockmanager *bm) {
	ppa_t ppa;
	if (!d_active || bm->check_full(bm, d_active, MASTER_BLOCK)) {
		if (bm->pt_isgc_needed(bm, DATA_S)) {
#ifdef DVALUE
			int nr_valid_pages = dpage_gc_dvalue(bm);
#else
			int nr_valid_pages = dpage_gc(bm);
#endif

#ifdef PRINT_GC_STATUS
			printf("DATA GC - (valid/total: %d/%d)\n", nr_valid_pages, _PPS);
#endif
		} else {
			d_active = bm->pt_get_segment(bm, DATA_S, false);
		}
	}
	ppa = bm->get_page_num(bm, d_active);
	return ppa;
}

int tpage_gc(blockmanager *bm) {
	d_stat.tgc_cnt++;

	struct gc_table_struct **bulk_table = (struct gc_table_struct **)calloc(sizeof(struct gc_table_struct *), _PPS);

	/* get the target gsegment */
	__gsegment *target_seg = bm->pt_get_gc_target(bm, MAP_S);

	int nr_valid_pages = _do_bulk_read_valid_pages(bm, bulk_table, target_seg, false);

	_do_wait_until_read_all(nr_valid_pages);

	_do_bulk_write_valid_pages(bm, bulk_table, nr_valid_pages, false);

	/* trim blocks on the gsegemnt */
	bm->pt_trim_segment(bm, MAP_S, target_seg, __demand.li);

	/* reserve -> active / target_seg -> reserve */
	t_active  = t_reserve;
	t_reserve = bm->change_pt_reserve(bm, MAP_S, t_reserve);

	for (int i = 0; i < nr_valid_pages; i++) free(bulk_table[i]);
	free(bulk_table);

	return nr_valid_pages;
}

ppa_t get_tpage(blockmanager *bm) {
	ppa_t ppa;
	if (!t_active || bm->check_full(bm, t_active, MASTER_BLOCK)) {
		if (bm->pt_isgc_needed(bm, MAP_S)) {
			int nr_valid_pages = tpage_gc(bm);
#ifdef PRINT_GC_STATUS
			printf("TRANS GC - (valid/total: %d/%d)\n", nr_valid_pages, _PPS);
#endif
		} else {
			t_active = bm->pt_get_segment(bm, MAP_S, false);
		}
	}
	ppa = bm->get_page_num(bm, t_active);
	return ppa;
}
