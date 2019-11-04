#ifndef __H_SETTING__
#define __H_SETTING__
#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "ftl_settings.h"
/*
#define free(a) \
	do{\
		printf("%s %d:%p\n",__FILE__,__LINE__,a);\
		free(a)\
	}while(0)
*/

#define K (1024)
#define M (1024*K)
#define G (1024*M)
#define T (1024L*G)
#define P (1024L*T)

#ifdef MLC

#define TOTALSIZE (300L*G)
#define REALSIZE (512L*G)
#define PAGESIZE (8*K)
#define _PPB (256)
#define _PPS (1<<14)
#define BPS ((_PPS)/_PPB)

#elif defined(SLC)

#define GIGAUNIT 16L
#define _OPAREA (GIGAUNIT*G*0.25)

#if !BFTL
#define TOTALSIZE ((GIGAUNIT*G)+_OPAREA)
#endif
//#define TOTALSIZE (GIGAUNIT * G)
#define REALSIZE (512L*G)
#define DEVSIZE (64L * G)
#define PAGESIZE (8*K)

#define _PPB (1<<7)
#define BPS (4)

#define _PPS (_PPB * BPS)
#define A_SIZE 4


#if BFTL
#define L_DEVICE ((GIGAUNIT * G))
#define L_PAGES  (L_DEVICE / PAGESIZE)
#define MASK ceil((_PPB*A_SIZE) * (1 - 0.25))

#if ZYNQ_ON
#define _NOS ceil(((L_PAGES / MASK) * A_SIZE) / BPS)  // Total physical blocks
#define _NOB (_NOS * BPS)
#else
#define _NOS ceil((L_PAGES / MASK)) * A_SIZE // Total physical blocks
#define _NOB (_NOS)
#endif
#define _NOP (_NOB * _PPB)
#define TOTALSIZE (_NOP * PAGESIZE)
#else

//#define _PPS (1<<14)
//#define BPS (64)
#endif

#endif




#define BLOCKSIZE (_PPB*PAGESIZE)

#if !BFTL
#define _NOS ceil(TOTALSIZE/(_PPS*PAGESIZE))
#define _NOP (_PPS*_NOS)
#define _NOB (_NOS*BPS)
#endif

#define _RNOS (REALSIZE/(_PPS*PAGESIZE))//real number of segment

#define RANGE (GIGAUNIT*(M/PAGESIZE)*1024L)
//#define RANGE ((GIGAUNIT)*(M/PAGESIZE)*1024L*0.8)
//#define RANGE (50*(M/PAGESIZE)*1024L*0.8)


#define SIMULATION 0

#define FSTYPE uint8_t
#define KEYT uint32_t
#define BLOCKT uint32_t
#define OOBT uint64_t
#define V_PTR char * const
#define PTR char*
#define ASYNC 1
#define QSIZE (1024)
#define QDEPTH (128)
#define THREADSIZE (1)

#define THPOOL
#define NUM_THREAD 64

#define TCP 1
//#define IP "10.42.0.2"
//#define IP "127.0.0.1"
//#define IP "10.42.0.1"
#define IP "192.168.0.1"
#define PORT 9999
#define NETWORKSET
#define DATATRANS

#define KEYGEN
#define SPINSYNC
#define interface_pq

#ifndef __GNUG__
typedef enum{false,true} bool;
#endif

#endif
