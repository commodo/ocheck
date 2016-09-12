#include <dlfcn.h>
#include "ocheck.h"
#include "ocheck-internal.h"
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

static FILE* (*real_fopen)(const char*, const char*);
static int (*real_fclose)(FILE*);
static int (*real_open)(const char *filename, int flags, ...);
static int (*real_close)(int fd);

static void initialize()
{
	static bool initialized = false;

	if (initialized) {
		return;
	}

	real_fopen = dlsym(RTLD_NEXT, "fopen");
	real_fclose = dlsym(RTLD_NEXT, "fclose");
	real_open = dlsym(RTLD_NEXT, "open");
	real_close = dlsym(RTLD_NEXT, "close");

	if ((real_fopen == NULL) || (real_fclose == NULL)
		|| (real_open == NULL) || (real_close == NULL)) {
		debug_exit("Error in `dlsym`: %s\n", dlerror());
	}

	initialized = true;
}

#define START_CALL() \
	initialize();

#define END_CALL(fd) \
	PUSH_MSG(FILES, fd, 0)

FILE* fopen(const char* filename, const char* mode)
{
	FILE* fd = NULL;
	START_CALL();
	fd = real_fopen(filename, mode);
	END_CALL(fd);
	return fd;
}

int fclose(FILE* fd)
{
	int result = EOF;
	START_CALL();
	if (fd != NULL) {
		result = real_fclose(fd);
		remove_message(FILES, (uintptr_t)fd);
	}
	return result;
}

int open(const char *filename, int flags, ...)
{
	int fd = -1;
	START_CALL();
	va_list args;
	va_start(args, flags);
	fd = real_open(filename, flags, args);
	va_end(args);
	END_CALL(fd);
	return fd;
}

int close(int fd)
{
	int result = -1;
	START_CALL();
	if (fd >= 0) {
		result = real_close(fd);
		remove_message(FILES, (uintptr_t)fd);
	}
	return result;
}
