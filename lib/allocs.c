#include <dlfcn.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "ocheck.h"
#include "ocheck-internal.h"

#define ALLOC_MESSAGES_COUNT	(128 * 1024)
static struct call_msg_store alloc_msg_store = {
	.type = ALLOC,
	.capacity = ALLOC_MESSAGES_COUNT,
	.messages[ALLOC_MESSAGES_COUNT] = {0},
};

struct call_msg_store *get_alloc_msg_store()
{
	return &alloc_msg_store;
}

static void* (*real_malloc)(size_t size);
static void* (*real_calloc)(size_t nmemb, size_t size);
static void* (*real_realloc)(void *ptr, size_t size);
static void* (*real_memalign)(size_t blocksize, size_t bytes);
static void* (*real_valloc)(size_t size);
static int   (*real_posix_memalign)(void** memptr, size_t alignment,
                                     size_t size);
static void  (*real_free)(void *ptr);

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

#define END_CALL(ptr,size)

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
	if (ptr != out_ptr) {
		remove_message_by_ptr(&alloc_msg_store, (uintptr_t)ptr);
		END_CALL(out_ptr, size);
	}
	return out_ptr;
}

void free(void *ptr)
{
	START_CALL();
	real_free(ptr);
	remove_message_by_ptr(&alloc_msg_store, (uintptr_t)ptr);
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

