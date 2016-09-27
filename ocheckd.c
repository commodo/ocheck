#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <libubox/list.h>
#include <libubox/uloop.h>
#include <libubox/usock.h>
#include <libubus.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <string.h>
#include <getopt.h>

#include "lib/frame_size.h"
#include "lib/ocheck.h"

static const char *sock_name = NULL;
struct ocheck_client {
	struct list_head list;
	char proc[PROC_NAME_LEN];
	struct uloop_fd sock;
	uint8_t buf[32 * 1024];
	uint32_t len;
	struct list_head calls;
};

struct call {
	struct list_head list;
	struct call_msg msg;
};

static struct list_head ocheck_client_list = LIST_HEAD_INIT(ocheck_client_list);

/* Primary buffer for handling ubus calls */
static struct blob_buf b;

static int ocheckd_list_handler(struct ubus_context *ctx,
	struct ubus_object *obj,
	struct ubus_request_data *req,
	const char *method,
	struct blob_attr *msg);
static int ocheckd_clear_handler(struct ubus_context *ctx,
	struct ubus_object *obj,
	struct ubus_request_data *req,
	const char *method,
	struct blob_attr *msg);
static const struct ubus_method ocheckd_methods[] = {
	UBUS_METHOD_NOARG("list", ocheckd_list_handler),
	UBUS_METHOD_NOARG("clear", ocheckd_clear_handler),
};

static struct ubus_object_type ocheckd_object_type =
	UBUS_OBJECT_TYPE("ocheck", ocheckd_methods);

static struct ubus_object ocheckd_object = {
	.name = "ocheck",
	.type = &ocheckd_object_type,
	.methods = ocheckd_methods,
	.n_methods = ARRAY_SIZE(ocheckd_methods),
};

static bool print_to_syslog = true;
#define log(prio, ...) { \
	if (print_to_syslog) \
		syslog(prio, __VA_ARGS__); \
	else \
		fprintf(stderr, __VA_ARGS__); }

/* Auto-connect data */
static void ocheckd_connect_handler(struct ubus_context *ctx);
static struct ubus_auto_conn ubus_conn = {
	.cb = ocheckd_connect_handler
};

static void ocheckd_connect_handler(struct ubus_context *ctx)
{
	ubus_add_object(ctx, &ocheckd_object);
}

static void ocheckd_populate_list(struct list_head *lst,
	const char *name,
	enum msg_type type)
{
	struct call *call, *tmp;
	uint32_t call_count = 0;
	char buf[19];
	void *elems, *a = blobmsg_open_table(&b, name);

	elems = blobmsg_open_array(&b, "elems");

#define blobmsg_add_hex_string(b, k, v) \
	snprintf(buf, sizeof(buf), "0x%"PRIxPTR_PAD, v); \
	blobmsg_add_string(b, k, buf);

	list_for_each_entry_safe(call, tmp, lst, list) {
		struct call_msg *m = &call->msg;

		if (m->type != type)
			continue;

		int i;
		void *t = blobmsg_open_table(&b, "");

		blobmsg_add_u32(&b, "tid", m->tid);
		blobmsg_add_hex_string(&b, "ptr", m->id);
		for (i = 0; i < BACK_FRAMES_COUNT && m->frames[i]; i++) {
			char frame_name[sizeof("frameX") + 1];
			snprintf(frame_name, sizeof(frame_name), "frame%d", i);
			blobmsg_add_hex_string(&b, frame_name, m->frames[i]);
		}
		blobmsg_add_u32(&b, "size", m->size);
		call_count++;

		blobmsg_close_table(&b, t);
	}

	blobmsg_close_array(&b, elems);
	blobmsg_add_u32(&b, "count", call_count);
	blobmsg_close_table(&b, a);
}

static int ocheckd_list_handler(struct ubus_context *ctx,
	struct ubus_object *obj,
	struct ubus_request_data *req,
	const char *method,
	struct blob_attr *msg)
{
	struct ocheck_client *cl, *tmp;

	blob_buf_init(&b, 0);

	list_for_each_entry_safe(cl, tmp, &ocheck_client_list, list) {
		void *p = blobmsg_open_table(&b, cl->proc);
		ocheckd_populate_list(&cl->calls, "allocs", ALLOC);
		ocheckd_populate_list(&cl->calls, "files", FILES);
		blobmsg_close_table(&b, p);
	}

	ubus_send_reply(ctx, req, b.head);
	return 0;
}

static inline void ocheck_client_calls_clear_list(struct ocheck_client *cl)
{
	struct call *call, *tmp;
	if (!cl)
		return;
	list_for_each_entry_safe(call, tmp, &cl->calls, list) {
		list_del(&call->list);
		free(call);
	}
}

static void ocheck_client_list_clear(const char *name)
{
	struct ocheck_client *cl, *tmp;
	int slen = 0;

	if (name)
		slen = strlen(name);

	list_for_each_entry_safe(cl, tmp, &ocheck_client_list, list) {
		if (name && strncmp(cl->proc, name, slen))
			continue;
		list_del(&cl->list);
		ocheck_client_calls_clear_list(cl);
		free(cl);
	}
}

static int ocheckd_clear_handler(struct ubus_context *ctx,
	struct ubus_object *obj,
	struct ubus_request_data *req,
	const char *method,
	struct blob_attr *msg)
{
	enum {
		CLEAR_NAME_ATTR,
		__CLEAR_ATTR_MAX
	};
	static const struct blobmsg_policy temp_policy[__CLEAR_ATTR_MAX] = {
		[CLEAR_NAME_ATTR] = { .name = "name", .type = BLOBMSG_TYPE_STRING },
	};
	const char *name = NULL;


	if (msg) {
		struct blob_attr *tb[__CLEAR_ATTR_MAX];
		blobmsg_parse(temp_policy, __CLEAR_ATTR_MAX, tb, blob_data(msg), blob_len(msg));
		name = blobmsg_get_string(tb[CLEAR_NAME_ATTR]);
	}
	ocheck_client_list_clear(name);

	return 0;
}

static inline void __create_call_to_list(struct list_head *lst, struct call_msg *msg)
{
	struct call *call = calloc(1, sizeof(*call));
	if (!call) {
		log(LOG_ERR, "Could not alloc memory for call object\n");
		return;
	}

	memcpy(&call->msg, msg, sizeof(call->msg));
	list_add(&call->list, lst);
}

static inline struct call *call_find(struct list_head *lst, struct call_msg *msg)
{
	struct call *call, *tmp;

	if (list_empty(lst))
		return NULL;

	list_for_each_entry_safe(call, tmp, lst, list) {
		struct call_msg *m = &call->msg;
		if ((m->id == msg->id) && (m->type == msg->type))
			return call;
	}
	return NULL;
}

static void call_add(struct ocheck_client *cl, struct call_msg *msg)
{
	/* Sanity */
	struct call *call = call_find(&cl->calls, msg);
	if (call) {
		log(LOG_WARNING, "Duplicate memory entry found (%u)(0x%"PRIxPTR_PAD")'\n", msg->tid, msg->id);
		memcpy(&call->msg, msg, sizeof(call->msg));
		return;
	}
	__create_call_to_list(&cl->calls, msg);
}

static inline int handle_proc_name_msg(struct ocheck_client *cl, uint32_t msg_pos)
{
	struct proc_msg *msg = (struct proc_msg *)&cl->buf[msg_pos];
	if (cl->len < sizeof(struct proc_msg))
		return -1;
	strncpy(cl->proc, msg->name, sizeof(cl->proc));
	return (sizeof(struct proc_msg));
}

static inline int handle_call_msg(struct ocheck_client *cl, uint32_t msg_pos)
{
	struct call_msg *msg = (struct call_msg *)&cl->buf[msg_pos];
	if (cl->len < sizeof(struct call_msg))
		return -1;
	call_add(cl, msg);
	return (sizeof(struct call_msg));
}

static inline int handle_clear_msg(struct ocheck_client *cl, uint32_t msg_pos)
{
	if (cl->len < sizeof(struct msg_common))
		return -1;
	ocheck_client_calls_clear_list(cl);
	return (sizeof(struct msg_common));
}

static inline void ocheck_client_delete_empty(struct ocheck_client *cl)
{
	if (!cl->sock.eof)
		return;

	if (!list_empty(&cl->calls))
		return;

	list_del(&cl->list);
	ocheck_client_calls_clear_list(cl);
	free(cl);
}

static void client_cb(struct uloop_fd *u, unsigned int events)
{
	struct ocheck_client *cl = container_of(u, typeof(*cl), sock);
	struct msg_common *msg;
	uint32_t msg_pos = 0;
	int r;

	if (u->eof)
		/* Socket EOF'd/closed ; do not free client data ; we need it for analysis */
		uloop_fd_delete(u);
	while ((r = read(u->fd, &cl->buf[cl->len], sizeof(cl->buf) - cl->len)) > 0)
		cl->len += r;

	while (cl->len > 0) {
		msg = (struct msg_common *)&cl->buf[msg_pos];

		switch (msg->type) {
			case PROC_NAME:
				r = handle_proc_name_msg(cl, msg_pos);
				break;
			case FILES:
			case ALLOC:
				r = handle_call_msg(cl, msg_pos);
				break;
			case CLEAR:
				r = handle_clear_msg(cl, msg_pos);
				break;
			default:
				log(LOG_ERR, "Invalid message type: %u\n", msg->type);
				return;
		}

		if (r <= 0) {
			if (msg_pos > 0)
				memcpy(&cl->buf[0], &cl->buf[msg_pos], cl->len);
			if (u->eof)
				log(LOG_WARNING, "Socket was closed, but there were still some bytes left (%u)\n", cl->len);
			goto out;
		}

		msg_pos += r;
		cl->len -= r;
	}

out:
	ocheck_client_delete_empty(cl);
}

static struct ocheck_client *ocheck_client_new(int fd, uloop_fd_handler cb)
{
	struct ocheck_client *cl = calloc(1, sizeof(*cl));

	if (!cl)
		return NULL;

	INIT_LIST_HEAD(&cl->calls);

	cl->sock.fd = fd;
	cl->sock.cb = cb;

	list_add(&cl->list, &ocheck_client_list);

	return cl;
}

static bool get_next_connection(int fd)
{
	struct ocheck_client *cl;
	int client_fd;

	client_fd = accept(fd, NULL, 0);
	if (client_fd < 0) {
		switch (errno) {
		case ECONNABORTED:
		case EINTR:
			return true;
		default:
			return false;
		}
	}

	cl = ocheck_client_new(client_fd, client_cb);
	if (cl)
		uloop_fd_add(&cl->sock, ULOOP_READ | ULOOP_EDGE_TRIGGER);
	else
		close(client_fd);

	return true;
}

static void server_cb(struct uloop_fd *fd, unsigned int events)
{
	bool next;

	do {
		next = get_next_connection(fd->fd);
	} while (next);
}

static int process_args(int argc, char *argv[])
{
	int c;
	opterr = 0;

	while ((c = getopt(argc, argv, "ds:p:")) != -1) {
		switch (c) {
			case 'd':
				print_to_syslog = false;
				break;
			case 's':
				if (!(sock_name = optarg))
					fprintf(stderr, "Sock name needs argument\n");
				break;
			default:
				if (isprint (optopt))
					fprintf(stderr, "Unrecognized option '%c'\n", optopt);
				else
					fprintf(stderr, "Unrecognized option charater '%x'\n", optopt);
				return -1;
		}
	}
	return 0;
}

int main(int argc, char*argv[])
{
	struct uloop_fd server_fd = {
		.cb = server_cb,
	};

	if (process_args(argc, argv))
		return -1;

	if (!sock_name)
		sock_name = DEFAULT_SOCKET;

	if (uloop_init()) {
		log(LOG_ERR, "Could not initialize uloop_init()\n");
		return -1;
	}

	if (print_to_syslog)
		openlog(argv[0], LOG_PID, LOG_DAEMON);

	unlink(sock_name);
	umask(0111);
	if ((server_fd.fd = usock(USOCK_UNIX | USOCK_SERVER | USOCK_NONBLOCK, sock_name, NULL)) < 0) {
		log(LOG_ERR, "Could not open UNIX socket\n");
		goto out;
	}

	uloop_fd_add(&server_fd, ULOOP_READ | ULOOP_EDGE_TRIGGER);
	ubus_auto_connect(&ubus_conn);

	log(LOG_NOTICE, "Memory check collector started\n");

	uloop_run();
	uloop_done();

	ubus_shutdown(&ubus_conn.ctx);
	uloop_fd_delete(&server_fd);

out:
	ocheck_client_list_clear(NULL);
	unlink(sock_name);
	if (server_fd.fd > -1)
		close(server_fd.fd);
	if (print_to_syslog)
		closelog();
	return 0;
}
