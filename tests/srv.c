#include <stdlib.h>
#include <stdio.h>
#include <parlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>
#include <ctype.h>
#include <error.h>
#include <nixip.h>
#include <dir.h>
#include <ndb.h>
#include <fcall.h>

char	*dest = "system";
int	mountflag = 0; // MREPL;

void	rpc(int, int);
void	post(char*, int);
void	mountfs(char*, int);
int	doauth = 1;

char *argv0;
#define	ARGBEGIN	for((argv0||(argv0=*argv)),argv++,argc--;\
			    argv[0] && argv[0][0]=='-' && argv[0][1];\
			    argc--, argv++) {\
				char *_args, *_argt;\
				char _argc;		\
				_args = &argv[0][1];\
				if(_args[0]=='-' && _args[1]==0){\
					argc--; argv++; break;\
				}\
				_argc = _args[0];\
				while(*_args && _args++)\
					switch(_argc)
#define	ARGEND		/*SET(_argt);USED(_argt,_argc,_args);}USED(argv, argc);*/}
#define	ARGF()		(_argt=_args, _args="",\
				(*_argt? _argt: argv[1]? (argc--, *++argv): 0))
#define	EARGF(x)	(_argt=_args, _args="",\
				(*_argt? _argt: argv[1]? (argc--, *++argv): ((x), abort(), (char*)0)))

#define	ARGC()		_argc
static void Error(char*s);
void
usage(void)
{
	fprintf(stderr, "usage: %s [-abcCm] [net!]host [srvname [mtpt]]\n", argv0);
	fprintf(stderr, "    or %s -e [-abcCm] command [srvname [mtpt]]\n", argv0);

	fprintf(stderr, "usage");
	exit(1);
}


void
main(int argc, char *argv[])
{
	int fd;
	char *srv, *mtpt;
	char dir[1024];
	char err[1024];
	char *p, *p2;

	ARGBEGIN{
	case 'n':
		doauth = 0;
		break;
	default:
		usage();
		break;
	}ARGEND

		 srv = malloc(1024);
	switch(argc){
	case 1:	/* calculate srv from address */
		p = strrchr(argv[0], '/');
		p = p ? p+1 : argv[0];
		snprintf(srv, 1024, "#s/%s", p);
		break;
	case 2:
		snprintf(srv, 1024, "#s/%s", argv[1]);
		break;
	default:
		srv = mtpt = NULL;
		usage();
	}

	dest = *argv;

Again:

	if(access(srv, 0) == 0){
		fprintf(stderr, "srv: %s already exists\n", srv);
		exit(0);
	}

	dest = netmkaddr(dest, 0, "9fs");
	fd = dial(dest, 0, dir, 0);
	if(fd < 0) {
		fprintf(stderr, "srv: dial %s: %r\n", dest);
		exit(1);
	}

	post(srv, fd);

/* fork doesn't really work. So we just post for now. */
#if 0
Mount:
	if(domount == 0 || reallymount == 0)
		exits(0);

	if((!doauth && mount(fd, -1, mtpt, mountflag, "") < 0)
	|| (doauth && amount(fd, mtpt, mountflag, "") < 0)){
		err[0] = 0;
		errstr(err, sizeof err);
		if(strstr(err, "Hangup") || strstr(err, "hungup") || strstr(err, "timed out")){
			remove(srv);
			if(try == 1)
				goto Again;
		}
		fprint(2, "srv %s: mount failed: %s\n", dest, err);
		exits("mount");
	}
	exits(0);
#endif
}

void
post(char *srv, int fd)
{
	int f;
	char buf[128];

	fprintf(stderr, "post...\n");
	f = open(srv, O_WRONLY | O_CREAT | O_EXCL, 0666);
	if(f < 0){
		sprintf(buf, "create(%s)", srv);
		Error(buf);
	}
	sprintf(buf, "%d", fd);
	if(write(f, buf, strlen(buf)) != strlen(buf))
		Error("write");
}

static void
Error(char *s)
{
	fprintf(stderr, "srv %s: %s: %r\n", dest, s);
	fprintf(stderr, "srv: error");
	exit(1);
}
