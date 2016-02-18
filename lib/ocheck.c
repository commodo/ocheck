#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <poll.h>
#include <sys/un.h>
#include <signal.h>
#include <errno.h>

#include "frame_size.h"
#include "backtraces.h"
#include "ocheck.h"
#include "ocheck-internal.h"

#define DEFAULT_MAX_FLUSH_COUNTER	512
static uint32_t max_flush_counter = DEFAULT_MAX_FLUSH_COUNTER;
static int fd = -1;
static struct call_msg messages[32 * 1024] = {0};
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

static uint32_t flush_messages()
{
	uint32_t i, flushed = 0;
	struct msg_common clr = { .magic = MSG_MAGIC_NUMBER, .type = CLEAR };

	/* Not inited, can't flush */
	if (fd < 0)
		return 0;

	curr_flush_state = BUSY;
	if (write_retry(fd, (uint8_t *) &clr, sizeof(clr)) < 0)
		debug_exit("Could not send clear message\n");

	for (i = 0; i < ARRAY_SIZE(messages) && curr_flush_state == BUSY; i++) {
		struct call_msg *msg = &messages[i];
		if (!msg->id) /* For now we consider non-zero IDs as valid IDs ; this could change */
			continue;
		if (write_retry(fd, (uint8_t *) msg, sizeof(*msg)) < 0)
			debug_exit("Could not send message ; errno %d\n", errno);
		flushed++;
	}
	curr_flush_state = IDLE;
	return flushed;
}

static inline struct call_msg *call_msg_find(enum msg_type type, uint32_t id)
{
	int i;
	if (!id)
		return NULL;
	for (i = 0; i < ARRAY_SIZE(messages); i++) {
		if (messages[i].type == type && messages[i].id == id)
			return &messages[i];
	}
	return NULL;
}

static inline struct call_msg *call_msg_get_free()
{
	int i;
	for (i = 0; i < ARRAY_SIZE(messages); i++) {
		/* For now we consider non-zero IDs as valid IDs ; this could change */
		if (!messages[i].id)
			return &messages[i];
	}
	return NULL;
}

void store_message(enum msg_type type, uint_ptr_size_t id, size_t size, uint_ptr_size_t *frames)
{
	struct call_msg *msg;

	/* FIXME: see what we do in this case ; this looks like a malfunction ; for now we ignore */
	if ((msg = call_msg_find(type, id)))
		msg->id = 0;

	if (!msg) {
		msg = call_msg_get_free();
		if (!msg)
			debug_exit("No more room to store calls\n");
	}

	msg->magic = MSG_MAGIC_NUMBER;
	msg->type  = type;
	msg->tid   = ourgettid();
	msg->id    = id;
	msg->size  = size; 
	memcpy(msg->frames, frames, sizeof(msg->frames));

	if (max_flush_counter <= 0)
		return;

	if (--flush_counter > 0)
		return;
	/* Make sure flush_counter cannot get to zero while calling flush_messages() */
	flush_counter = 999999999;
	flush_messages();
	flush_counter = max_flush_counter;
}

void remove_message(enum msg_type type, uint32_t id)
{
	struct call_msg *msg;
	/* FIXME: this can be an invalid call/free ; store later */
	if ((msg = call_msg_find(type, id)))
		msg->id = 0;
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

/* Processes that support ocheck should implement theses
   hooks so this lib can override them and do init + fini
 */

static __attribute__((constructor(101))) void ocheck_init()
{
	struct proc_msg msg;
	const char *proc_name;
	const char *s;

	if (lib_inited)
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

	/* if max_flush_counter <= 0 then flush only on ocheck_fini() */
	if ((s = getenv("FLUSH_COUNT")))
		max_flush_counter = atoi(s);

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

	if (!lib_inited)
		return;

	backtraces_set_max_backtraces(0);
	if (!(proc_name = is_this_the_right_proc()))
		goto out;

	debug("Uninitializing libocheck.so for %s.%u...\n", proc_name, pid);

	/* Make sure flush_counter cannot get to zero */
	flush_counter = 999999999;
	if (fd > -1) {
		if (curr_flush_state == BUSY) {
			debug("  Flushing still in progress...\n");
			curr_flush_state = INTERRUPT;
			while (curr_flush_state != IDLE)
				wait_data(fd, true);
		}

		flushed = flush_messages();
		close(fd);
	}

	debug("  Flushed %u messages\n", flushed);

	if (pid > -1 && flushed)
		kill(pid, SIGSEGV);

	debug("Done\n");
out:
	lib_inited = false;
}

