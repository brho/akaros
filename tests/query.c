/*
 *  search the network database for matches
 */
#include <stdlib.h>
#include <stdio.h>
#include <parlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <error.h>
#include <iplib.h>
#include <ndb.h>

static int all, multiple;

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

void
usage(void)
{
	fprintf(stderr, "usage: query [-am] [-f ndbfile] attr value "
		"[returned-attr [reps]]\n");
	fprintf(stderr, "usage");
	exit(1);
}

/* print values of nt's attributes matching rattr */
static void
prmatch(struct ndbtuple *nt, char *rattr)
{
	for(; nt; nt = nt->entry)
		if (strcmp(nt->attr, rattr) == 0)
			printf("%s\n", nt->val);
}

void
search(struct ndb *db, char *attr, char *val, char *rattr)
{
	char *p;
	struct ndbs s;
	struct ndbtuple *t, *nt;

	/* first entry with a matching rattr */
	if(rattr && !all){
		p = ndbgetvalue(db, &s, attr, val, rattr, &t);
		if (multiple)
			prmatch(t, rattr);
		else if(p)
			printf("%s\n", p);
		ndbfree(t);
		free(p);
		return;
	}

	/* all entries with matching rattrs */
	if(rattr) {
		for(t = ndbsearch(db, &s, attr, val); t != NULL;
		    t = ndbsnext(&s, attr, val)){
			prmatch(t, rattr);
			ndbfree(t);
		}
		return;
	}

	/* all entries */
	for(t = ndbsearch(db, &s, attr, val); t; t = ndbsnext(&s, attr, val)){
		for(nt = t; nt; nt = nt->entry)
			printf("%s=%s ", nt->attr, nt->val);
		printf("\n");
		ndbfree(t);
	}
}

void
main(int argc, char **argv)
{
	int reps = 1;
	char *rattr = NULL, *dbfile = NULL;
	struct ndb *db;
	
	ARGBEGIN{
	case 'a':
		all++;
		break;
	case 'm':
		multiple++;
		break;
	case 'f':
		dbfile = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND;

	switch(argc){
	case 4:
		reps = atoi(argv[3]);	/* wtf use is this? */
		/* fall through */
	case 3:
		rattr = argv[2];
		break;
	case 2:
		rattr = NULL;
		break;
	default:
		usage();
	}

	db = ndbopen(dbfile);
	if(db == NULL){
		error(1, 0, "no db: %r");
	}
	while(reps--)
		search(db, argv[0], argv[1], rattr);
	ndbclose(db);
}

