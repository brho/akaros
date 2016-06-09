#pragma once

#include <parlib/arch/debug.h>
#include <parlib/uthread.h>

enum d9_msg_t {
	D9_TREADMEM = 10,
	D9_RREADMEM,
	D9_TSTOREMEM,
	D9_RSTOREMEM,
	D9_TFETCHREG,
	D9_RFETCHREG,
	D9_TERROR, /* Do not use. */
	D9_RERROR,
	D9_NUM_MESSAGES, /* Do not use. */
};

struct d9_header {
	uint32_t size;
	uint32_t msg_type;
} __attribute__((packed));

struct d9_rerror_msg {
	uint32_t errnum;
} __attribute__((packed));

struct d9_rerror {
	struct d9_header hdr;
	struct d9_rerror_msg msg;
} __attribute__((packed));

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

#define D9_INIT_HDR(len, msgt)                                                 \
	{                                                                          \
		.hdr = {.size = (len), .msg_type = (msgt) }                            \
	}

/* Server-side ops. */
struct d9_ops {
	int (*read_memory)(const struct d9_treadmem_msg *req,
	                   struct d9_rreadmem_msg *resp);
	int (*store_memory)(const struct d9_tstoremem_msg *req);
	int (*fetch_registers)(struct uthread *t, struct d9_regs *resp);
};

/* Server-side functions. */
void d9s_init(struct d9_ops *debug_ops);
int d9s_read_memory(const struct d9_treadmem_msg *req,
                    struct d9_rreadmem_msg *resp);
int d9s_store_memory(const struct d9_tstoremem_msg *req);

/* Client-side ops. */
int d9c_read_memory(int fd, uintptr_t address, uint32_t length, uint8_t *buf);
int d9c_store_memory(int fd, uintptr_t address, const void *const data,
                     uint32_t length);
int d9c_fetch_registers(int fd, uint64_t tid, struct d9_regs *regs);
