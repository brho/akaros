#pragma once

#include <parlib/uthread.h>

struct d9_regs {
  uint64_t reg_rax;
  uint64_t reg_rbx;
  uint64_t reg_rcx;
  uint64_t reg_rdx;
  uint64_t reg_rsp;
  uint64_t reg_rbp;
  uint64_t reg_rsi;
  uint64_t reg_rdi;
  uint64_t reg_rip;
  uint64_t reg_r8;
  uint64_t reg_r9;
  uint64_t reg_r10;
  uint64_t reg_r11;
  uint64_t reg_r12;
  uint64_t reg_r13;
  uint64_t reg_r14;
  uint64_t reg_r15;
  uint64_t reg_eflags;
  uint64_t reg_cs;
  uint64_t reg_ss;
  uint64_t reg_ds;
  uint64_t reg_es;
  uint64_t reg_fs;
  uint64_t reg_gs;
};

int d9_fetch_registers(struct uthread *t, struct d9_regs *resp);
