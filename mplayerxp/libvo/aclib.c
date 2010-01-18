#include <stdio.h>
#include <pthread.h>
#include "../mp_config.h"
#include "../mplayer.h"
#define MSGT_CLASS MSGT_GLOBAL
#include "../__mp_msg.h"

#if defined(USE_FASTMEMCPY)
#include "fastmemcpy.h"
#include "../cpudetect.h"

#define BLOCK_SIZE 4096
#define CONFUSION_FACTOR 0

/* generic version */
#undef OPTIMIZE_AVX
#undef OPTIMIZE_SSE4
#undef OPTIMIZE_SSSE3
#undef OPTIMIZE_SSE3
#undef OPTIMIZE_SSE2
#undef OPTIMIZE_SSE
#undef OPTIMIZE_MMX2
#undef OPTIMIZE_MMX

#ifndef __x86_64__
#ifdef __MMX__
#define OPTIMIZE_MMX
#undef RENAME
#define RENAME(a) a ## _MMX
#include "aclib_template.c"
#endif
#ifdef __SSE__
#define OPTIMIZE_MMX2
#undef RENAME
#define RENAME(a) a ## _MMX2
#include "aclib_template.c"
#endif
#endif // __x86_64__
#ifdef __SSE2__
#define OPTIMIZE_SSE2
#undef RENAME
#define RENAME(a) a ## _SSE2
#include "aclib_template.c"
#endif
/*
  aclib - advanced C library ;)
  This file contains functions which improve and expand standard C-library
  see aclib_template.c ... this file only contains runtime cpu detection and config options stuff
  runtime cpu detection by michael niedermayer (michaelni@gmx.at) is under GPL
*/

static void * init_fast_memcpy(void * to, const void * from, size_t len)
{
#ifdef __SSE2__
	if(gCpuCaps.hasSSE2)
	{
		MSG_V("Using SSE2 optimized memcpy\n");
		fast_memcpy_ptr = fast_memcpy_SSE2;
	}
	else
#endif
#ifndef __x86_64__
#ifdef __SSE__
	if(gCpuCaps.hasMMX2)
	{
		MSG_V("Using MMX2 optimized memcpy\n");
		fast_memcpy_ptr = fast_memcpy_MMX2;
	}
	else
#endif
#ifdef __MMX__
	if(gCpuCaps.hasMMX)
	{
		MSG_V("Using MMX optimized memcpy\n");
		fast_memcpy_ptr = fast_memcpy_MMX;
	}
	else
#endif
#endif
	{
		MSG_V("Using generic memcpy\n");
		fast_memcpy_ptr = memcpy; /* prior to mmx we use the standart memcpy */
	}
	return (*fast_memcpy_ptr)(to,from,len);
}

static void * init_stream_copy(void * to, const void * from, size_t len)
{
#ifdef __SSE2__
	if(gCpuCaps.hasSSE2)
	{
		MSG_V("Using SSE2 optimized agpcpy\n");
		fast_stream_copy_ptr = fast_stream_copy_SSE2;
	}
#endif
#ifndef __x86_64__
#ifdef __SSE__
	if(gCpuCaps.hasMMX2)
	{
		MSG_V("Using MMX2 optimized agpcpy\n");
		fast_stream_copy_ptr = fast_stream_copy_MMX2;
	}
	else
#endif
#ifdef __MMX__
	if(gCpuCaps.hasMMX)
	{
		MSG_V("Using MMX optimized agpcpy\n");
		fast_stream_copy_ptr = fast_stream_copy_MMX;
	}
	else
#endif
#endif
	{
		MSG_V("Using generic optimized agpcpy\n");
		fast_stream_copy_ptr = memcpy; /* prior to mmx we use the standart memcpy */
	}
	return (*fast_stream_copy_ptr)(to,from,len);
}

void *(*fast_memcpy_ptr)(void * to, const void * from, size_t len) = init_fast_memcpy;
void *(*fast_stream_copy_ptr)(void * to, const void * from, size_t len) = init_stream_copy;

#endif /* use fastmemcpy */

