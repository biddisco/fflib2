#ifndef _FFOP_MEM_H_
#define _FFOP_MEM_H_

#include "ff.h"
#include "ffinternal.h"

#define FFOP_MEM_PTR 0x2 
#define FFOP_MEM_IDX 0x4

#define MAX_BUFFER_COUNT 3

typedef uint32_t ffop_mem_type_t;

typedef struct ffop_mem{
    ffop_mem_type_t type;
    
    union{
        void * ptr;
        uint32_t idx;
    };

    uint32_t count;
    ffdatatype_t datatype;

} ffop_mem_t;

typedef struct ffop_mem_set{
    /* ugly */
    void * buffers[MAX_BUFFER_COUNT];
    uint32_t length;

    struct ffmem_set * next;
} ffop_mem_set_t;

#endif /* _FFOP_MEM_H_ */
