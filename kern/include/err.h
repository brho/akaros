/* Plan9 style error popping.  For details, read Documentation/plan9.txt */

#ifndef ROS_KERN_ERR_H
#define ROS_KERN_ERR_H

#include <setjmp.h>
#include <syscall.h>

struct errbuf {
	struct jmpbuf jmpbuf;
};

#define ERRSTACK(x) struct errbuf *prev_errbuf; struct errbuf errstack[(x)];   \
                    int curindex = 0;
#define waserror() (errpush(errstack, ARRAY_SIZE(errstack), &curindex,         \
                            &prev_errbuf) ||                                   \
                    setjmp(&(get_cur_errbuf()->jmpbuf)))
#define error(x,...) {set_errstr(x, ##__VA_ARGS__);                            \
                      longjmp(&get_cur_errbuf()->jmpbuf, 1);}
#define nexterror() {errpop(errstack, ARRAY_SIZE(errstack), &curindex,         \
                            prev_errbuf);                                      \
                     longjmp(&(get_cur_errbuf())->jmpbuf, 1);}
#define poperror() {errpop(errstack, ARRAY_SIZE(errstack), &curindex,          \
                           prev_errbuf);}

int errpush(struct errbuf *errstack, int stacksize, int *curindex,
            struct errbuf **prev_errbuf);
void errpop(struct errbuf *errstack, int stacksize, int *curindex,
            struct errbuf *prev_errbuf);

#endif /* ROS_KERN_ERR_H */
