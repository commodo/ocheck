#include <dlfcn.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "backtraces.h"
#include "ocheck.h"
#include "ocheck-internal.h"

static uint32_t heap_pos = 0;
static uint8_t static_heap[256 * 1024];

static void* init_malloc(size_t size)
{
	void *ptr;
	if (heap_pos + size >= sizeof(static_heap))
		exit(1);
	ptr = &static_heap[heap_pos];
	heap_pos += size;
	return ptr;
}

static void* init_realloc(void *ptr, size_t size)
{
	return init_malloc(size);
}

static void* init_calloc(size_t nmemb, size_t size)
{
	void *ptr = init_malloc(nmemb * size);
	unsigned int i = 0;
	for (; i < nmemb * size; ++i)
		*((uint8_t*)(ptr + i)) = '\0';
	return ptr;
}

static void init_free(void *ptr) {}

static void* (*real_malloc)(size_t size) = init_malloc;
static void* (*real_calloc)(size_t nmemb, size_t size) = init_calloc;
static void* (*real_realloc)(void *ptr, size_t size) = init_realloc;
static void* (*real_memalign)(size_t blocksize, size_t bytes) = NULL;
static void* (*real_valloc)(size_t size) = NULL;
static int   (*real_posix_memalign)(void** memptr, size_t alignment,
                                     size_t size) = NULL;
static void  (*real_free)(void *ptr) = init_free;

static void initialize()
{
	static bool initialized = false;

	if (initialized)
		return;

	real_malloc         = dlsym(RTLD_NEXT, "malloc");
	real_calloc         = dlsym(RTLD_NEXT, "calloc");
	real_realloc        = dlsym(RTLD_NEXT, "realloc");
	real_free           = dlsym(RTLD_NEXT, "free");
	real_memalign       = dlsym(RTLD_NEXT, "memalign");
	real_valloc         = dlsym(RTLD_NEXT, "valloc");
	real_posix_memalign = dlsym(RTLD_NEXT, "posix_memalign");

	if (!real_malloc || !real_calloc || !real_realloc || !real_memalign ||
	    !real_valloc || !real_posix_memalign || !real_free)
		debug_exit("Error in `dlsym`: %s\n", dlerror());

	initialized = true;
}

#define START_CALL() \
	initialize();

#define END_CALL(ptr,size) \
	PUSH_MSG(ALLOC, ptr, size)

void* malloc(size_t size)
{
	void *ptr;
	START_CALL();
	ptr = real_malloc(size);
	END_CALL(ptr, size);
	return ptr;
}

void* calloc(size_t nmemb, size_t size)
{
	void *ptr;
	START_CALL();
	ptr = real_calloc(nmemb, size);
	END_CALL(ptr, (nmemb * size));
	return ptr;
}

void* realloc(void *ptr, size_t size)
{
	void *out_ptr;
	START_CALL();
	out_ptr = real_realloc(ptr, size);
	if (ptr != out_ptr)
		remove_message(ALLOC, (uintptr_t)ptr);
	END_CALL(out_ptr, size);
	return out_ptr;
}

void free(void *ptr)
{
	START_CALL();
	real_free(ptr);
	remove_message(ALLOC, (uintptr_t)ptr);
}

void* memalign(size_t blocksize, size_t bytes)
{
	void *ptr;
	START_CALL();
	ptr = real_memalign(blocksize, bytes);
	END_CALL(ptr, bytes);
	return ptr;
}

int posix_memalign(void** memptr, size_t alignment, size_t size)
{
	int rv;
	START_CALL();
	rv = real_posix_memalign(memptr, alignment, size);
	/* FIXME: not sure if all this works fine yet */
	if (memptr)
		END_CALL(*memptr, size);
	return rv;
}

void* valloc(size_t size)
{
	void *ptr;
	START_CALL();
	ptr = real_valloc(size);
	END_CALL(ptr, size);
	return ptr;
}

