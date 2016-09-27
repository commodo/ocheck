#ifndef __BACKTRACES_H__

#include <stdint.h>
#include <stdbool.h>

bool backtraces(uintptr_t *frames, uint32_t max_frames);
void backtraces_set_max_backtraces(uint32_t max_backtraces);
void ignore_backtrace_push(uintptr_t frame, uint32_t range);

extern uintptr_t ocheck_guard_frame;

#endif
