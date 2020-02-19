#include "bloomftl.h"


BF* bf_init(int entry, int ppb){

#if !SYMMETRIC
	double true_p = 0.0, false_p = 0.0;
#endif
	uint32_t bf_bits, s_bits;
	BF *res = (BF *)malloc(sizeof(BF));

	res->n = entry;
	res->k = 1;


#if SYMMETRIC	
	//SYMMETRIC
	res->p = (double)0.1*2/(ppb+1);
#else
	//ASYMMETIC
	true_p = pow(PR_SUCCESS, (double)1/p);
	false_p = 1 - true_p;
	res->p = false_p;
#endif

	/* Calculate real BF bit length for a superblock */
	res->m = ceil(-1 / (log(1-pow(res->p,1/1)) / log(exp(1.0))));
	bf_bits = res->m;  

	/* Calculate symblozed BF bit length for a superblock */
	s_bits = ceil(log(bf_bits) / log(2));
	res->bits_per_entry = s_bits;

	for(int i = 0 ; i < ppb; i++){		
		res->total_s_bits += s_bits;
	}

	res->total_s_bytes = bf_bytes(res->total_s_bits);

	return res;

}


void bf_free(BF *bf){
	free(bf);
}


uint32_t bf_bytes(uint32_t bits){
	uint32_t bytes = 0;
	bytes = bits / 8;
	if(bits % 8)
		bytes++;
	return bytes;
}










