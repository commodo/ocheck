#include <dlfcn.h>
#include "ocheck.h"
#include "ocheck-internal.h"
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#define FILES_MESSAGES_COUNT	(64 * 1024)
static struct call_msg_store files_msg_store = {
	.type = FILES,
	.messages_count = FILES_MESSAGES_COUNT,
	.messages[FILES_MESSAGES_COUNT] = {0}
};

struct call_msg_store *get_files_msg_store()
{
	return &files_msg_store;
}

static int (*real_socket)(int domain, int type, int protocol);
static int (*real_open)(const char *filename, int flags, ...);
static int (*real_close)(int fd);
static FILE* (*real_fdopen)(int filedes, const char *mode);

static void initialize()
{
	static bool initialized = false;

	if (initialized) {
		return;
	}

	real_socket = dlsym(RTLD_NEXT, "socket");
	real_open = dlsym(RTLD_NEXT, "open");
	real_close = dlsym(RTLD_NEXT, "close");
	real_fdopen = dlsym(RTLD_NEXT, "fdopen");

	if ((real_open == NULL) || (real_close == NULL) || 
	    (real_fdopen == NULL) || (real_socket == NULL)) {
		debug_exit("Error in `dlsym`: %s\n", dlerror());
	}

	initialized = true;
}

#define START_CALL() \
	initialize();

#define END_CALL(ptr, fd) \
	if (lib_inited) {\
		uintptr_t frames[BACK_FRAMES_COUNT] = {0}; \
		if (backtraces(frames, ARRAY_SIZE(frames))) \
			store_message_by_fd(&files_msg_store, fd, frames); \
	}

FILE *fdopen(int filedes, const char *mode)
{
	FILE *result;
	START_CALL();
	result = real_fdopen(filedes, mode);
	/* This is a bit weird, I know ; but if you think about it,
	   fopen() & fclose() are also handled by malloc() + free().
	   So, no need to also track them here.
	   Here, we only need to care about FDs ; so, technically,
	   fdopen() would do an alloc(), which should be free'd.
	*/
	remove_message_by_fd(&files_msg_store, filedes);
	return result;
}

int socket(int domain, int type, int protocol)
{
	int sock;
	START_CALL();
	sock = real_socket(domain, type, protocol);
	END_CALL(NULL, sock);
	return sock;
}

int open(const char *filename, int flags, ...)
{
	int fd = -1;
	START_CALL();
	va_list args;
	va_start(args, flags);
	fd = real_open(filename, flags, args);
	va_end(args);
	END_CALL(NULL, fd);
	return fd;
}

int close(int fd)
{
	int result = -1;
	START_CALL();
	result = real_close(fd);
	remove_message_by_fd(&files_msg_store, fd);
	return result;
}
