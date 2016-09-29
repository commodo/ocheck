
#include "backtraces.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

/* This gist of 'ignore_bt' here, is that some functions in some libraries
   (like libc) do allocs which are never explicitly free'd.
   That's rude (and annoying)... well, I am annoyed (to be more specific).
   So, the fix is to add some basic mechanism for ignoring some functions
   that do allocs, which are known to be safe (to be ignored).
*/

struct ignore_bt {
	uintptr_t frame_start;
	uintptr_t frame_end;
};

/* 512 entries should be enough for now */
static struct ignore_bt g_ignore_bt[512];
static uint32_t g_ignore_bt_cnt = 0;

static uint32_t g_max_backtraces = 0;
void backtraces_set_max_backtraces(uint32_t max_backtraces)
{
	g_max_backtraces = max_backtraces;
}

/* For a lot of cases, the top stack frame (or anything after that) should be zero.
   But gcc docs says that this is arch dependant, so this should be called in the
   main() function with 'ocheck_guard_frame = (uint32_t) __builtin_return_address(0);'.
*/
uintptr_t ocheck_guard_frame = 0;

enum BT_RESULT {
	BT_DONE,
	BT_HAVE_MORE,
	BT_IGNORE,
};

/*
	Getting stack-traces doesn't seem as straightforward as one might think.
	__builtin_return_address() is not a function nor a macro, it's a builtin,
	so it needs some special treatment.
*/

#define backtraces_(N) \
	static enum BT_RESULT backtraces_##N(uintptr_t *frames) \
	{ \
		int i; \
		frames[N] = (uintptr_t) __builtin_return_address(N); \
		for (i = 0; i < g_ignore_bt_cnt; i++) { \
			struct ignore_bt *bt = &g_ignore_bt[i]; \
			if (bt->frame_start < frames[N] && frames[N] < bt->frame_end) \
				return BT_IGNORE; \
		} \
		return (frames[N] != ocheck_guard_frame) ? BT_HAVE_MORE : BT_DONE; \
	}

backtraces_(0);
backtraces_(1);
backtraces_(2);
backtraces_(3);
backtraces_(4);
backtraces_(5);
backtraces_(6);
backtraces_(7);
backtraces_(8);
backtraces_(9);
backtraces_(10);
backtraces_(11);
backtraces_(12);
backtraces_(13);
backtraces_(14);
backtraces_(15);

typedef enum BT_RESULT (*backtraces_func)(uintptr_t *);

static backtraces_func backtraces_funcs[] = {
	backtraces_0,
	backtraces_1,
	backtraces_2,
	backtraces_3,
	backtraces_4,
	backtraces_5,
	backtraces_6,
	backtraces_7,
	backtraces_8,
	backtraces_9,
	backtraces_10,
	backtraces_11,
	backtraces_12,
	backtraces_13,
	backtraces_14,
	backtraces_15,
	0,
};

/* https://gcc.gnu.org/bugzilla/show_bug.cgi?id=8743 */
bool backtraces(uintptr_t *frames, uint32_t max_frames)
{
	int i, res = BT_HAVE_MORE;
	for (i = 0; i < g_max_backtraces && i < max_frames && backtraces_funcs[i] && res == BT_HAVE_MORE; i++)
		res = backtraces_funcs[i](frames);
	return (res != BT_IGNORE);
}

void ignore_backtrace_push(uintptr_t frame, uint32_t range)
{
	struct ignore_bt *bt = &g_ignore_bt[g_ignore_bt_cnt++];
	bt->frame_start = frame;
	bt->frame_end = frame + range;
}

