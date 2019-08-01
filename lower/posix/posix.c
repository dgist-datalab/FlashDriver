
#define _LARGEFILE64_SOURCE
#include "posix.h"
#include "../../include/settings.h"
#include "../../bench/bench.h"
#include "../../bench/measurement.h"
#include "../../algorithm/Lsmtree/lsmtree.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <limits.h>

static int _fd;
pthread_mutex_t fd_lock;
lower_info my_posix={
	.create=posix_create,
	.destroy=posix_destroy,
	.write=posix_push_data,
	.read=posix_pull_data,
	.device_badblock_checker=NULL,
	.trim_block=posix_trim_block,
	.refresh=posix_refresh,
	.stop=posix_stop,
	.lower_alloc=NULL,
	.lower_free=NULL,
	.lower_flying_req_wait=posix_flying_req_wait
};

uint32_t posix_create(lower_info *li){
	li->NOB=_NOS;
	li->NOP=_NOP;
	li->SOB=BLOCKSIZE * BPS;
	li->SOP=PAGESIZE;
	li->SOK=sizeof(KEYT);
	li->PPB=_PPB;
	li->PPS=_PPS;
	li->TS=TOTALSIZE;

	li->write_op=li->read_op=li->trim_op=0;
	_fd=open("data/simulator.data",O_RDWR|O_CREAT|O_TRUNC,0666);
//	_fd=open("/dev/robusta",O_RDWR|O_DIRECT|O_DIRECT,0666);
	if(_fd==-1){
		printf("file open error%d!\n",errno);
		exit(-1);
	}
	pthread_mutex_init(&fd_lock,NULL);
	pthread_mutex_init(&my_posix.lower_lock,NULL);
	measure_init(&li->writeTime);
	measure_init(&li->readTime);
	return 1;
}

void *posix_refresh(lower_info *li){
	measure_init(&li->writeTime);
	measure_init(&li->readTime);
	li->write_op=li->read_op=li->trim_op=0;
	return NULL;
}

void *posix_destroy(lower_info *li){

	printf("TRIM\t%lu\n", li->req_type_cnt[0]);
	printf("TR\t%lu\n", li->req_type_cnt[5]);
	printf("TW\t%lu\n", li->req_type_cnt[6]);
	printf("TGCR\t%lu\n", li->req_type_cnt[7]);
	printf("TGCW\t%lu\n", li->req_type_cnt[8]);
	printf("DR\t%lu\n", li->req_type_cnt[1]);
	printf("DW\t%lu\n", li->req_type_cnt[2]);
	printf("DGCR\t%lu\n", li->req_type_cnt[3]);
	printf("DGCW\t%lu\n\n", li->req_type_cnt[4]);

	printf("Total Read Traffic : %lu\n", li->req_type_cnt[1]+li->req_type_cnt[3]+li->req_type_cnt[5]+li->req_type_cnt[7]);
	printf("Total Write Traffic: %lu\n\n", li->req_type_cnt[2]+li->req_type_cnt[4]+li->req_type_cnt[6]+li->req_type_cnt[8]);
	printf("Total WAF: %.2f\n\n", (float)(li->req_type_cnt[2]+li->req_type_cnt[4]+li->req_type_cnt[6]+li->req_type_cnt[8]) / li->req_type_cnt[6]);
	printf("Total RAF: %.2f\n\n", (float)(li->req_type_cnt[1]+li->req_type_cnt[3]+li->req_type_cnt[5]+li->req_type_cnt[7]) / li->req_type_cnt[5]);

	pthread_mutex_destroy(&my_posix.lower_lock);
	pthread_mutex_destroy(&fd_lock);
	close(_fd);
	return NULL;
}
static uint8_t convert_type(uint8_t type) {
        return (type & (0x7f));
}


void *posix_push_data(KEYT PPA, uint32_t size, value_set* value, bool async,algo_req *const req){
	/*
	if(PPA>6500)
		printf("PPA : %u\n", PPA);
	*/

	uint8_t test_type;
	if(value->dmatag==-1){
		printf("dmatag -1 error!\n");
		exit(1);
	}
	bench_lower_w_start(&my_posix);
	if(req->parents)
		bench_lower_start(req->parents);
	pthread_mutex_lock(&fd_lock);

	//if(((lsm_params*)req->params)->lsm_type!=5){
	if(lseek64(_fd,((off64_t)my_posix.SOP)*PPA,SEEK_SET)==-1){
		printf("lseek error in write\n");
	}//

	if(!write(_fd,value->value,size)){
		printf("write none!\n");
	}else{
	//	if(req->parents->key == 1805458){
	}
//	}
	pthread_mutex_unlock(&fd_lock);
	  test_type = convert_type(req->type);
        if(test_type < LREQ_TYPE_NUM){
                my_posix.req_type_cnt[test_type]++;
        }


	if(req->parents)
		bench_lower_end(req->parents);
	bench_lower_w_end(&my_posix);
	req->end_req(req);
/*
	if(async){
		req->end_req(req);
	}else{
	
	}
	*/
	return NULL;
}

void *posix_pull_data(KEYT PPA, uint32_t size, value_set* value, bool async, algo_req *const req){	
	/*
	if(PPA>6500)
		printf("PPA : %u\n", PPA);
	*/
	uint8_t test_type;
	if(value->dmatag==-1){
		printf("dmatag -1 error!\n");
		exit(1);
	}
	bench_lower_r_start(&my_posix);
	if(req->parents)
		bench_lower_start(req->parents);

	pthread_mutex_lock(&fd_lock);
	//if(((lsm_params*)req->params)->lsm_type!=4){
	if(lseek64(_fd,((off64_t)my_posix.SOP)*PPA,SEEK_SET)==-1){
		printf("lseek error in read\n");
	}
	int res;
	if(!(res=read(_fd,value->value,size))){
		printf("LOWER - LPA : %d PPA: %d SEQ: %d\n",req->parents->key, PPA,req->parents->seq);
		printf("%d:read none!\n",res);
	}
	//}
	pthread_mutex_unlock(&fd_lock);
	test_type = convert_type(req->type);
	if(test_type < LREQ_TYPE_NUM){
		my_posix.req_type_cnt[test_type]++;
	}

	if(req->parents)
		bench_lower_end(req->parents);
	bench_lower_r_end(&my_posix);
	req->end_req(req);
	/*
	if(async){
		req->end_req(req);
	}
	else{
	
	}*/
	return NULL;
}

void *posix_trim_block(KEYT PPA, bool async){
	//ench_lower_t(&my_posix);
	char *temp=(char *)malloc(my_posix.SOB);
	memset(temp,0,my_posix.SOB);
	pthread_mutex_lock(&fd_lock);
	if(lseek64(_fd,((off64_t)my_posix.SOP)*PPA,SEEK_SET)==-1){
		printf("lseek error in trim\n");
	}
	my_posix.req_type_cnt[TRIM]++;
	if(!write(_fd,temp,BLOCKSIZE*BPS)){
		printf("write none\n");
	}
	pthread_mutex_unlock(&fd_lock);
	
	free(temp);
	return NULL;
}

void posix_stop(){}

void posix_flying_req_wait(){
	return ;
}
