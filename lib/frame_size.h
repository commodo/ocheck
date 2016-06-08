#ifndef __FRAME_SIZE_H__

#include <inttypes.h>

#if UINTPTR_MAX > 0xFFFFFFFF
#define PRIxPTR_PAD	"016" PRIxPTR
#else
#define PRIxPTR_PAD	"08" PRIxPTR
#endif

#endif
