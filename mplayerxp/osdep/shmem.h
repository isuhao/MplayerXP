#ifndef __SHMEM_H_INCLUDED
#define __SHMEM_H_INCLUDED 1

any_t* shmem_alloc(int size);
void shmem_free(any_t* p);
#endif
