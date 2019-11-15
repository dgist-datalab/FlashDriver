#include "../include/FS.h"
#include "../include/container.h"
#include "layer_info.h"
#include "interface.h"
#include "exterior_interface.h"

engine my_engine;
/*KLEN, VLEN, KEYS, VALUES*/
int _fd_kvd_init(int argc, char **argv){
	engine_mapping(&my_engine,argc, argv);
	return 1;
}

static bool _fd_end_req(request *const req){
	char *value;
	uint8_t klen;
	switch(req->type){
		case FD_KVD_SET:
			inf_free_valueset(req->value,FS_MALLOC_W);
			break;
		case FD_KVD_GET:
			value=req->value->value;
			klen=*(uint8_t*)&value[0];
			memcpy(req->rvlen,&value[sizeof(uint8_t)],sizeof(uint32_t));
			memcpy(req->pparam,&value[5+klen],*req->rvlen);
			inf_free_valueset(req->value,FS_MALLOC_R);
			break;
		case FD_KVD_DELETE:
			break;
	}
	
	req->p_normal_end_req(req->p_req);
	free(req);
	return 1;
}
	
int _fd_kvd_ops(uint32_t type, void *key, uint8_t keylen, void *value, uint32_t vlen, uint32_t *rvlen, void *preq, void (*end_req)(void *req)){
	request *req;
	uint32_t n_type;

	req=(request *)calloc(1,(sizeof(request)));
	req->end_req=_fd_end_req;
	req->p_req=preq;
	req->p_normal_end_req=end_req;
	req->pparam=value;
	req->rvlen=rvlen;
	
	req->key.len=keylen;
	req->key.key=(char*)malloc(keylen);
	memcpy(req->key.key,key,keylen);

	char *vvalue;
	uint32_t vptr=0;
	switch(type){
		case FD_KVD_SET:
			if(vlen+keylen+sizeof(keylen)+sizeof(vlen)>PAGESIZE){
				fprintf(stderr,"it has size limited!! len+key.len+sizeof(key.len)+sizeof(vlen):%ld>%d\n",vlen+keylen+sizeof(keylen)+sizeof(vlen),PAGESIZE);
				abort();
			}
			n_type=FS_SET_T;
			req->value=inf_get_valueset(NULL,FS_SET_T,vlen+keylen+sizeof(keylen)+sizeof(vlen));
			vvalue=req->value->value;
			memcpy(&vvalue[vptr++],&keylen,sizeof(keylen)); //it include uint8_t size 
			memcpy(&vvalue[vptr],&vlen,sizeof(vlen));
			vptr+=sizeof(vlen);
			memcpy(&vvalue[vptr],key,keylen);
			vptr+=keylen;
			memcpy(&vvalue[vptr],value,vlen);
/*
			memcpy(&req->value->value[keylen+sizeof(keylen)],value,vlen);
			memcpy(req->value->value,&keylen,sizeof(keylen));
			memcpy(&req->value->value[sizeof(keylen)],key,keylen);
			my_engine.algo->write(req);
 */
			break;
		case FD_KVD_GET:
			n_type=FS_GET_T;
			req->value=inf_get_valueset(NULL,FS_GET_T,PAGESIZE);
			my_engine.algo->read(req);
			break;
		case FD_KVD_DELETE:
			n_type=FS_DELETE_T;
			req->value=NULL;
			my_engine.algo->remove(req);
			break;
	}
	return 1;
}

int _fd_kvd_destroy(){
	my_engine.algo->destroy(my_engine.li,my_engine.algo);
	my_engine.li->destroy(my_engine.li);
	return 1;
}

