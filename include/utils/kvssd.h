#ifndef __H_KVSSD__
#define __H_KVSSD__
#include "../settings.h"

char* kvssd_tostring(KEYT);
void kvssd_cpy_key(KEYT *des,KEYT* src);
void kvssd_free_key_content(KEYT *des);

#endif
