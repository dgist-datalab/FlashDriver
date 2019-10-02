#ifndef __H_SETFTL__
#define __H_SETFTL__

// clean cache flag
#define C_CACHE 0

// sftl & tpftl flag
#define S_FTL 1
#define TPFTL 0
#define BFTL  1

#define REAL_BENCH_SET 0
// memcpy op gc flag
#define MEMCPY_ON_GC 0

// flying mapping page request waiting flag
#define FLYING 0

// write buffering flag
#define W_BUFF 1

// write buffering polling flag depend to W_BUFF
#define W_BUFF_POLL 0

// gc polling flag
#define GC_POLL 0

// eviction polling flag
#define EVICT_POLL 0

// max size of write buffer
#define MAX_SL 1024

#endif
