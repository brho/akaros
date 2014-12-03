#ifndef _SYSDEP_H
#define _SYSDEP_H

#ifdef __ASSEMBLER__
# define PTR_MANGLE(dst,src,tmp) mov src,dst
# define PTR_DEMANGLE(dst,src,tmp) PTR_MANGLE(dst,src,tmp)
# define PTR_MANGLE2(dst,src,tmp) PTR_MANGLE(dst,src,tmp)
# define PTR_DEMANGLE2(dst,src,tmp) PTR_MANGLE2(dst,src,tmp)
#else
# define PTR_MANGLE(a) (a)
# define PTR_DEMANGLE(a) (a)
#endif

#define INTERNAL_SYSCALL_DECL(err) do { } while (0)

#define INTERNAL_SYSCALL(name, err, nr, args...) (0)

#  define cfi_startproc                 .cfi_startproc
#  define cfi_endproc                   .cfi_endproc
#  define cfi_def_cfa(reg, off)         .cfi_def_cfa reg, off
#  define cfi_def_cfa_register(reg)     .cfi_def_cfa_register reg
#  define cfi_def_cfa_offset(off)       .cfi_def_cfa_offset off
#  define cfi_adjust_cfa_offset(off)    .cfi_adjust_cfa_offset off
#  define cfi_offset(reg, off)          .cfi_offset reg, off
#  define cfi_rel_offset(reg, off)      .cfi_rel_offset reg, off
#  define cfi_register(r1, r2)          .cfi_register r1, r2
#  define cfi_return_column(reg)        .cfi_return_column reg
#  define cfi_restore(reg)              .cfi_restore reg
#  define cfi_same_value(reg)           .cfi_same_value reg
#  define cfi_undefined(reg)            .cfi_undefined reg
#  define cfi_remember_state            .cfi_remember_state
#  define cfi_restore_state             .cfi_restore_state
#  define cfi_window_save               .cfi_window_save
#  define cfi_personality(enc, exp)     .cfi_personality enc, exp
#  define cfi_lsda(enc, exp)            .cfi_lsda enc, exp

#define C_SYMBOL_NAME(name) name
#define C_LABEL(name) name ## :

#define ENTRY(name)                     \
	.align  4;                      \
	.global C_SYMBOL_NAME(name);    \
	.type   name, @function;        \
C_LABEL(name)                           \
	cfi_startproc;

#define END(name)                       \
	cfi_endproc;                    \
	.size name, . - name

#define LOC(name)  .L##name

#endif
