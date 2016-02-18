#ifndef __FRAME_SIZE_H__

#include <stdint.h>

#if __GNUC__
#if __x86_64__ || __ppc64__
#define uint_ptr_size_t	uint64_t
#define PRIxPTR1	"0x%16lx"
#else
#define uint_ptr_size_t	uint32_t
#define PRIxPTR1	"0x%08x"
#endif
#endif

#endif
