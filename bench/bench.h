#ifndef __H_BENCH__
#define __H_BENCH__
#include "../include/settings.h"
#include "../include/container.h"
#include "measurement.h"

#define PRINTPER 1
#define ALGOTYPE 10
#define LOWERTYPE 10
#define BENCHNUM 16

//RocksDB bench setting

#define ROCKS_S_W_16   "/home/yumin/real_trace/rocksdb_16/rocks_s_w_16.out"
#define ROCKS_R_W_16   "/home/yumin/real_trace/db_bench_16/db2_r_w_16.out"
#define ROCKS_SW_RR_16 "/home/yumin/real_trace/rocksdb_16/rocks_sw_rr_16.out"
#define ROCKS_RW_SR_16 "/home/yumin/real_trace/rocksdb_16/rocks_rw_sr_16.out"
#define ROCKS_RW_RR_16 "/home/yumin/real_trace/db_bench_16/db2_rwrr_16.out"
#define ROCKS_S_W_32   "/home/yumin/real_trace/rocksdb_32/rocks_s_w_32.out"
#define ROCKS_R_W_32   "/home/yumin/real_trace/rocksdb_32/rocks_r_w_32.out"
#define ROCKS_SW_RR_32 "/home/yumin/real_trace/rocksdb_32/rocks_sw_rr_32.out"
#define ROCKS_RW_SR_32 "/home/yumin/real_trace/rocksdb_32/rocks_rw_sr_32.out"
#define ROCKS_RW_RR_32 "/home/yumin/real_trace/rocksdb_32/rocks_rw_rr_32.out"

//TPC-C bench setting
#define TPC_C_W_16     "/home/yumin/real_trace/tpc_16/tpc_write_16.out"
#define TPC_C_BENCH_16 "/home/yumin/real_trace/tpc_16/tpc_bench_16.out"
#define TPC_C_W_32     "/home/yumin/real_trace/tpc_32/tpc_c_write_32.out"
#define TPC_C_BENCH_32 "/home/yumin/real_trace/tpc_32/tpc_bench_32.out"

//YCSB bench setting
#define YCSB_LOAD_16   "/home/yumin/real_trace/test/t2_load_16.out"
#define YCSB_RUN_16    "/home/yumin/real_trace/test/t2_run_16.out"


#define YCSB_LOAD_32   "/home/yumin/real_trace/ycsb_32/ycsb_load_32a.out"
#define YCSB_RUN_32    "/home/yumin/real_trace/ycsb_32/ycsb_run_32a.out"

//MonetDB bench setting (TPC-H)
#define MONET_16     "/home/yumin/real_trace/monet_16/m_load_16_16.out"
#define MONET_B_16   "/home/yumin/real_trace/monet_16/m_run_16_16.out"

//Sysbench setting with mysql
#define SYS_LOAD_16		"/home/yumin/real_trace/sysbench_16/f2fs_load_16.out"
#define SYS_RUN_16		"/home/yumin/real_trace/sysbench_16/f2fs_run_16.out"



//Filebench

#define FSERVER_LOAD_16     "/home/yumin/real_trace/filebench_16/fserver_load_16.out"
#define FSERVER_RUN_16      "/home/yumin/real_trace/filebench_16/fserver_run_16.out"

#define VARMAIL_LOAD_16     "/home/yumin/real_trace/filebench_16/varmail_load_16.out"
#define VARMAIL_RUN_16      "/home/yumin/real_trace/filebench_16/varmail_run_16.out"


#define WEBSERVER_LOAD_16   "/home/yumin/real_trace/filebench_16/webserver_load_16.out"
#define WEBSERVER_RUN_16    "/home/yumin/real_trace/filebench_16/webserver_run_16.out"


#define WEBPROXY_LOAD_16    "/home/yumin/real_trace/filebench_16/webproxy_load_16.out"
#define WEBPROXY_RUN_16     "/home/yumin/real_trace/filebench_16/webproxy_run_16.out"


//FIO
#define FIO_SR_LOAD_16  "/home/yumin/real_trace/fio_16/fio_sr_load_16.out"
#define FIO_SR_16       "/home/yumin/real_trace/fio_16/fio_sr_run_16.out"

#define FIO_RR_LOAD_16  "/home/yumin/real_trace/fio_16/fio_rr_load_16.out"
#define FIO_RR_16       "/home/yumin/real_trace/fio_16/fio_rr_run_16.out"

#define FIO_MIX_LOAD_16 "/home/yumin/real_trace/fio_16/fio_mix_load_16.out"
#define FIO_MIX_16      "/home/yumin/real_trace/fio_16/fio_mix_run_16.out"

#define FIO_RWSR_LOAD_16    "/home/yumin/real_trace/fio_16/fio_rwsr_load_16.out"
#define FIO_RWSR_RUN_16     "/home/yumin/real_trace/fio_16/fio_rwsr_run_16.out"

#define FIO_RWMIX_LOAD_16       "/home/yumin/real_trace/fio_16/fio_rwmix_load_16.out"
#define FIO_RWMIX_RUN_16        "/home/yumin/real_trace/fio_16/fio_rwmix_run_16.out"

#define FIO_RWRR_LOAD_16    "/home/yumin/real_trace/fio_16/fio_rwrr_load_16.out"
#define FIO_RWRR_RUN_16     "/home/yumin/real_trace/fio_16/fio_rwrr_run_16.out"



#ifdef CDF
#define TIMESLOT 10 //micro sec
#endif

#define BENCHSETSIZE (1024+1)
typedef struct{
	FSTYPE type;
	KEYT key;
	V_PTR value;
	uint32_t range;
	uint32_t length;
	int mark;
}bench_value;

typedef struct{
	KEYT start;
	KEYT end;
	uint64_t number;
	int32_t seq_locality;
	bench_type type;
}bench_meta;

typedef struct{
	uint64_t total_micro;
	uint64_t cnt;
	uint64_t max;
	uint64_t min;
}bench_ftl_time;

typedef struct{
	uint64_t algo_mic_per_u100[12];
	uint64_t lower_mic_per_u100[12];
	uint64_t algo_sec,algo_usec;
	uint64_t lower_sec,lower_usec;
#ifdef CDF
	uint64_t write_cdf[1000000/TIMESLOT+1];
	uint64_t read_cdf[1000000/TIMESLOT+1];
#endif
	uint64_t read_cnt,write_cnt;
	bench_ftl_time ftl_poll[ALGOTYPE][LOWERTYPE];
	MeasureTime bench;
}bench_data;

typedef struct{
	bench_value *body[BENCHSETSIZE];
	uint32_t bech;
	uint32_t benchsetsize;
	volatile uint64_t n_num;//request throw num
	volatile uint64_t m_num;
	volatile uint64_t r_num;//request end num
	bool finish;
	bool empty;
	int mark;
	uint64_t notfound;
	uint64_t write_cnt;
	uint64_t read_cnt;
	int32_t seq_locality;
	bench_type type;
	MeasureTime benchTime;
	MeasureTime benchTime2;
	uint64_t cache_hit;
}monitor;

typedef struct{
	int n_num;
	int m_num;
	monitor *m;
	bench_meta *meta;
	bench_data *datas;
	lower_info *li;
	uint32_t error_cnt;
}master;

//Real bench parsing functions
extern uint64_t trace_cdf[1000000/10+1];


void bench_init();
void bench_add(bench_type type,KEYT start, KEYT end,uint64_t number, int32_t sequentiality);

bench_value* get_bench();
void bench_refresh(bench_type, KEYT start, KEYT end, uint64_t number);
void bench_free();
void bench_print();
void bench_li_print(lower_info *,monitor *);
bool bench_is_finish_n(int n);
bool bench_is_finish();
void bench_cache_hit(int mark);
void bench_reap_data(request *const,lower_info *);
void bench_reap_nostart(request *const);
char *bench_lower_type(int);


void bench_algo_start(request *const);
void bench_algo_end(request *const);
void bench_lower_start(request *const);
void bench_lower_end(request* const);
void bench_lower_w_start(lower_info *);
void bench_lower_w_end(lower_info *);
void bench_lower_r_start(lower_info *);
void bench_lower_r_end(lower_info *);
void bench_lower_t(lower_info*);



#ifdef CDF
void bench_cdf_print(uint64_t, uint8_t istype, bench_data*);
#endif
void bench_update_ftltime(bench_data *_d, request *const req);
void bench_ftl_cdf_print(bench_data *_d);
void free_bnech_all();
void free_bench_one(bench_value *);
#endif
