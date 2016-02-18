#ifndef __OCHECK_INTERNAL_H__
#define __OCHECK_INTERNAL_H__

#include <stdbool.h>

extern bool lib_inited;

void store_message(enum msg_type type, uint_ptr_size_t id, size_t size, uint_ptr_size_t *frames);
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

#endif
