#ifndef ROS_INC_LIMITS_H
#define ROS_INC_LIMITS_H

/* Keep this 255 to stay in sync with glibc (expects d_name[256]) */
#define MAX_FILENAME_SZ 255
/* POSIX / glibc name: */
#define NAME_MAX MAX_FILENAME_SZ

#define PATH_MAX 4096 /* includes null-termination */

/* # bytes of args + environ for exec()  (i.e. max size of argenv) */
#define ARG_MAX (32 * 4096) /* Good chunk of our 256 page stack */

/* This partitions the FD space.  Negative values are errors (bit 31).  Bits
 * 0-19 are for kernel FDs.  Bits 20-30 are for userspace shims. */
#define NR_FILE_DESC_MAX (1 << 19)

#endif /* ROS_INC_LIMITS_H */
