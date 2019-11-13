#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <math.h>
#include "../include/lsm_settings.h"
#include "../include/FS.h"
#include "../include/settings.h"
#include "../include/types.h"
#include "../bench/bench.h"
#include "../bench/measurement.h"
#include "interface.h"
#define LOAD_FILE TPC_C_W_16
#define RUN_FILE  TPC_C_BENCH_16

#define LOAD_CYCLE 1
#define RUN_CYCLE 1

#define BLK_NUM 8
MeasureTime *bt;
MeasureTime *st;
float total_sec;
int32_t req_t_cnt;
int32_t write_cnt; 
int32_t read_cnt;
int32_t real_w_cnt;
int32_t real_r_cnt;
/*
void log_print(int sig){
 	if (total_sec) {
	printf("\ntotal sec: %.2f\n", total_sec);
	printf("read throughput: %.2fMB/s\n", (float)read_cnt*8192/total_sec/1000/1000);
	printf("write throughput: %.2fMB/s\n\n", (float)write_cnt*8192/total_sec/1000/1000);
	}

	inf_free();
	exit(1);
}
*/
int main(int argc, char *argv[]) {
 	int len;
	int8_t fs_type;
	unsigned long long int offset;
	FILE *w_fp;
	FILE *r_fp;
	char command[2];
	char type[5];
	double cal_len;
	struct sigaction sa;
	uint64_t c_number = 0;

	//sa.sa_handler = log_print;
	//sigaction(SIGINT, &sa, NULL);
	bt = (MeasureTime *)malloc(sizeof(MeasureTime));
	st = (MeasureTime *)malloc(sizeof(MeasureTime));
	measure_init(bt);
	measure_init(st);

	inf_init();
	bench_init();
	bench_add(NOR,0,-1,-1,0);
	char t_value[PAGESIZE];
	memset(t_value, 'x', PAGESIZE);
	/*	
	value_set dummy;
	dummy.value=t_value;
	dummy.dmatag=-1;
	dummy.length=PAGESIZE;
	*/
	printf("%s load start!\n",LOAD_FILE);
	w_fp = fopen(LOAD_FILE, "r");
	if (w_fp == NULL) {
		printf("No file\n");
		return 1;
	}

	//Set sequential write for GC
/*		
	int32_t set_range = RANGE * 0.7;
	for(int i = 0 ; i < set_range; i++){
		inf_make_req(FS_SET_T, i, t_value, PAGESIZE, 0);
	}
  */  
//	int32_t cnt = 0;
	for(int i = 0; i < LOAD_CYCLE; i++){
		printf("LOAD CYCLE : %d\n",i);
		while (fscanf(w_fp, "%s %s %llu %lf", command, type, &offset, &cal_len) != EOF) {

			if(command[0] == 'D'){
				offset = offset / BLK_NUM;
				len = ceil(cal_len / BLK_NUM);
				if(offset + len > RANGE){
					continue;
				}
				if(type[0] == 'R'){
					fs_type = FS_GET_T;
				}else{
					fs_type = FS_SET_T;
				}
			}else{
				continue;
			}

			for (int i = 0; i < len; i++) {

				inf_make_req(fs_type, offset+i, t_value, PAGESIZE, 0);
				if (fs_type == FS_SET_T) {
					write_cnt++;
				}

				else if (fs_type == FS_GET_T) {
					read_cnt++;
				}

				/*if (++cnt % 10240 == 0) {
				  printf("cnt -- %d\n", cnt);
				  printf("%d %llu %d\n", rw, offset, len);
				  }*/
			}
			memset(command,0,sizeof(char) * 2);
			memset(type,0,sizeof(char)*5);
			fflush(stdout);
		}
		fseek(w_fp,0,SEEK_SET);
	}
	printf("%s load complete!\n\n",LOAD_FILE);
	printf("Load write : %d\n",write_cnt);
	printf("Load read  : %d\n",read_cnt);
	
	
	fclose(w_fp);
	printf("%s bench start!\n", RUN_FILE);
	r_fp = fopen(RUN_FILE, "r");
#if !FILEBENCH_SET
	if (r_fp == NULL) {
		printf("No file\n");
		return 1;
	}
//	trace_cdf = (uint64_t *)malloc(sizeof(uint64_t) * (1000000/TIMESLOT)+1);
//	memset(trace_cdf, 0, sizeof(uint64_t) * ((1000000/TIMESLOT)+1));
	measure_start(bt);	
//	cnt = 0;

	for(int i = 0 ; i < RUN_CYCLE; i++){
		printf("RUN_CYCLE= %d\n",i);
		while (fscanf(r_fp, "%s %s %llu %lf", command, type, &offset, &cal_len) != EOF) {

			if(command[0] == 'D'){
				offset = offset / BLK_NUM;
				len = ceil(cal_len / BLK_NUM);
				if(offset + len > RANGE){
					continue;
				}
				if(type[0] == 'R'){
					fs_type = FS_GET_T;
				}else{
					fs_type = FS_SET_T;
				}
			}else{
				continue;
			}
			for (int i = 0; i < len; i++) {

				inf_make_req(fs_type, offset+i, t_value, PAGESIZE, 1);
				if (fs_type == FS_SET_T) {

					write_cnt++;
					real_w_cnt++;
				} else if (fs_type == FS_GET_T) {
					read_cnt++;
					real_r_cnt++;
				}

				/*
				   if (++cnt % 10240 == 0) {
				   MA(&st);
				   total_sec = st.adding.tv_sec + (float)st.adding.tv_usec/1000000;
				   printf("\ntotal sec: %.2f\n", total_sec);
				   printf("read throughput: %.2fMB/s\n", (float)read_cnt*8192/total_sec/1000/1000);
				   printf("write throughput: %.2fMB/s\n\n", (float)write_cnt*8192/total_sec/1000/1000);
				   MS(&st);
				   }
				 */
			}
			memset(command,0,sizeof(char) * 2);
			memset(type,0,sizeof(char)*5);
			fflush(stdout);

		}

		fseek(r_fp,0,SEEK_SET);
	}
	measure_adding(bt);
	printf("%s bench complete!\n",RUN_FILE);
	fclose(r_fp);
#endif
	total_sec = bt->adding.tv_sec + (float)bt->adding.tv_usec/1000000;
	req_t_cnt = real_w_cnt + real_r_cnt;
	printf("--------------- summary ------------------\n");
	printf("-----------  bench run result  -----------\n");
	printf("total_cnt = %d\n",req_t_cnt);
	printf("\ntotal sec: %.2f\n", total_sec);
	printf("read_cnt : %d write_cnt : %d\n",real_r_cnt, real_w_cnt);
	printf("read throughput: %.2fMB/s\n", (float)real_r_cnt*8192/total_sec/1000/1000);
	printf("write throughput: %.2fMB/s\n", (float)real_w_cnt*8192/total_sec/1000/1000);
	printf("total_throughput: %.2fMB/s\n",(float)req_t_cnt*8192/total_sec/1000/1000);
	printf("CDF for bench trace!\n");
	for(int i = 0; i < 1000000/TIMESLOT+1; i++){
		c_number += trace_cdf[i];
		if(trace_cdf[i]==0) continue;
		printf("%d\t%ld\t%f\n",i * 10, trace_cdf[i], (float)c_number/real_r_cnt);
		if(real_r_cnt == c_number)
			break;
	}
	free(bt);
	free(st);
	sleep(5);
	inf_free();

	return 0;
}

