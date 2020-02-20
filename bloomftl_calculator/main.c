#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>


#define K 1024
#define M K*1024
#define G M*1024L


#define PAGESIZE 4*K
#define SUPERBLK_SIZE 4
#define PPB 1024


#define LNP DEVICE/PAGESIZE




int main(int argc, char **argv){

	int k = 1;
	int pps;
	
	uint32_t lnp = 16L*1024*1024*1024/4096;
	/*
	int nop;
	int mask = ceil((pps * (1-0.25)));
	int nob = (lnp / mask);

	pps = pps - mask;
	nob = (nob+1) * SUPERBLK_SIZE;
	nop = nob * PPB;
	*/


	double fpr_start, fpr_end, i_range;
	double pr_ratio, fpr_ratio = 0.0;
	double real_fpr, memory = 0.0;
	int32_t bf_bits, per_bits;
	int32_t check_bits = -1;
	int32_t p_check;


	printf("---------- Calculator for BloomFTL ----------\n");
	while(1){
		
		printf("Page per superblock : ");
		scanf("%d",&pps);
		printf("FPR start setup (0~1) : ");
		scanf("%lf",&fpr_start);
		printf("FPR end setup (0~1) : ");
		scanf("%lf",&fpr_end);
		printf("Increment range (0~1) : ");
		scanf("%lf",&i_range);
		if(fpr_start < 0 || fpr_start > 1 || fpr_end < 0 || fpr_end > 1){
			printf("FPR Setup error! Program restart!");
			continue;
		}

		for(double i = fpr_start ; i < fpr_end; i = i + i_range){
			pr_ratio = i;
			fpr_ratio = (double) pr_ratio*2/(pps+1);
			real_fpr = 1 + (pow(1-fpr_ratio, pps)-1) / (pps * fpr_ratio);
			bf_bits = ceil(-1 / (log(1-pow(fpr_ratio,1/1)) / log(exp(1.0))));
			per_bits = ceil(log(bf_bits) / log(2));	
			memory = (double) (per_bits * lnp)/8/1024/1024;
			printf("FPR : %.2lf BITS : %d memory : %.2lf percentage : %.2lf\n", real_fpr, per_bits, memory, (double) memory/16);
		}


		printf("exit(0) : ");
		scanf("%d",&p_check);
		if(!p_check) break;



	}

	printf("Calculator Exit!\n");

	return 0;
}

