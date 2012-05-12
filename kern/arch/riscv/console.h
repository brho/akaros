/* See COPYRIGHT for copyright information. */

#ifndef _CONSOLE_H_
#define _CONSOLE_H_
#ifndef ROS_KERNEL
# error "This is a ROS kernel header; user programs should not #include it"
#endif

#include <ros/common.h>

#define CRT_ROWS	25
#define CRT_COLS	80
#define CRT_SIZE	(CRT_ROWS * CRT_COLS)

void cons_init(void);
void keyboard_alarm_init();
/* Returns any available character, or 0 for none (legacy helper) */
int cons_get_any_char(void);

#define	FESVR_SYS_exit	1
#define	FESVR_SYS_getpid	20
#define	FESVR_SYS_kill	37
#define	FESVR_SYS_read	3
#define	FESVR_SYS_write	4
#define	FESVR_SYS_open	5
#define	FESVR_SYS_close	6
#define	FESVR_SYS_lseek	19
#define	FESVR_SYS_brk		17
#define	FESVR_SYS_link	9
#define	FESVR_SYS_unlink	10
#define	FESVR_SYS_chdir	12
#define FESVR_SYS_stat	18
#define FESVR_SYS_fstat	28
#define	FESVR_SYS_lstat	84
#define	FESVR_SYS_pread 180
#define	FESVR_SYS_pwrite 181
#define FESVR_SYS_getmainvars 201

void fesvr_die();

#endif /* _CONSOLE_H_ */
