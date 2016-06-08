#ifndef __BACKTRACES_H__

#include <stdint.h>

void backtraces(uintptr_t *frames, uint32_t max_frames);
void backtraces_set_max_backtraces(uint32_t max_backtraces);

extern uintptr_t ocheck_guard_frame;

#endif
