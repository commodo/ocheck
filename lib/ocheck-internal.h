#ifndef __OCHECK_INTERNAL_H__
#define __OCHECK_INTERNAL_H__

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "backtraces.h"

extern bool lib_inited;

void store_message(enum msg_type type, uintptr_t id, size_t size, uintptr_t *frames);
void remove_message(enum msg_type type, uint32_t id);

#define debug(...) { \
	FILE *fp = fopen("/tmp/ocheck.out", "ab"); \
	if (fp) { \
		fprintf(fp, __VA_ARGS__); \
		fclose(fp); \
	} \
	fprintf(stderr, __VA_ARGS__); \
}

/* In case we exit because of some failure */
#define debug_exit(...) { \
	debug(__VA_ARGS__); \
	exit(1); \
}

#define PUSH_MSG(type, ptr, size) \
	if (lib_inited) {\
		uintptr_t frames[BACK_FRAMES_COUNT] = {0}; \
		if (backtraces(frames, ARRAY_SIZE(frames))) \
			store_message(type, (uintptr_t)ptr, size, frames); \
	}

#endif
