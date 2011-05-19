#ifndef ROS_KERN_KDEBUG_H
#define ROS_KERN_KDEBUG_H

#include <ros/common.h>
#include <arch/kdebug.h>

// Debug information about a particular instruction pointer
typedef struct eipdebuginfo {
	const char *eip_file;		// Source code filename for EIP
	int eip_line;				// Source code linenumber for EIP

	const char *eip_fn_name;	// Name of function containing EIP
								//  - Note: not null terminated!
	int eip_fn_namelen;			// Length of function name
	uintptr_t eip_fn_addr;		// Address of start of function
	int eip_fn_narg;			// Number of function arguments
} eipdebuginfo_t;

int debuginfo_eip(uintptr_t eip, eipdebuginfo_t *NONNULL info);
void *debug_get_fn_addr(char *fn_name);
void backtrace(void);

/* Arch dependent, listed here for ease-of-use */
static inline uintptr_t get_caller_pc(void);

/* Returns a null-terminated string with the function name for a given PC /
 * instruction pointer.  kfree() the result. */
char *get_fn_name(uintptr_t pc);

#endif /* ROS_KERN_KDEBUG_H */
