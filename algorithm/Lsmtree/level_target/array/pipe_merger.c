#include "array.h"
#include "pipe.h"
#include "../../../../include/settings.h"
#include "../../../../bench/bench.h"
#include "../../../../include/utils/thpool.h"
#include "../../compaction.h"
#include "../../nocpy.h"
#include "../../bitmap_cache.h"
#include "mapping_utils.h"
#include "../../../../interface/koo_hg_inf.h"
extern MeasureTime write_opt_time2[10];
p_body *rp;
char **r_data;
static bool cutter_start;
#ifdef BLOOM
float t_fpr;
#endif
extern _bc bc;
extern lsmtree LSM;
char *array_skip_cvt2_data(skiplist *mem){
	char *res=(char*)malloc(PAGESIZE);
	uint16_t *bitmap=(uint16_t *)res;
	uint32_t idx=1;
	uint16_t data_start=KEYBITMAP;
	snode *temp;
	char *now_ptr;
	uint16_t added_length=0;
	bitmap[0]=mem->size;
	for_each_sk(mem,temp){
		added_length=0;
		now_ptr=&res[data_start];
#ifdef META_UNSEP
		if(temp->ppa!=TOMBSTONE && temp->key.key[0]=='m'){
			now_ptr[added_length++]=KVUNSEP;
			memcpy(&now_ptr[added_length], &temp->value.u_value->length, sizeof(uint32_t));
			added_length+=sizeof(uint32_t);
			memcpy(&now_ptr[added_length], temp->value.u_value->value, temp->value.u_value->length);
			added_length+=temp->value.u_value->length;
		}
		else
#endif
		{
			now_ptr[added_length++]=KVSEP;
			memcpy(&now_ptr[added_length],&temp->ppa,sizeof(temp->ppa));
			added_length+=sizeof(temp->ppa);
		}
		memcpy(&now_ptr[added_length],temp->key.key,temp->key.len);
		added_length+=temp->key.len;

		bitmap[idx]=data_start;
		data_start+=added_length;
		idx++;
	}
	bitmap[idx]=data_start;
	return res;
}
void temp_func(char* body, level *d, bool insert){
	int idx;
	uint16_t *bitmap=(uint16_t*)body;
	p_entry pent, prev_pent;
	for_each_header_start(idx,pent,bitmap,body)
		if(idx==1) prev_pent=pent;
		else{
			if(KEYCMP(prev_pent.key, pent.key) >=0 ){
				if(insert){
					printf("insert order error!!\n");
				}
				else{
					printf("cutter order error!!\n");
				}
				abort();
			}
			prev_pent=pent;
		}

	/*
		if(KEYCONSTCOMP(pent.key, "m0000000000004134313")==0){
			if(insert)
				printf("[%.*s] insert into %d\n",KEYFORMAT(pent.key), d->idx);
			else{
				printf("[%.*s] cutter %d\n",KEYFORMAT(pent.key), d->idx);
			}	
		}*/
	for_each_header_end
}

extern uint32_t debugging_ppa;
void array_pipe_merger(struct skiplist* mem, run_t** s, run_t** o, struct level* d){
	static int cnt=0;
	//printf("cnt:%d\n", cnt++);
	bool debug=false;
	if(cnt==6399){
//		debug=true;
	}
	cutter_start=true;
	int o_num=0; int u_num=0;
	char **u_data;
#ifdef BLOOM
	t_fpr=d->fpr;
#endif
	if(mem){
		u_num=1;
		u_data=(char**)malloc(sizeof(char*)*u_num);
		u_data[0]=array_skip_cvt2_data(mem);
		//temp_func(u_data[0],d,true);
	}
	else{
		for(int i=0; s[i]!=NULL; i++) u_num++;
		u_data=(char**)malloc(sizeof(char*)*u_num);
		for(int i=0; i<u_num; i++) {
			u_data[i]=data_from_run(s[i]);
			if(!u_data[i]) abort();
		//	temp_func(u_data[i],d,true);
		}
	}

	for(int i=0;o[i]!=NULL ;i++) o_num++;
	char **o_data=(char**)malloc(sizeof(char*)*o_num);
	for(int i=0; o[i]!=NULL; i++){ 
		o_data[i]=data_from_run(o[i]);
		if(!o_data[i]) abort();
		//temp_func(o_data[i],d,true);
	}


	if(d->idx==LSM.LEVELN-1 && !bc.full_caching){
		bc_reset();
	}


	r_data=(char**)calloc(sizeof(char*),(o_num+u_num+LSM.result_padding));
	p_body *lp, *hp;
	lp=pbody_init(o_data,o_num,NULL,false,NULL);
	hp=pbody_init(u_data,u_num,NULL,false,NULL);
#ifdef BLOOM
	rp=pbody_init(r_data,o_num+u_num+LSM.result_padding,NULL,false,d->filter);
#else
	rp=pbody_init(r_data,o_num+u_num+LSM.result_padding,NULL,false,NULL);
#endif

/*	uint32_t lppa, hppa, rppa=0;
	KEYT lp_key=pbody_get_next_key(lp,&lppa);
	KEYT hp_key=pbody_get_next_key(hp,&hppa);*/

	p_entry lpentry, hpentry, rpentry;
	lpentry=pbody_get_next_pentry(lp);
	hpentry=pbody_get_next_pentry(hp);
	int next_pop=0;
	int result_cnt=0;

	while(!(lpentry.key.len==UINT8_MAX && hpentry.key.len==UINT8_MAX)){
		if(lpentry.key.len==UINT8_MAX){
			rpentry=hpentry;
			next_pop=1;
		}
		else if(hpentry.key.len==UINT8_MAX){
			rpentry=lpentry;
			next_pop=-1;
		}
		else{
			if(!KEYVALCHECK(lpentry.key)){
				printf("%.*s\n",KEYFORMAT(lpentry.key));
				abort();
			}
			if(!KEYVALCHECK(hpentry.key)){
				printf("%.*s\n",KEYFORMAT(hpentry.key));
				abort();
			}

			next_pop=KEYCMP(lpentry.key,hpentry.key);
			if(next_pop<0){
				rpentry=lpentry;
			}
			else if(next_pop>0){
				rpentry=hpentry;
			}
			else{
				rpentry=hpentry;
				switch(lpentry.type){
					case KVSEP:
						invalidate_PPA(DATA,lpentry.info.ppa, d->idx);
						break;
					case KVUNSEP:
						break;
					default:
						printf("unknown type %s:%d\n", __FILE__, __LINE__);
						abort();
						break;

				}
			}
		}
		/*
		if(KEYCONSTCOMP(insert_key,"215155000000")==0){
			printf("----real insert into %d\n",d->idx);
		}*/
		if(d->idx==LSM.LEVELN-1 && !bc.full_caching){
			if(rpentry.type==KVSEP){
				bc_set_validate(rpentry.info.ppa);
			}
		}
/*
		if(debug){
			char buf[100], buf2[100], buf3[100];
			key_interpreter(rpentry.key, buf);
			key_interpreter(lpentry.key, buf2);
			key_interpreter(hpentry.key, buf3);
			static int insert_cnt=0;
			if(insert_cnt++>1869300){
				printf("%d key:%s l-key:%s, h-key:%s\n", insert_cnt++, buf, buf2, buf3);
			}
		}
*/
		if(d->idx==LSM.LEVELN-1 && (rpentry.type==KVSEP && rpentry.info.ppa==TOMBSTONE)){
			//printf("ignore key\n");
		}
		else if((pbody_insert_new_pentry(rp,rpentry,false))){
			result_cnt++;
		}

		if(next_pop<0) lpentry=pbody_get_next_pentry(lp);
		else if(next_pop>0) hpentry=pbody_get_next_pentry(hp);
		else{
			lpentry=pbody_get_next_pentry(lp);
			hpentry=pbody_get_next_pentry(hp);
		}
	}
	if(d->idx==LSM.LEVELN-1 && !bc.full_caching){
		if(rpentry.type==KVSEP){
			bc_set_validate(rpentry.info.ppa);
		}
	}

	if((pbody_insert_new_pentry(rp,rpentry,true))){
		result_cnt++;
	}

	if(mem) free(u_data[0]);
	free(o_data);
	free(u_data);
	pbody_clear(lp);
	pbody_clear(hp);
	if(d->idx==LSM.LEVELN-1){
		bc_clear_ignore_flag();
	}
}

run_t *array_pipe_make_run(char *data,uint32_t level_idx)
{
	KEYT start,end;
	uint16_t *body=(uint16_t*)data;
	//uint32_t num=body[0];

	array_find_first_key(data, &start);
	array_find_last_key(data, &end);
/*
	start.len=body[2]-body[1]-sizeof(ppa_t);
	start.key=&data[body[1]+sizeof(ppa_t)];

	end.len=body[num+1]-body[num]-sizeof(ppa_t);
	end.key=&data[body[num]+sizeof(ppa_t)];
*/	
	run_t *r=array_make_run(start,end,-1);

	if(level_idx<LSM.LEVELCACHING){
		r->level_caching_data=data;
	}
	else{
		htable *res=(ISNOCPY(LSM.setup_values))?htable_assign(data,0):htable_assign(data,1);
		r->cpt_data=res;
		free(data);
	}
	return r;
}

run_t *array_pipe_cutter(struct skiplist* mem, struct level* d, KEYT* _start, KEYT *_end){
	char *data;
	if(cutter_start){
		cutter_start=false;
		data=pbody_get_data(rp,true);
	}
	else{
		data=pbody_get_data(rp,false);
	}
	if(!data) {
		free(r_data);
		pbody_clear(rp);
		return NULL;
	}
	//temp_func(data,d,false);

	return array_pipe_make_run(data,d->idx);
}

//run_t *array_pipe_p_merger_cutter(skiplist *skip, pl_run *u_data, pl_run* l_data, uint32_t u_num, uint32_t l_num,level *d, void *(*lev_insert_write)(level *,run_t *data)){
//	printf("is this called?? %s:%d\n", __FILE__, __LINE__);
//	abort();
//	char *skip_data;
//	if(skip){
//		u_num=1;
//		u_data=(pl_run*)malloc(sizeof(pl_run));
//		u_data[0].lock=(fdriver_lock_t*)malloc(sizeof(fdriver_lock_t));
//		fdriver_lock_init(u_data[0].lock,1);
//		skip_data=array_skip_cvt2_data(skip);
//		u_data[0].r=array_pipe_make_run(skip_data,-1);
//	}
//
//	p_body *lp, *hp, *p_rp;
//	char **r_datas=(char**)calloc(sizeof(char*),(u_num+l_num+LSM.result_padding));
//	lp=pbody_init(NULL,l_num,l_data,true,NULL);
//	hp=pbody_init(NULL,u_num,u_data,true,NULL);
//	p_rp=pbody_init(r_datas,u_num+l_num+LSM.result_padding,NULL,false,NULL);
//	
//	int result_cnt=0;
//	int flushed_idx=0;
//	run_t **result_temp=(run_t**)malloc(sizeof(run_t*)*(u_num+l_num+LSM.result_padding));
//
//	uint32_t lppa, hppa, p_rppa;
//	KEYT lp_key=pbody_get_next_key(lp,&lppa);
//	KEYT hp_key=pbody_get_next_key(hp,&hppa);
//	KEYT insert_key;
//	int next_pop=0;
//	char *res_data;
//	while(!(lp_key.len==UINT8_MAX && hp_key.len==UINT8_MAX)){
//		if(lp_key.len==UINT8_MAX){
//			insert_key=hp_key;
//			p_rppa=hppa;
//			next_pop=1;
//		}
//		else if(hp_key.len==UINT8_MAX){
//			insert_key=lp_key;
//			p_rppa=lppa;
//			next_pop=-1;
//		}
//		else{
//			if(!KEYVALCHECK(lp_key)){
//				printf("%.*s\n",KEYFORMAT(lp_key));
//				abort();
//			}
//			if(!KEYVALCHECK(hp_key)){
//				printf("%.*s\n",KEYFORMAT(hp_key));
//				abort();
//			}
//			next_pop=KEYCMP(lp_key,hp_key);
//			if(next_pop<0){
//				insert_key=lp_key;
//				p_rppa=lppa;
//			}
//			else if(next_pop>0){
//				insert_key=hp_key;
//				p_rppa=hppa;
//			}
//			else{
//				invalidate_PPA(DATA,lppa,d->idx);
//				p_rppa=hppa;
//				insert_key=hp_key;
//			}
//		}
//
//		if((res_data=pbody_insert_new_key(p_rp,insert_key,p_rppa,false)))
//		{
//			/*
//#ifdef BOOM
//			result_temp[result_cnt++]=array_pipe_make_run(res_data,d->idx,filter);
//#else
//			result_temp[result_cnt++]=array_pipe_make_run(res_data,d->idx);
//#endif	*/
//		}
//
//		if(next_pop<0) lp_key=pbody_get_next_key(lp,&lppa);
//		else if(next_pop>0) hp_key=pbody_get_next_key(hp,&hppa);
//		else{
//			lp_key=pbody_get_next_key(lp,&lppa);
//			hp_key=pbody_get_next_key(hp,&hppa);
//		}
//	}
//
//	if((res_data=pbody_insert_new_key(p_rp,insert_key,0,true)))
//	{
//		/*
//#ifdef BLOOM
//		result_temp[result_cnt++]=array_pipe_make_run(res_data,d->idx,filter);
//#else
//		result_temp[result_cnt++]=array_pipe_make_run(res_data,d->idx);
//#endif	*/
//
//	}
//
//
//	res_data=pbody_get_data(p_rp,true);
//	do{
//		if(!res_data) break;
//	//	printf("%d\n",result_cnt);
//		result_temp[result_cnt++]=array_pipe_make_run(res_data,d->idx);
//
//	}
//	while((res_data=pbody_get_data(p_rp,false)));
//	
//	for(int i=flushed_idx; i<result_cnt; i++){
//		lev_insert_write(d,result_temp[i]);
//	}
//
//	if(skip){
//		fdriver_destroy(u_data[0].lock);
//		free(u_data[0].lock);
//		array_free_run(u_data[0].r);
//		htable_free(u_data[0].r->cpt_data);
//		free(u_data[0].r);
//		free(u_data);
//	}
//	free(result_temp);
//	free(r_datas);
//	pbody_clear(p_rp);
//	pbody_clear(lp);
//	pbody_clear(hp);
//	return NULL;
//}
