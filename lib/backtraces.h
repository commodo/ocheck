#ifndef __BACKTRACES_H__

#include <stdint.h>

void backtraces(uint32_t *frames, uint32_t max_frames);
void backtraces_set_max_backtraces(uint32_t max_backtraces);

extern uint32_t ocheck_guard_frame;

#endif
