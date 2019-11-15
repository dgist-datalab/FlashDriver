#include "../include/FS.h"
#include "../include/utils/cond_lock.h"
#include "interface.h"
#include "exterior_interface.h"

extern cl_lock* flying;

int _fd_kvd_init(int argc, char **argv){
	inf_init(0,0,argc,argv);
	return 1;
}

static bool _fd_end_req(request *const req){
	switch(req->type){
		case FD_KVD_SET:
			inf_free_valueset(req->value,FS_MALLOC_R);
			break;
		case FD_KVD_GET:
			inf_free_valueset(req->value,FS_MALLOC_R);
			break;
		case FD_KVD_DELETE:
			break;
	}

	req->p_normal_end_req(req->p_req);
	free(req);
	cl_release(flying);
	return 1;
}
	
int _fd_kvd_ops(uint32_t type, void *key, uint8_t keylen, void *value, uint32_t vlen, void *preq, void (*end_req)(void *req)){
	request *req;
	uint32_t n_type;

	req=(request *)calloc(1,(sizeof(request)));
	req->end_req=_fd_end_req;
	req->p_req=preq;
	req->p_normal_end_req=end_req;
	
	req->key.len=keylen;
	req->key.key=(char*)malloc(keylen);
	memcpy(req->key.key,key,keylen);

	switch(type){
		case FD_KVD_SET:
			if(vlen+keylen+sizeof(keylen)>PAGESIZE){
				fprintf(stderr,"it has size limited!! len+key.len+sizeof(key.len):%ld>%d\n",vlen+keylen+sizeof(keylen),PAGESIZE);
				abort();
			}
			n_type=FS_SET_T;
			req->value=inf_get_valueset(NULL,FS_SET_T,vlen+keylen+sizeof(keylen));
			memcpy(&req->value->value[keylen+sizeof(keylen)],value,vlen);
			memcpy(req->value->value,&keylen,sizeof(keylen));
			memcpy(&req->value->value[sizeof(keylen)],key,keylen);
			break;
		case FD_KVD_GET:
			n_type=FS_GET_T;
			req->value=inf_get_valueset(NULL,FS_GET_T,PAGESIZE);
			break;
		case FD_KVD_DELETE:
			n_type=FS_DELETE_T;
			req->value=NULL;
			break;
	}

	cl_grap(flying);
	assign_req(req);
	return 1;
}

int _fd_kvd_destroy(){
	inf_free();
	return 1;
}

