
#include "frame_size.h"
#include "backtraces.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

/* For a lot of cases, the top stack frame (or anything after that) should be zero.
   But gcc docs says that this is arch dependant, so this should be called in the
   main() function with 'ocheck_guard_frame = (uint32_t) __builtin_return_address(0);'.
*/
uint_ptr_size_t ocheck_guard_frame = 0;

enum BT_RESULT {
    BT_DONE,
    BT_HAVE_MORE,
};

/*
	Getting stack-traces doesn't seem as straightforward as one might think.
	__builtin_return_address() is not a function nor a macro, it's a builtin,
	so it needs some special treatment.
*/

#define backtraces_(N) \
	static enum BT_RESULT backtraces_##N(uint_ptr_size_t *frames) \
	{ \
		frames[N] = (uint_ptr_size_t) __builtin_return_address(N); \
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

typedef enum BT_RESULT (*backtraces_func)(uint_ptr_size_t *);

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
void backtraces(uint_ptr_size_t *frames, uint32_t max_frames)
{
	int i, res = BT_HAVE_MORE;
	for (i = 0; i < max_frames && backtraces_funcs[i] && res == BT_HAVE_MORE; i++)
		res = backtraces_funcs[i](frames);
}

