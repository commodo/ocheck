#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <string.h>

#include "ocheck.h"
#include "ocheck-internal.h"
#include "frame_size.h"

static pid_t pid = -1;

static const char *leak_fname_out = "/tmp/ocheck.leaks";
const char *debug_fname_out = "/tmp/ocheck.out";

bool lib_inited = false;

static inline pid_t ourgettid()
{
	return syscall(SYS_gettid);
}

static inline pid_t ourgetpid()
{
	return syscall(SYS_getpid);
}

static uint32_t flush_messages_in_store(struct call_msg_store *store, FILE *fp, const char *name)
{
	uint32_t i, flushed = 0;
	struct call_msg *messages = store->messages;

	real_fprintf(fp, "\t\"%s\": {\n", name);
	real_fprintf(fp, "\t\t\"total_count\": %u,\n", store->total_count);
	real_fprintf(fp, "\t\t\"max_store_expansion\": %u,\n", store->max_store_expansion);
	real_fprintf(fp, "\t\t\"capacity\": %u", store->capacity);

	for (i = 0; i < store->max_store_expansion; i++) {
		if (messages[i].type == INVALID)
			continue;
		real_fprintf(fp, ",\n\t\t\"leak%u\": {\n", flushed);
		real_fprintf(fp, "\t\t\t\"ptr\" : \"0x%"PRIxPTR_PAD "\",\n", messages[i].ptr);
		real_fprintf(fp, "\t\t\t\"ptr_size\" : %u,\n", messages[i].size);
		real_fprintf(fp, "\t\t\t\"fd\" : %d\n", messages[i].fd);
		real_fprintf(fp, "\t\t}");
		flushed++;
	}

	real_fprintf(fp, ",\n\t\t\"leaks\": %u\n", flushed);

	real_fprintf(fp, "\t}");

	return flushed;
}

static void flush_messages()
{
	uint32_t flushed = 0;
	FILE *fp = real_fopen(leak_fname_out, "wb");

	if (!fp) {
		debug("  Could not open file '%s' for leak dump", leak_fname_out);
		exit(1);
	}

	real_fprintf(fp, "{\n");

	flushed += flush_messages_in_store(get_alloc_msg_store(), fp, "allocs");
	real_fprintf(fp, ",\n");
	flushed += flush_messages_in_store(get_files_msg_store(), fp, "files");

	real_fprintf(fp, "\n}\n");

	fflush(fp);
	real_fclose(fp);
	debug("  Flushed %u messages\n", flushed);
}

static inline void call_msg_invalidate(struct call_msg *msg)
{
	if (msg) {
		msg->type = INVALID;
		msg->ptr = 0;
		msg->fd = -1;
	}
}

static struct call_msg *call_msg_find_by_ptr(struct call_msg_store *store, uintptr_t ptr)
{
	int i;
	struct call_msg *messages;
	if (!ptr)
		return NULL;
	messages = store->messages;
	for (i = 0; i < store->max_store_expansion; i++) {
		if (messages[i].ptr == ptr)
			return &messages[i];
	}
	return NULL;
}

static struct call_msg *call_msg_find_by_fd(struct call_msg_store *store, int fd)
{
	int i;
	struct call_msg *messages;
	if (fd < 0)
		return NULL;
	messages = store->messages;
	for (i = 0; i < store->max_store_expansion; i++) {
		if (messages[i].fd == fd)
			return &messages[i];
	}
	return NULL;
}

static inline struct call_msg *call_msg_get_free(struct call_msg_store *store)
{
	int i;
	struct call_msg *messages = store->messages;
	for (i = 0; i < store->max_store_expansion; i++) {
		if (messages[i].type == INVALID)
			return &messages[i];
	}
	if (store->max_store_expansion < store->capacity)
		return &messages[store->max_store_expansion++];
	return NULL;
}

static void store_message(struct call_msg_store *store, struct call_msg *msg,
	uintptr_t ptr, int fd, size_t size)
{
	store->total_count++;
	if (!msg)
		msg = call_msg_get_free(store);
	if (!msg)
		debug_exit("No more room to store calls\n");

	msg->magic = MSG_MAGIC_NUMBER;
	msg->type  = store->type;
	msg->tid   = ourgettid();
	msg->ptr   = ptr;
	msg->fd    = fd;
	msg->size  = size; 
}

void store_message_by_ptr(struct call_msg_store *store, uintptr_t ptr, size_t size)
{
	struct call_msg *msg;
	if (!ptr)
		return;
	msg = call_msg_find_by_ptr(store, ptr);
	store_message(store, msg, ptr, -1, size);
}

void store_message_by_fd(struct call_msg_store *store, int fd)
{
	struct call_msg *msg;
	if (fd < 0)
		return;
	msg = call_msg_find_by_fd(store, fd);
	store_message(store, msg, 0, fd, 0);
}

void remove_message_by_ptr(struct call_msg_store *store, uintptr_t ptr)
{
	call_msg_invalidate(call_msg_find_by_ptr(store, ptr));
}

void remove_message_by_fd(struct call_msg_store *store, int fd)
{
	call_msg_invalidate(call_msg_find_by_fd(store, fd));
}

void update_message_ptr_by_fd(struct call_msg_store *store, uintptr_t ptr, int fd)
{
	struct call_msg *msg;
	if (fd < 0)
		return;
	if ((msg = call_msg_find_by_fd(store, fd)))
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

	if (!(proc = real_fopen(file, "r")) ||
	    !(a = fread(file, 1, sizeof(file), proc))) {
		if (proc)
			real_fclose(proc);
		return NULL;
	}
	real_fclose(proc);

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

	if (!proc_name || !strlen(proc_name))
		debug_exit("No 'PROC' env var specified\n");

	if (!(actual_proc_name = progname(pid)))
		debug_exit("Could not get actual program name\n");

	if (strcmp(actual_proc_name, proc_name))
		return NULL;

	return proc_name;
}

static void ocheck_init_store(struct call_msg_store *store)
{
	int i;
	for (i = 0; i < store->capacity; i++)
		store->messages[i].fd = -1;
}

/* Processes that support ocheck should implement theses
   hooks so this lib can override them and do init + fini
 */

static __attribute__((constructor(101))) void ocheck_init()
{
	const char *proc_name;
	const char *s;

	/* Do not re-initialize if already initialized and it's the same pid */
	if (lib_inited && (pid == ourgetpid()))
		return;

	initialize_file_hooks_for_debug();

	pid = ourgetpid();
	if (pid < 0)
		debug_exit("Could not get pid\n");
	if (!(proc_name = is_this_the_right_proc()))
		return;

	if ((s = getenv("DEBUG_OUTPUT")) && strlen(s))
		debug_fname_out = s;
	if ((s = getenv("LEAKS_OUTPUT")) && strlen(s))
		leak_fname_out = s;

	unlink(debug_fname_out);

	debug("Initializing libocheck.so for %s.%u...\n", proc_name, pid);
	debug("Leaks file output: '%s'\n", leak_fname_out);

	ocheck_init_store(get_alloc_msg_store());
	ocheck_init_store(get_files_msg_store());

	debug("done\n");
	lib_inited = true;
}

static __attribute__((destructor(101))) void ocheck_fini()
{
	const char *proc_name;

	/* Prevent forks from calling de-init code */
	if (!lib_inited || pid != ourgetpid())
		return;

	if (!(proc_name = is_this_the_right_proc()))
		goto out;

	debug("Uninitializing libocheck.so for %s.%u...\n", proc_name, pid);

	flush_messages();

	debug("Done\n");
out:
	lib_inited = false;
}

