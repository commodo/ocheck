#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <dlfcn.h>
#include <poll.h>
#include <sys/un.h>
#include <signal.h>
#include <errno.h>
#include <string.h>

#include "ocheck.h"
#include "ocheck-internal.h"

#define DEFAULT_MAX_FLUSH_COUNTER	512
static uint32_t max_flush_counter = DEFAULT_MAX_FLUSH_COUNTER;
static int fd = -1;
static struct call_msg messages[512 * 1024] = {0};
static int flush_counter = -1;
static pid_t pid = -1;

enum FLUSH_STATE {
	IDLE = 0,
	BUSY,
	INTERRUPT
};
static enum FLUSH_STATE curr_flush_state = IDLE;

bool lib_inited = false;

static inline pid_t ourgettid()
{
	return syscall(SYS_gettid);
}

static inline pid_t ourgetpid()
{
	return syscall(SYS_getpid);
}

static void initialize_sock()
{
	const char *sock;
	struct sockaddr_un sun = {.sun_family = AF_UNIX};

	if (fd > -1)
		return;

	if (fd < 0 && (fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		debug_exit("Could not open socket\n");

	sock = getenv("SOCK");
	if (!sock)
		sock = DEFAULT_SOCKET;
	else if (strlen(sock) >= sizeof(sun.sun_path))
		debug_exit("Sock path too long\n");

	strcpy(sun.sun_path, sock);

	fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | O_NONBLOCK | FD_CLOEXEC);

	if (connect(fd, (struct sockaddr *)&sun, sizeof(sun)) && errno != EINPROGRESS) {
		close(fd);
		debug_exit("Could not connect to socket (%d) '%s' errno %d\n", fd, sock, errno);
	}
}

/* wait_data() and write_retry() adapted from libubus-io.c */
static void wait_data(int fd, bool write)
{
	struct pollfd pfd = { .fd = fd };

	pfd.events = write ? POLLOUT : POLLIN;
	poll(&pfd, 1, -1);
}

static int write_retry(int fd, uint8_t *buf, uint32_t buf_len)
{
	int len = 0;

	do {
		int cur_len;

		cur_len = write(fd, &buf[len], buf_len - len);
		if (cur_len < 0) {
			switch(errno) {
			case EAGAIN:
				wait_data(fd, true);
				break;
			case EINTR:
				break;
			default:
				return -1;
			}
			continue;
		}
		if (curr_flush_state != BUSY)
			return 0;

		len += cur_len;
		if (len == buf_len)
			return len;

	} while (1);

	/* Should never reach here */
	return -1;
}

static uint32_t flush_messages(bool now)
{
	uint32_t i, flushed = 0;
	struct msg_common clr = { .magic = MSG_MAGIC_NUMBER, .type = CLEAR };

	/* Not inited, can't flush */
	if (fd < 0)
		return 0;

	if (!now && --flush_counter > 0)
		return 0;

	/* Make sure flush_counter cannot get to zero while calling flush_messages() */
	flush_counter = 999999999;

	curr_flush_state = BUSY;
	if (write_retry(fd, (uint8_t *) &clr, sizeof(clr)) < 0)
		debug_exit("Could not send clear message\n");

	for (i = 0; i < ARRAY_SIZE(messages) && curr_flush_state == BUSY; i++) {
		struct call_msg *msg = &messages[i];
		if (!msg->ptr && msg->fd < 0)
			continue;
		if (write_retry(fd, (uint8_t *) msg, sizeof(*msg)) < 0)
			debug_exit("Could not send message ; errno %d\n", errno);
		flushed++;
	}
	curr_flush_state = IDLE;
	flush_counter = max_flush_counter;
	return flushed;
}

static inline void call_msg_invalidate(struct call_msg *msg)
{
	if (msg) {
		msg->type = INVALID;
		msg->ptr = 0;
		msg->fd = -1;
	}
}

static struct call_msg *call_msg_find_by_ptr(enum msg_type type, uintptr_t ptr)
{
	int i;
	if (!ptr)
		return NULL;
	for (i = 0; i < ARRAY_SIZE(messages); i++) {
		if (messages[i].type == type && messages[i].ptr == ptr)
			return &messages[i];
	}
	return NULL;
}

static struct call_msg *call_msg_find_by_fd(enum msg_type type, int fd)
{
	int i;
	if (fd < 0)
		return NULL;
	for (i = 0; i < ARRAY_SIZE(messages); i++) {
		if (messages[i].type == type && messages[i].fd == fd)
			return &messages[i];
	}
	return NULL;
}

static inline struct call_msg *call_msg_find(enum msg_type type, uintptr_t ptr, int fd)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(messages); i++) {
		if (messages[i].type != type)
			continue;
		if (messages[i].ptr == ptr && messages[i].fd == fd)
			return &messages[i];
	}
	return NULL;
}

static inline struct call_msg *call_msg_get_free()
{
	int i;
	for (i = 0; i < ARRAY_SIZE(messages); i++) {
		if (messages[i].type == INVALID)
			return &messages[i];
	}
	return NULL;
}

void store_message(enum msg_type type, uintptr_t ptr, int fd, size_t size, uintptr_t *frames)
{
	struct call_msg *msg;

	if (fd < 0 && !ptr)
		return;

	/* FIXME: see what we do in this case ; this looks like a malfunction ; for now we ignore */
	if (!(msg = call_msg_find(type, ptr, fd))) {
		msg = call_msg_get_free();
		if (!msg)
			debug_exit("No more room to store calls\n");
	}

	msg->magic = MSG_MAGIC_NUMBER;
	msg->type  = type;
	msg->tid   = ourgettid();
	msg->ptr   = ptr;
	msg->fd    = fd;
	msg->size  = size; 
	memcpy(msg->frames, frames, sizeof(msg->frames));

	if (max_flush_counter <= 0)
		return;

	flush_messages(false);
}

void remove_message_by_ptr(enum msg_type type, uintptr_t ptr)
{
	call_msg_invalidate(call_msg_find_by_ptr(type, ptr));
}

void remove_message_by_fd(enum msg_type type, int fd)
{
	call_msg_invalidate(call_msg_find_by_fd(type, fd));
}

void update_message_ptr_by_fd(enum msg_type type, uintptr_t ptr, int fd)
{
	struct call_msg *msg;
	if (fd < 0)
		return;
	if ((msg = call_msg_find_by_fd(type, fd)))
		msg->ptr = ptr;
}

static const char *progname(int pid)
{
	FILE *proc;
	static char prog[32] = "";
	char file[128], *c;
	int a;

	if (prog[0] != '\0')
		return prog;

	snprintf(file, sizeof(file), "/proc/%u/cmdline", pid);

	if (!(proc = fopen(file, "r")) ||
	    !(a = fread(file, 1, sizeof(file), proc))) {
		if (proc)
			fclose(proc);
		return NULL;
	}
	fclose(proc);

	if ((c = strrchr(file, '/')))
		c++;
	else
		c = file;
	strncpy(prog, c, sizeof(prog));

	return prog;
}

static const char *is_this_the_right_proc()
{
	const char *proc_name = getenv("PROC");
	const char *actual_proc_name = NULL;

	if (!proc_name)
		debug_exit("No 'PROC' env var specified\n");

	if (!(actual_proc_name = progname(pid)))
		debug_exit("Could not get actual program name\n");

	if (strcmp(actual_proc_name, proc_name))
		return NULL;

	return proc_name;
}

/* Ah, parsing in C (and with no allocs), such a "joy" ; look mom, no hands */
static void parse_ignore_backtraces()
{
	/* Note: functions can be provided as 'IGNORE_BT=func1:300,func2:400'
	   So, each pair is separated by a comma and is a tuple of
	   function name + an arbitrary offset (in decimal).
	   Only functions that are exported will work (usually).
	*/
	const char *ignore_bts = getenv("IGNORE_BT");
	int len;
	const char *endp, *lastp;
	/* no allocs, because "who knows ?" */
	char buf[64] = "";

	if (!ignore_bts) {
		debug("\n Ignore list empty\n");
		return;
	}
	len = strlen(ignore_bts);

	lastp = endp = ignore_bts;
	while (len > 0) {
		uintptr_t frame;
		char *delim;
		uint32_t range;

		if (!(endp = strchr(endp, ',')))
			endp = lastp + len;

		len -= (endp - lastp);

		strncpy(buf, lastp, (endp - lastp));
		delim = strchr(buf, ':');
		lastp = ++endp;
		if (!delim)
			continue;
		*delim = '\0';
		delim++;
		frame = (uintptr_t) dlsym(RTLD_NEXT, buf);
		range = atoi(delim);

		if (!frame) {
			debug("\n Could not find dlsym() for '%s'", buf);
			continue;
		}

		debug("\n Ignoring '%s' frame 0x%08x range %u", buf, frame, range);
		ignore_backtrace_push(frame, range);
	}
	debug("\n");
}

/* Processes that support ocheck should implement theses
   hooks so this lib can override them and do init + fini
 */

static __attribute__((constructor(101))) void ocheck_init()
{
	struct proc_msg msg;
	const char *proc_name;
	const char *s;
	int i;

	/* Do not re-initialize if already initialized and it's the same pid */
	if (lib_inited && (pid == ourgetpid()))
		return;

	backtraces_set_max_backtraces(0);
	pid = ourgetpid();
	if (pid < 0)
		debug_exit("Could not get pid\n");
	if (!(proc_name = is_this_the_right_proc()))
		return;
	unlink("/tmp/ocheck.out");

	debug("Initializing libocheck.so for %s.%u... ", proc_name, pid);

	initialize_sock();

	parse_ignore_backtraces();

	/* if max_flush_counter <= 0 then flush only on ocheck_fini() */
	if ((s = getenv("FLUSH_COUNT")))
		max_flush_counter = atoi(s);

	/* Initialize fd to negative */
	for (i = 0; i < ARRAY_SIZE(messages); i++)
		messages[i].fd = -1;

	/* We won't get here, since initialize_sock() calls exit(1) in case something does not init  */
	msg.magic = MSG_MAGIC_NUMBER;
	msg.type  = PROC_NAME;
	snprintf(msg.name, sizeof(msg.name), "%s.%d", proc_name, pid);
	write_retry(fd, (uint8_t *)&msg, sizeof(msg));

	debug("done\n");
	lib_inited = true;
	backtraces_set_max_backtraces(BACK_FRAMES_COUNT);
}

static __attribute__((destructor(101))) void ocheck_fini()
{
	uint32_t flushed = 0;
	const char *proc_name;

	/* Prevent forks from calling de-init code */
	if (!lib_inited || pid != ourgetpid())
		return;

	backtraces_set_max_backtraces(0);
	if (!(proc_name = is_this_the_right_proc()))
		goto out;

	debug("Uninitializing libocheck.so for %s.%u...\n", proc_name, pid);

	if (fd > -1) {
		if (curr_flush_state == BUSY) {
			debug("  Flushing still in progress...\n");
			curr_flush_state = INTERRUPT;
			while (curr_flush_state != IDLE)
				wait_data(fd, true);
		}

		flushed = flush_messages(true);
		close(fd);
	}

	debug("  Flushed %u messages\n", flushed);

	if (pid > -1 && flushed)
		kill(pid, SIGSEGV);

	debug("Done\n");
out:
	lib_inited = false;
}

