#ifndef _BITS_ERRNO_H
#define _BITS_ERRNO_H

#ifndef __ASSEMBLER__

int *__errno_location_tls(void);
char *__errstr_location_tls(void);
extern int *(*ros_errno_loc)(void);
extern char *(*ros_errstr_loc)(void);
int *__errno_location(void);
#define errno (*__errno_location())
char *errstr(void); 	/* can't macro, errstr is used internally in libc */
/* this is defined in init-first.c, but declared here for easy #includes */
void werrstr(const char *fmt, ...);

# ifdef libc_hidden_proto
libc_hidden_proto(__errno_location_tls)
libc_hidden_proto(__errstr_location_tls)
libc_hidden_proto(errstr)
# endif

#endif

#include <ros/errno.h>

#endif
