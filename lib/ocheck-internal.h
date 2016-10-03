#ifndef __OCHECK_INTERNAL_H__
#define __OCHECK_INTERNAL_H__

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "backtraces.h"

extern bool lib_inited;

struct call_msg_store {
	enum msg_type type;
	uint32_t upper_index_limit;
	uint32_t messages_count;
	struct call_msg messages[];
};

void store_message_by_ptr(struct call_msg_store *store, uintptr_t ptr, size_t size, uintptr_t *frames);
void store_message_by_fd(struct call_msg_store *store, int fd, uintptr_t *frames);
void remove_message_by_ptr(struct call_msg_store *store, uintptr_t ptr);
void remove_message_by_fd(struct call_msg_store *store, int fd);
void update_message_ptr_by_fd(struct call_msg_store *store, uintptr_t ptr, int fd);

struct call_msg_store *get_alloc_msg_store();
struct call_msg_store *get_files_msg_store();

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
