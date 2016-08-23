#pragma once

#include <parlib/arch/debug.h>
#include <parlib/uthread.h>

/* Message types of D9.
 * T messages have to be even, R messages have to be odd. T messages are sent
 * from gdbserver to 2LS, R messages the other way.
 *
 * If you modify this, also make sure that srv_msg_handlers and clt_msg_handlers
 * are still correct in debug.c.
 */
enum d9_msg_t {
	D9_TREADMEM = 10,
	D9_RREADMEM,
	D9_TSTOREMEM,
	D9_RSTOREMEM,
	D9_TFETCHREG,
	D9_RFETCHREG,
	D9_TSTOREREG,
	D9_RSTOREREG,
	D9_TERROR, /* Do not use. */
	D9_RERROR,
	D9_THITBREAKPOINT,
	D9_RHITBREAKPOINT,
	D9_TRESUME,
	D9_RRESUME,
	D9_TADDTHREAD,
	D9_RADDTHREAD,
	D9_TINIT,
	D9_RINIT,
	D9_NUM_MESSAGES, /* Do not use. */
};

#define IS_MSG_T(type) ((type) % 2 == 0)
#define IS_MSG_R(type) ((type) % 2 == 1)

struct d9_header {
	uint32_t size;
	uint32_t msg_type;
} __attribute__((packed));

/* Initialization message. */
struct d9_tinit {
	struct d9_header hdr;
} __attribute__((packed));

struct d9_rinit {
	struct d9_header hdr;
} __attribute__((packed));

/* Error message */
struct d9_rerror_msg {
	uint32_t errnum;
} __attribute__((packed));

struct d9_rerror {
	struct d9_header hdr;
	struct d9_rerror_msg msg;
} __attribute__((packed));

/* reading memory */
struct d9_treadmem_msg {
	uintptr_t address;
	uint32_t length;
} __attribute__((packed));

struct d9_treadmem {
	struct d9_header hdr;
	struct d9_treadmem_msg msg;
} __attribute__((packed));

struct d9_rreadmem_msg {
	uint32_t length;
	uint8_t data[]; /* Variable length; must be the last member. */
} __attribute__((packed));

struct d9_rreadmem {
	struct d9_header hdr;
	struct d9_rreadmem_msg msg;
} __attribute__((packed));

/* storing memory */
struct d9_tstoremem_msg {
	uintptr_t address;
	uint32_t length;
	uint8_t data[]; /* Variable length; must be the last member. */
} __attribute__((packed));

struct d9_tstoremem {
	struct d9_header hdr;
	struct d9_tstoremem_msg msg;
} __attribute__((packed));

struct d9_rstoremem {
	struct d9_header hdr;
} __attribute__((packed));

/* fetching registers */
struct d9_tfetchreg_msg {
	uint64_t threadid;
} __attribute__((packed));

struct d9_tfetchreg {
	struct d9_header hdr;
	struct d9_tfetchreg_msg msg;
} __attribute__((packed));

struct d9_rfetchreg_msg {
	struct d9_regs regs; /* Architecture-dependent. */
} __attribute__((packed));

struct d9_rfetchreg {
	struct d9_header hdr;
	struct d9_rfetchreg_msg msg;
} __attribute__((packed));

/* storing registers */
struct d9_tstorereg_msg {
	uint64_t threadid;
	struct d9_regs regs;
} __attribute__((packed));

struct d9_tstorereg {
	struct d9_header hdr;
	struct d9_tstorereg_msg msg;
} __attribute__((packed));

struct d9_rstorereg {
	struct d9_header hdr;
} __attribute__((packed));

/* resuming */
struct d9_tresume_msg {
	uint64_t tid;
	bool singlestep : 1;
} __attribute__((packed));

struct d9_tresume {
	struct d9_header hdr;
	struct d9_tresume_msg msg;
} __attribute__((packed));

struct d9_rresume {
	struct d9_header hdr;
} __attribute__((packed));

/* hitting a breakpoint */
struct d9_thitbreakpoint_msg {
	pid_t pid;
	uint64_t tid;
	uint64_t address;
} __attribute__((packed));

struct d9_thitbreakpoint {
	struct d9_header hdr;
	struct d9_thitbreakpoint_msg msg;
} __attribute__((packed));

/* adding a thread */
struct d9_taddthread_msg {
	pid_t pid;
	uint64_t tid;
} __attribute__((packed));

struct d9_taddthread {
	struct d9_header hdr;
	struct d9_taddthread_msg msg;
} __attribute__((packed));

#define D9_INIT_HDR(len, msgt)                                                 \
	{                                                                          \
		.hdr = {.size = (len), .msg_type = (msgt) }                            \
	}

/* 2LS ops.
 *
 * These represent the actual operations to be carried out for each message
 * type sent to the 2LS. This serves to keep the actual operations separate from
 * the implementation of the protocol itself.
 */
struct d9_ops {
	int (*read_memory)(const struct d9_treadmem_msg *req,
	                   struct d9_rreadmem_msg *resp);
	int (*store_memory)(const struct d9_tstoremem_msg *req);
	int (*fetch_registers)(struct uthread *t, struct d9_regs *resp);
	int (*store_registers)(struct uthread *t, struct d9_regs *resp);
	void (*resume)(struct uthread *t, bool singlestep);
};

/* gdbserver ops.
 *
 * These represent the actual operations to be carried out for each message type
 * sent to gdbserver.
 */
struct d9c_ops {
	/* hit_breakpoint is called when the process hits a breakpoint. */
	int (*hit_breakpoint)(pid_t pid, uint64_t tid, uint64_t address);

	/* add_thread is called when the process adds a new thread. */
	int (*add_thread)(pid_t pid, uint64_t tid);
};

/* 2LS-side functions. */
void d9s_init(struct d9_ops *debug_ops);

/* Implementations of d9_ops. */
int d9s_read_memory(const struct d9_treadmem_msg *req,
                    struct d9_rreadmem_msg *resp);
int d9s_store_memory(const struct d9_tstoremem_msg *req);
void d9s_resume(struct uthread *t, bool singlestep);

/* Helpers to send messages from 2LS to gdbserver. */
int d9s_notify_hit_breakpoint(uint64_t tid, uint64_t address);
int d9s_notify_add_thread(uint64_t tid);

/* gdbserver-side functions. */
int d9c_attach(unsigned long pid);
void *d9c_read_thread(void *arg);

/* Helpers to send messages from gdbserver to 2LS. */
int d9c_read_memory(int fd, uintptr_t address, uint32_t length, uint8_t *buf);
int d9c_store_memory(int fd, uintptr_t address, const void *const data,
                     uint32_t length);
int d9c_fetch_registers(int fd, uint64_t tid, struct d9_regs *regs);
int d9c_store_registers(int fd, uint64_t tid, struct d9_regs *regs);
int d9c_resume(int fd, uint64_t tid, bool singlestep);
int d9c_init(int fd, struct d9c_ops *ops);
