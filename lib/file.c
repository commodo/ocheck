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

FILE* (*real_fopen)(const char*, const char*);
int (*real_fclose)(FILE*);
int (*real_fprintf)(FILE *, const char *, ... );

static int (*real_socket)(int domain, int type, int protocol);
static int (*real_open)(const char *filename, int flags, ...);
static int (*real_close)(int fd);
static FILE* (*real_fdopen)(int filedes, const char *mode);

void initialize_file_hooks_for_debug()
{
	real_fopen = dlsym(RTLD_NEXT, "fopen");
	real_fclose = dlsym(RTLD_NEXT, "fclose");
	real_fprintf = dlsym(RTLD_NEXT, "fprintf");

	if (!real_fopen || !real_fclose || !real_fprintf)
		exit(1);
}

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

#define END_CALL_FD(fd)
#define END_CALL_PTR(fp)

FILE* fopen(const char* filename, const char* mode)
{
	FILE* fp = NULL;
	START_CALL();
	fp = real_fopen(filename, mode);
	END_CALL_PTR(fp);
	return fp;
}

int fclose(FILE* fp)
{
	int result;
	START_CALL();
	result = real_fclose(fp);
	remove_message_by_ptr(&files_msg_store, (uintptr_t)fp);
	return result;
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
	END_CALL_PTR(result);
	return result;
}

int socket(int domain, int type, int protocol)
{
	int sock;
	START_CALL();
	sock = real_socket(domain, type, protocol);
	END_CALL_FD(sock);
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
	END_CALL_FD(fd);
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
