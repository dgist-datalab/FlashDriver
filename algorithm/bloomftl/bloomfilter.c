#include "bloomftl.h"


BF* bf_init(int entry, int ppb){

#if !SYMMETRIC
	double true_p = 0.0, false_p = 0.0;
#endif
	uint32_t bf_bits, s_bits;
	uint32_t total_s_bits = 0; 
	BF *res = (BF *)malloc(sizeof(BF));
	res->base_bf = (BF_INFO *)malloc(sizeof(BF_INFO) * ppb);
	memset(res->base_bf, 0, sizeof(BF_INFO) * ppb);

	res->n = entry;
	res->k = 1;

	for(int i = 0 ; i < ppb; i++){
		/*
		if(i == 0){
			res->base_bf[i].bf_bits = 0;
			res->base_bf[i].s_bits = 0;
			continue;
		}
		*/
#if SYMMETRIC	
		//SYMMETRIC
		res->p = (double)0.1*2/(ppb+1);
#else
		//ASYMMETIC
		true_p = pow(PR_SUCCESS, (double)1/p);
		false_p = 1 - true_p;
		res->p = false_p;
#endif
		res->m = ceil(-1 / (log(1-pow(res->p,1/1)) / log(exp(1.0))));
		bf_bits = res->m;
	
		
//		printf("bf_bits : %d\n",bf_bits);

		//Set BF bits and symbol bits
		
		res->base_bf[i].bf_bits = bf_bits;
		
		s_bits = ceil(log(bf_bits) / log(2));
//		printf("s_bits : %d\n",s_bits);

//		sleep(1);
		res->base_bf[i].s_bits = s_bits;

		total_s_bits += s_bits;
	}

	res->base_s_bits = total_s_bits;
	res->base_s_bytes = bf_bytes(total_s_bits);

	return res;

}


void bf_free(BF *bf){
	free(bf->base_bf);
	free(bf);
}


uint32_t bf_bytes(uint32_t bits){
	uint32_t bytes = 0;
	bytes = bits / 8;
	if(bits % 8)
		bytes++;
	return bytes;
}










