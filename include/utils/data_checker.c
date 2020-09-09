#include "../settings.h"
#include "../sem_lock.h"
#include "randomsequence.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <random>

uint32_t *keymap;
static uint32_t my_seed;
static fdriver_lock_t data_check_lock;
static uint32_t max_size;
uint32_t test_key=9;

int str2int(const char* str, int len)
{
    int i;
    int ret = 0;
    for(i = 0; i < len; ++i)
    {
        ret = ret * 10 + (str[i] - '0');
    }
    return ret;
}

void __checking_data_init(){
	max_size=SHOWINGSIZE/DEFVALUESIZE+1;
	keymap=(uint32_t *)malloc(sizeof(uint32_t)*max_size);
	fdriver_mutex_init(&data_check_lock);
}

void __checking_data_make(uint32_t key, char *data){
	keymap[key]=my_seed;
	fdriver_lock(&data_check_lock);
	RandomSequenceOfUnique rng(my_seed, my_seed);
	for(uint32_t i=0; i<1024; i++){
		uint32_t t=rng.next();
		memcpy(&data[i*sizeof(uint32_t)], &t, sizeof(uint32_t));
	}
	my_seed++;
	fdriver_unlock(&data_check_lock);
}

void __checking_data_make_key(KEYT _key, char *data, uint32_t length){
	uint32_t key=str2int(_key.key, _key.len);
	if(key>max_size){
		printf("it is over max_size %d(%d)\n", key, max_size);
		abort();
	}
	fdriver_lock(&data_check_lock);
	keymap[key]=my_seed;
	if(key==test_key){
		printf("target populate - seed:%u\n debuging code\n",keymap[key]);
	}
	RandomSequenceOfUnique rng(my_seed, my_seed);
	for(uint32_t i=0; i<length/sizeof(uint32_t); i++){
		uint32_t t=rng.next();
		memcpy(&data[i*sizeof(uint32_t)], &t, sizeof(uint32_t));
		if(key==test_key && i<10){
			printf("%u ", t);
		}
	}
	if(key==test_key){
		printf("\ndata print\n");
	}
	my_seed++;
	fdriver_unlock(&data_check_lock);
}

bool __checking_data_check_key(KEYT _key, char *data, uint32_t length){
	uint32_t key=str2int(_key.key, _key.len);
	fdriver_lock(&data_check_lock);
	uint32_t test_seed=keymap[key];
	uint32_t t;
	RandomSequenceOfUnique rng(test_seed, test_seed);
	if(key==test_key){
		printf("target check - seed: %u debugin code\n", keymap[key]);
	}
	for(uint32_t i=0; i<length/sizeof(uint32_t); i++){
		memcpy(&t, &data[i*sizeof(uint32_t)], sizeof(uint32_t));
		uint32_t tt=rng.next();
		if(key==test_key && i<10){
			printf("%u %u (read : org)\n", t, tt);
		}
		if(tt != t){
			printf("data miss!!!!\n");
			abort();
		}
	}
	fdriver_unlock(&data_check_lock);
	return true;
}

bool __checking_data_check(uint32_t key, char *data){
	uint32_t test_seed=keymap[key];
	fdriver_lock(&data_check_lock);
	uint32_t t;
	RandomSequenceOfUnique rng(test_seed, test_seed);
	for(uint32_t i=0; i<1024; i++){
		memcpy(&t, &data[i*sizeof(uint32_t)], sizeof(uint32_t));
		if(rng.next() != t){
			printf("data miss!!!!\n");
			abort();
		}
	}
	fdriver_unlock(&data_check_lock);
	return true;
}

void __checking_data_free(){
	free(keymap);
	fdriver_destroy(&data_check_lock);
}
