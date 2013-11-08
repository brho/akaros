#include <stdlib.h>
#include <stdio.h>
#include <parlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <error.h>
void	usage(void);
void	catch(void*, char*);

char *keyspec = "";

#if 0
int
amount0(int fd, char *mntpt, int flags, char *aname, char *keyspec)
{
	int rv, afd;
	AuthInfo *ai;

	afd = fauth(fd, aname);
	if(afd >= 0){
		ai = auth_proxy(afd, amount_getkey, "proto=p9any role=client %s", keyspec);
		if(ai != NULL)
			auth_freeAI(ai);
		else
			fprintf(stderr, "%s: auth_proxy: %r\n", argv0);
	}
	rv = mount(fd, afd, mntpt, flags, aname);
	if(afd >= 0)
		close(afd);
	return rv;
}
#endif
char *argv0;

void
main(int argc, char *argv[])
{
	char *spec;
	ulong flag = 1;
	int qflag = 0;
	int noauth = 1;
	int fd, rv;

	argv0 = argv[0];
	argc--,argv++;
	while (argc){
		if (*argv[0] != '-')
			break;
		switch(argv[0][1]){
		case 'a':
			flag |= 2;
			break;
		case 'b':
			flag |= 1;
			break;
		case 'c':
			flag |= 4;
			break;
			/*
			  case 'C':
			  flag |= MCACHE;
			  break;

		case 'k':
			keyspec = EARGF(usage());
			break;
			*/
		case 'n':
			noauth = 1;
			break;
		case 'q':
			qflag = 1;
			break;
		default:
			usage();
		}
		argc--,argv++;
	}
	
	spec = 0;
	if(argc == 2)
		spec = "";
	else if(argc == 3)
		spec = argv[2];
	else
		usage();

	if((flag&2)&&(flag&1))
		usage();

	fd = open(argv[0], O_RDWR);
	if(fd < 0){
		if(qflag)
			exit(0);
		fprintf(stderr, "%s: can't open %s: %r\n", argv0, argv[0]);
		exit(1);
	}


	if(noauth){
		rv = syscall(SYS_nmount, fd, argv[1], strlen(argv[1]), flag);
	}else {
		printf("auth: not yet\n"); exit(1);
		//rv = amount0(fd, argv[1], flag, spec, keyspec);
	}
	if(rv < 0){
		if(qflag)
			exit(0);
		fprintf(stderr, "%s: mount %s %r\n", argv0, argv[1]);
		exit(1);
	}
}

void
usage(void)
{
	fprintf(stderr, "usage: mount [-a|-b] [-cnq] [-k keypattern] /srv/service dir [spec]\n");
	fprintf(stderr, "usage");
	exit(1);
}
