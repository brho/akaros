/* Plan9 style error popping.  For details, read Documentation/plan9.txt */

#ifndef ROS_KERN_ERR_H
#define ROS_KERN_ERR_H

#include <setjmp.h>
#include <kthread.h>
#include <error.h>

#define ERRSTACK(x) struct errbuf *prev_errbuf; struct errbuf errstack[(x)];   \
                    int curindex = 0;
#define waserror() setjmp(&(errpush(errstack, ARRAY_SIZE(errstack), &curindex, \
									&prev_errbuf)->jmpbuf))
#define error(e, x, ...)						\
	do {										\
		if (x != NULL)													\
			set_errstr(x, ##__VA_ARGS__);								\
		else															\
			set_errstr(errno_to_string(e));								\
		set_errno(e);													\
		longjmp(&get_cur_errbuf()->jmpbuf, 1);							\
	} while(0)
#define nexterror() longjmp(&(errpop(errstack, ARRAY_SIZE(errstack), &curindex, \
									 prev_errbuf)->jmpbuf), 1)
#define poperror() errpop(errstack, ARRAY_SIZE(errstack), &curindex,	\
						  prev_errbuf)

struct errbuf *errpush(struct errbuf *errstack, int stacksize, int *curindex,
						struct errbuf **prev_errbuf);
struct errbuf *errpop(struct errbuf *errstack, int stacksize, int *curindex,
					  struct errbuf *prev_errbuf);

#endif /* ROS_KERN_ERR_H */
