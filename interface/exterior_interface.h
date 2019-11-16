#ifndef __H_EXT_INTERFACE_H
#define __H_EXT_INTERFACE_H
#include <stdint.h>
enum fd_operations_type{
	FD_KVD_SET,FD_KVD_GET,FD_KVD_DELETE
};

int _fd_kvd_init(int argc, char **argv);
int _fd_kvd_ops(uint32_t type, void *key, uint8_t keylen, void *value, uint32_t vlen, uint32_t *rvlen,void *req, void (*end_req)(void *req));
int _fd_kvd_destroy();
#endif
