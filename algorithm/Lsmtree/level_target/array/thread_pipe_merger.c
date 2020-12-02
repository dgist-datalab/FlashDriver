#include "array.h"
#include "pipe.h"
#include "../../../../include/settings.h"
#include "../../../../bench/bench.h"
#include "../../../../include/utils/thpool.h"
#include "../../../../interface/koo_hg_inf.h"
#include "../../compaction.h"
#include "../../nocpy.h"
#include "../../bitmap_cache.h"
#include "mapping_utils.h"
#include <vector>
extern MeasureTime write_opt_time2[10];
std::vector<char*> splited_data_set;
extern lsmtree LSM;
static threadpool pool;
static bool cutter_start;
bool ismulti_thread;
#ifdef BLOOM
extern float t_fpr;
#endif
extern _bc bc;
extern lmi LMI;
bool isclear;

static void temp_func(char* body, level *d, bool merger){
	int idx;
	uint16_t *bitmap=(uint16_t*)body;
	KEYT key;
	//KEYT prev_key;
	//ppa_t *ppa_ptr;
	p_entry pent; 
	for_each_header_start(idx,pent,bitmap,body)
		if(pent.type!=KVSEP && pent.type!=KVUNSEP){
			if(merger)
				printf("insert into %d\n",d->idx);
			else{
				printf("cutter %d\n",d->idx);
			}
			array_header_print(body);
			abort();
		}
	for_each_header_end
}
#ifdef KOO
static void header_start_end_print(char *data, int idx){
	char start[100], end[100];
	key_interpreter(__extract_start_key(data), start);
	key_interpreter(__extract_end_key(data), end);
	printf("%d %s ~ %s\n", idx, start, end);
}
#endif

typedef struct thread_params{
	uint32_t o_num;
	char **u_data;
	char **o_data;
	uint32_t result_return_num;
	uint32_t result_num;
	char **result_data;
	p_body *rp;
	uint32_t idx;
	bool isdone;
	bool isdummy;
	struct level *d;
	struct trivial_move_container *res;
}tp;

static tp *init_thread_params(uint32_t num, char *target, level *d, uint32_t idx){
	tp *res=(tp*)calloc(1,sizeof(tp));
	res->u_data=(char**)malloc(sizeof(char*)*1);
	res->u_data[0]=target;
	res->o_data=(char**)malloc(sizeof(char*)*num);
	res->o_num=0;
	res->result_return_num=0;
	res->result_num=0;
	res->d=d;
	res->idx=idx;
	res->isdummy=false;
	res->isdone=false;
	return res;
}

static tp *init_dummy_thread_params(uint32_t num, char *target, level *d, uint32_t idx){
	tp *res=(tp*)calloc(1,sizeof(tp));
	res->u_data=(char**)malloc(sizeof(char*)*1);
	res->u_data[0]=target;
	res->o_data=NULL;
	res->o_num=0;

	res->result_data=(char**)malloc(sizeof(char*)*num);
	for(uint32_t i=0; i<num; i++){
		res->result_data[i]=(char*)malloc(PAGESIZE);
		memcpy(res->result_data[i], target, PAGESIZE);
	}
	res->rp=pbody_move_dummy_init(res->result_data, num);
	res->result_num=num;

	res->result_return_num=0;
	res->d=d;
	res->idx=idx;
	res->isdummy=true;
	res->isdone=true;
	return res;
}

void tp_print(tp *t){
	if(!t->isdummy){
#ifdef KOO
		char buf[4][100];
		key_interpreter(__extract_start_key(t->u_data[0]), buf[0]);
		key_interpreter(__extract_end_key(t->u_data[0]), buf[1]);
		key_interpreter(__extract_start_key(t->o_data[0]), buf[2]);
		key_interpreter(__extract_end_key(t->o_data[t->o_num-1]), buf[3]);
		printf("[%d] %s ~ %s(1) && %s ~ %s (%d)\n",t->idx,
			buf[0], buf[1], buf[2], buf[3],
			t->o_num
			);
#else
		printf("[%d] %.*s ~ %.*s(1) && %.*s ~ %.*s (%d)\n",t->idx,
			KEYFORMAT(__extract_start_key(t->u_data[0])),
			KEYFORMAT(__extract_end_key(t->u_data[0])),
			KEYFORMAT(__extract_start_key(t->o_data[0])),
			KEYFORMAT(__extract_end_key(t->o_data[t->o_num?t->o_num-1:0])),		
			t->o_num
			);
#endif
	}
	else{
#ifdef KOO
		char buf[4][100];
		key_interpreter(__extract_start_key(t->u_data[0]), buf[0]);
		key_interpreter(__extract_end_key(t->u_data[0]), buf[1]);
		printf("[%d] isdummy %s~%s\n", t->idx, buf[0], buf[1]);
#endif
	}
}

void tp_check_sanity(tp **_t, int num){
	KEYT ps, pe, ns, ne;

	//internal o_data check!!!
	for(int i=0; i<num-1; i++){
		if(_t[i]->isdummy || _t[i+1]->isdummy) continue;
		tp *t=_t[i];
		pe=__extract_end_key(t->o_data[t->o_num-1]);
		t=_t[i+1];
		if(*(uint16_t*)t->o_data[0]==0) continue;
		ns=__extract_start_key(t->o_data[0]);
		if(KEYCMP(pe, ns) >=0){
			printf("sanity failed %s:%d\n", __FILE__, __LINE__);
			abort();
		}
	}
	//external o_data check!!!
	for(int i=0; i<num-1; i++){
		if(_t[i]->isdummy || _t[i+1]->isdummy) continue;
		tp *t=_t[i]; tp *tn=_t[i+1];
		ps=__extract_start_key(t->o_data[0]);
		pe=__extract_start_key(t->o_data[t->o_num-1]);
		ns=__extract_start_key(tn->o_data[0]);
		ne=__extract_start_key(tn->o_data[tn->o_num-1]);

		if(KEYCMP(pe,ns)>=0){
			printf("tpp idx:%d~%d\n", i, i+1);
			printf("sanity failed %s:%d\n", __FILE__, __LINE__);
			abort();	
		}
	}
	//u_data check!!!
	for(int i=0; i<num-1; i++){
		//if(_t[i]->isdummy || _t[i+1]->isdummy) continue;
		tp *t=_t[i]; tp *tn=_t[i+1];
		ps=__extract_start_key(t->u_data[0]);
		pe=__extract_start_key(t->u_data[0]);
		ns=__extract_start_key(tn->u_data[0]);
		ne=__extract_start_key(tn->u_data[0]);

		if(KEYCMP(pe,ns)>=0){
			char buf[2][100];
			key_interpreter(pe, buf[0]);
			key_interpreter(ns, buf[1]);
			printf("tpp idx:%d~%d pe:%s ns:%d\n", i, i+1, buf[0], buf[1]);
			printf("sanity failed %s:%d\n", __FILE__, __LINE__);
			abort();	
		}
	}
}

static void free_thread_params(tp *t){
	free(t->u_data);
	free(t->o_data);
	free(t);
}

void __pipe_merger(void *argument, int id){
	tp *params=(tp*)argument;
	char **o_data=params->o_data;
	char **u_data=params->u_data;
	uint32_t o_num=params->o_num;
	uint32_t u_num=1;
	level *d=params->d;
	//tp_print(params);

	char **tp_r_data=(char**)calloc(sizeof(char*),(o_num+u_num+LSM.result_padding));
	p_body *lp, *hp, *rp;
	lp=pbody_init(o_data,o_num,NULL,false,NULL);
	hp=pbody_init(u_data,u_num,NULL,false,NULL);
#ifdef BLOOM
	rp=pbody_init(tp_r_data,o_num+u_num+LSM.result_padding,NULL,false,d->filter);
#else
	rp=pbody_init(tp_r_data,o_num+u_num+LSM.result_padding,NULL,false,NULL);
#endif

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

		if(next_pop<0) lpentry=pbody_get_next_pentry(lp);
		else if(next_pop>0) hpentry=pbody_get_next_pentry(hp);
		else{
			lpentry=pbody_get_next_pentry(lp);
			hpentry=pbody_get_next_pentry(hp);
		}

		if(d->idx==LSM.LEVELN-1 && (rpentry.type==KVSEP && rpentry.info.ppa==TOMBSTONE)){
			//printf("ignore key\n");
		}
		else if((pbody_insert_new_pentry(rp,rpentry,false))){
			result_cnt++;
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

	pbody_clear(lp);
	pbody_clear(hp);
	params->result_num=result_cnt;
	params->result_data=tp_r_data;
	params->rp=rp;
	params->isdone=true;
}

thread_params **tpp;
int params_idx;
int params_max;

run_t* array_thread_pipe_cutter(struct skiplist *mem, struct level *d, KEYT *_start, KEYT *_end, trivial_move_container **res){
	if(!ismulti_thread){
		if(res)
			*res=NULL;
		return array_pipe_cutter(mem, d, _start,_end);
	}
	char *data;
retry:
	static int KP_cnt=0;
	thread_params *tp=tpp[params_idx];
	if(!tp->isdone){
		thpool_wait(pool);
	}
	p_body *rp=tp->rp;
	if(cutter_start){
		cutter_start=false;
		data=pbody_get_data(rp, true);
		KP_cnt=0;
	}
	else{
		data=pbody_get_data(rp, false);
	}

	if(!data){
		free(tp->result_data);
		pbody_clear(rp);
		if(params_idx < params_max-1){
			params_idx++;
			cutter_start=true;
			goto retry;
		}
		for(int i=0; i<params_max; i++) free_thread_params(tpp[i]);
		for(int i=0; i<splited_data_set.size(); i++){
			free(splited_data_set[i]);
		}
		splited_data_set.clear();
		free(tpp);
		ismulti_thread=false;
		if(!isclear){
			bc_clear_ignore_flag();
		}
		return NULL;
	}
	else{
		//temp_func(data, d, false);
	}
	//array_header_print(data);
	//printf("head %d %dprint, %d %d\n", cnt++, rp->pidx,params_max, params_idx);
	//printf("[%d]KP num:%u move:%d\n",KP_cnt++, __get_KP_pair_num(data), tp->isdummy?1:0);
	return array_pipe_make_run(data,d->idx);
}

static int temp_print_format(KEYT key, int boundary, char *tt){
	char buf[100];
	key_interpreter(key, buf);
	printf("[%s] %s -> %d\n", tt, buf, boundary);
	return 0;
}

void array_thread_pipe_merger(struct skiplist* mem, run_t** s, run_t** o, struct level* d){
	if(mem) return array_pipe_merger(mem, s, o, d);
	ismulti_thread=true;
	isclear=false;
	static bool is_thread_init=false;
	if(!is_thread_init){
		is_thread_init=true;
#ifdef THREADCOMPACTION
		pool=thpool_init(THREADCOMPACTION);
#endif
	}

	cutter_start=true;
	params_idx=0;
	int o_num=0; int u_num=0;
	char **u_data;
#ifdef BLOOM
	t_fpr=d->fpr;
#endif
	bool debug=false;
	/*
	static int cnt=0;
	
	printf("mg cnt:%d\n", cnt++);
	if(cnt==17563+1){
		debug=true;
		printf("break!\n");
	}*/

	for(int i=0; s[i]!=NULL; i++) u_num++;
	u_data=(char**)malloc(sizeof(char*)*u_num);
	if(debug){
		printf("upper print\n");
	}
	for(int i=0; i<u_num; i++) {
		u_data[i]=data_from_run(s[i]);
		if(debug){	
			header_start_end_print(u_data[i], i);
		}
	//	temp_func(u_data[i], d, true);
		if(!u_data[i]) abort();
	}

	if(d->idx==LSM.LEVELN-1 && !bc.full_caching){
		bc_reset();
	}

	for(int i=0;o[i]!=NULL ;i++) o_num++;
	char **o_data=(char**)malloc(sizeof(char*)*o_num);
	if(debug){
		printf("lower print\n");
	}
	for(int i=0; o[i]!=NULL; i++){ 
		o_data[i]=data_from_run(o[i]);
		if(debug){
			header_start_end_print(o_data[i], i);
		}
		//printf("lower:%d\n",i);
		//array_header_print(o_data[i]);

		//temp_func(o_data[i], d, true);
	//	printf("lower %d\n", i);
	//	array_header_print(o_data[i]);
		if(!o_data[i]) abort();
	}

	tpp=(tp**)malloc(sizeof(tp*)*(u_num*2));
	int tp_num=0, t_data_num;
	int prev_consume_num=0;
	int end_boundary;
	int next_boundary=-1, num;
	bool issplit_start=false;
	char **target_data_set;
	char *splited_data=NULL;
	uint32_t j=0, real_bound;
	KEYT now_end_key, next_start_key;
/*
	printf("upper level!\n");
	array_print(LSM.disk[d->idx-1]);
	printf("lower level!\n");
	array_print(LSM.disk[d->idx]);
*/
	for(uint32_t i=0; i<u_num; i++){
		if(debug && (tp_num==5)){
			printf("break!\n");
		}
		if(i==u_num-1){
			end_boundary=o_num-prev_consume_num-1;
			next_boundary=end_boundary+1;
			real_bound=end_boundary+prev_consume_num;
			goto make_params;
		}
		now_end_key=__extract_end_key(u_data[i]);
		next_start_key=__extract_start_key(u_data[i+1]);

		end_boundary=__find_boundary_in_data_list(now_end_key, &o_data[prev_consume_num], o_num-prev_consume_num);
		//temp_print_format(now_end_key, end_boundary, "now_end_key");

		next_boundary=__find_boundary_in_data_list(next_start_key, &o_data[prev_consume_num], o_num-prev_consume_num);
		//temp_print_format(next_start_key, next_boundary, "nxt_start_key");

make_params:
		if(end_boundary==-1){ //-1==no data or before data
			real_bound=prev_consume_num+end_boundary;
			if(splited_data){
				switch(__header_overlap_chk(u_data[i],splited_data)){
					case 1://no overlap: [splited_data] [u_data[i]]
						tpp[tp_num]=init_dummy_thread_params(1, splited_data, d, tp_num);
						if(debug){
							tp_print(tpp[tp_num]);
						}
						tp_num++;
						LMI.move_run_cnt++;
						splited_data=NULL;
						issplit_start=false;
						//should do next step;
					case -1: //no overlap :[u_data[i]] [splited_data]
						num=0;
						tpp[tp_num]=init_dummy_thread_params(1, u_data[i], d, tp_num);
						if(debug){
							tp_print(tpp[tp_num]);
						}
						LMI.move_run_cnt++;
						goto next_round;
					case 0: //overlap splited data
						num=1;
						break;
				}
			}
			else{
				num=0;
				tpp[tp_num]=init_dummy_thread_params(1, u_data[i], d, tp_num);
				if(debug){
					tp_print(tpp[tp_num]);
				}
				LMI.move_run_cnt++;
				goto next_round;
			}
		}
		else{
			real_bound=prev_consume_num+end_boundary;
			num=real_bound-prev_consume_num+(issplit_start?1:0)+1;
		}

		tpp[tp_num]=init_thread_params(num, u_data[i], d, tp_num);
		t_data_num=0;
		target_data_set=tpp[tp_num]->o_data;


		if(issplit_start){
			target_data_set[t_data_num++]=splited_data;
			issplit_start=false;
			//splited_data=NULL;
		}

		if(end_boundary!=-1 && end_boundary==next_boundary){
			//array_header_print(o_data[prev_consume_num+end_boundary]);
			splited_data=__split_data(o_data[prev_consume_num+end_boundary], now_end_key, next_start_key, false);
			if(splited_data){
				splited_data_set.push_back(splited_data);
				issplit_start=true;
			}
		}
		else if(end_boundary==-1 && end_boundary==next_boundary){
			splited_data=__split_data(splited_data, now_end_key, next_start_key, false);
			if(splited_data){
				splited_data_set.push_back(splited_data);
				issplit_start=true;				
			}
		}

		if(end_boundary!=-1){
			for(j=prev_consume_num; j<=real_bound; j++){
				target_data_set[t_data_num++]=o_data[j];
				prev_consume_num++;
			}
		}

		tpp[tp_num]->o_num=t_data_num;
		LMI.compacting_run_cnt+=t_data_num+1;
		if(debug){
			tp_print(tpp[tp_num]);
		}
		thpool_add_work(pool, __pipe_merger, (void*)tpp[tp_num]);
next_round:
		tp_num++;
	}
	
	if(issplit_start){
		tpp[tp_num]=init_dummy_thread_params(1, splited_data, d, tp_num);
		if(debug){
			tp_print(tpp[tp_num]);
		}
		tp_num++;
		LMI.move_run_cnt++;
		issplit_start=false;
	}

	
	if(debug){
		for(int i=0; i<tp_num; i++){
			tp_print(tpp[i]);
		}
	}
	
	//tp_check_sanity(tpp, tp_num);


//	exit(1);
	params_max=tp_num;

	//thpool_wait(pool);

	free(o_data);
	free(u_data);
	if(d->idx==LSM.LEVELN-1){
		if(!thpool_num_threads_working(pool)){
			bc_clear_ignore_flag();
			isclear=true;
		}
	}
}

