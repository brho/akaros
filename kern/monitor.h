#ifndef JOS_KERN_MONITOR_H
#define JOS_KERN_MONITOR_H
#ifndef JOS_KERNEL
# error "This is a JOS kernel header; user programs should not #include it"
#endif

typedef struct trapframe {
} trapframe_t;

// Activate the kernel monitor,
// optionally providing a trap frame indicating the current state
// (NULL if none).
void monitor(trapframe_t *tf);

// Functions implementing monitor commands.
int mon_help(int argc, char *NTS *NT COUNT(argc) argv, trapframe_t *tf);
int mon_kerninfo(int argc, char *NTS *NT COUNT(argc) argv, trapframe_t *tf);
int mon_backtrace(int argc, char *NTS *NT COUNT(argc) argv, trapframe_t *tf);
int mon_reboot(int argc, char *NTS *NT COUNT(argc) argv, trapframe_t *tf);
int mon_showmapping(int argc, char *NTS *NT COUNT(argc) argv, trapframe_t *tf);
int mon_setmapperm(int argc, char *NTS *NT COUNT(argc) argv, trapframe_t *tf);

#endif	// !JOS_KERN_MONITOR_H
