#ifndef __OCHECK_H__
#define __OCHECK_H__

#include <stdint.h>
#include <stdbool.h>

#define PROC_NAME_LEN	32

/* Something to identify the message type */
#define MSG_MAGIC_NUMBER 0x11233221

#define BACK_FRAMES_COUNT	16

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

enum msg_type {
	INVALID = 0,
	PROC_NAME = 1,
	ALLOC,
	FILES,
};

struct call_msg {
	uint32_t magic;
	uint16_t type;
	uintptr_t ptr;
	int fd;
	uint16_t tid;
	uint32_t size;
} __attribute__((__packed__));

#define DEFAULT_SOCKET	"/var/run/ocheckd.socket"

#endif
