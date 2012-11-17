/*
 *  mplib.h
 *
 *	Copyright (C) Nickols_K <nickols_k@mail.ru> - Oct 2001
 *
 *  You can redistribute this file under terms and conditions
 *  of GNU General Public licence v2.
 */
#ifndef __MPLIB_H_INCLUDED__
#define __MPLIB_H_INCLUDED__ 1
#include <execinfo.h>
#include <stddef.h>
#include <sys/mman.h>
#include "mp_config.h"

#ifdef __cplusplus
extern "C" {
#endif
extern volatile unsigned long long int my_profile_start,my_profile_end,my_profile_total;

#if defined ( ENABLE_PROFILE ) && (defined ( ARCH_X86 ) || defined( ARCH_X86_64 ))
static inline unsigned long long int read_tsc( void )
{
  unsigned long long int retval;
  __asm __volatile ("rdtsc":"=A"(retval)::"memory");
  return retval;
}

#define PROFILE_RESET()                 (my_profile_total=0ULL)
#define PROFILE_START()			{ static int inited=0; if(!inited) { inited=1; my_profile_total=0ULL; } my_profile_start=read_tsc(); }
#define PROFILE_END(your_message)	{ my_profile_end=read_tsc(); my_profile_total+=(my_profile_end-my_profile_start); printf(your_message" current=%llu total=%llu\n\t",(my_profile_end-my_profile_start),my_profile_total); }
#else
#define PROFILE_RESET()
#define PROFILE_START()
#define PROFILE_END(your_message)
#endif
/** Initializes randomizer for malloc.
  * @param rnd_limit       upper limit of random generator (recommened: 1000)
  * @param every_nth_call  how often call randimzer (recommened: 10)
  * @note                  Pseudo-randomizing memory objects makes memory
  *                        exploits harder
*/
enum mp_malloc_e {
    MPA_FLG_RANDOMIZER   = 0x00000000,
    MPA_FLG_BOUNDS_CHECK = 0x00000001,
    MPA_FLG_BEFORE_CHECK = 0x00000002,
    MPA_FLG_BACKTRACE    = 0x00000004
};
extern void	__FASTCALL__ mp_init_malloc(const char *argv0,unsigned rnd_limit,unsigned every_nth_call,enum mp_malloc_e flags);
extern void	__FASTCALL__ mp_uninit_malloc(int verbose);

extern any_t*	__FASTCALL__ mp_malloc(size_t __size);
extern any_t*	__FASTCALL__ mp_mallocz(size_t __size);
extern any_t*	__FASTCALL__ mp_realloc(any_t*__ptr, size_t __size);
static inline any_t* mp_calloc (size_t __nelem, size_t __size) { return mp_mallocz(__nelem*__size); }
extern any_t*	__FASTCALL__ mp_memalign (size_t boundary, size_t __size);
extern void  	__FASTCALL__ mp_free(any_t*__ptr);
extern char *	__FASTCALL__ mp_strdup(const char *src);

/* flags: PROT_NONE, PROT_READ, PROT_WRITE, PROT_EXEC */
enum mp_prot_e {
    MP_PROT_READ	=0x1,	/* Page can be read.  */
    MP_PROT_WRITE	=0x2,	/* Page can be written.  */
    MP_PROT_EXEC	=0x4,	/* Page can be executed.  */
    MP_DENY_ALL		=0x0,	/* Page can not be accessed.  */
};
extern int	__FASTCALL__ mp_mprotect(any_t* addr,size_t len,enum mp_prot_e flags);

extern void print_backtrace(const char *why,any_t** stack,unsigned num);

static inline void show_backtrace(const char *why,unsigned num_calls) {
    any_t*	stack[num_calls];
    unsigned ncalls;
    ncalls=backtrace(stack,num_calls);
    print_backtrace(why,stack,ncalls);
}

extern any_t*	__FASTCALL__ SECURE_NAME0(_mp_malloc)(size_t size);
extern any_t*	__FASTCALL__ SECURE_NAME1(_mp_mallocz)(size_t size);
extern void	__FASTCALL__ SECURE_NAME2(_mp_free)(any_t* ptr);
extern any_t*	__FASTCALL__ SECURE_NAME9(rnd_fill)(any_t* buffer,size_t size);
#ifdef __cplusplus
}

extern "C++" {
#include <new>
    enum zeromemory_t{ zeromem=0 };
    inline any_t *operator new(size_t size) { return SECURE_NAME0(_mp_malloc)(size); }
    inline any_t *operator new(size_t size,const zeromemory_t&) { return SECURE_NAME1(_mp_mallocz)(size); }
    inline any_t *operator new(size_t size,const std::nothrow_t&) { return mp_malloc(size); }
    inline any_t *operator new[](size_t size) { return SECURE_NAME0(_mp_malloc)(size); }
    inline any_t *operator new[](size_t size,const zeromemory_t&) { return SECURE_NAME1(_mp_mallocz)(size); }
    inline any_t *operator new[](size_t size,const std::nothrow_t&) { return mp_malloc(size); }
    inline void   operator delete(any_t* p) { SECURE_NAME2(_mp_free)(p); }
    inline void   operator delete[](any_t* p) { SECURE_NAME2(_mp_free)(p); }

    extern any_t* get_caller_address(unsigned num_caller=0);
}
#endif

#endif
