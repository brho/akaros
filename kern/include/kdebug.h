#ifndef ROS_KERN_KDEBUG_H
#define ROS_KERN_KDEBUG_H

#include <ros/common.h>

// Debug information about a particular instruction pointer
typedef struct Eipdebuginfo {
	const char *NTS eip_file;		// Source code filename for EIP
	int eip_line;				// Source code linenumber for EIP

	const char *NT COUNT(eip_fn_namelen) eip_fn_name;	// Name of function containing EIP
								//  - Note: not null terminated!
	int eip_fn_namelen;			// Length of function name
	uintptr_t eip_fn_addr;		// Address of start of function
	int eip_fn_narg;			// Number of function arguments
} eipdebuginfo_t;

int debuginfo_eip(uintptr_t eip, eipdebuginfo_t *NONNULL info);
void *debug_get_fn_addr(char *fn_name);

#endif
