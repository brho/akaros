#include <netif.h>

enum
{
	Eaddrlen	= 6,
	ETHERMINTU	= 60,		/* minimum transmit size */
	ETHERMAXTU	= 1514,		/* maximum transmit size */
	ETHERHDRSIZE	= 14,		/* size of an ethernet header */

	Maxstruct ether	= 48,
	Ntypes		= 8,
};

struct ether {

	int	ctlrno;
	int	tbdf;			/* type+busno+devno+funcno */
	uchar	ea[Eaddrlen];

	void	(*attach)(struct ether*);	/* filled in by reset routine */
	void	(*detach)(struct ether*);
	void	(*transmit)(struct ether*);
	void	(*interrupt)(Ureg*, void*);
	long	(*ifstat)(struct ether*, void*, long, ulong);
	long 	(*ctl)(struct ether*, void*, long); /* custom ctl messages */
	void	(*power)(struct ether*, int);	/* power on/off */
	void	(*shutdown)(struct ether*);	/* shutdown hardware before reboot */
	void	*ctlr;

	int	scan[Ntypes];		/* base station scanning interval */
	int	nscan;			/* number of base station scanners */

	struct netif netif;
};

struct etherpkt
{
	uchar	d[Eaddrlen];
	uchar	s[Eaddrlen];
	uchar	type[2];
	uchar	data[1500];
};

extern struct block* etheriq(struct ether*, struct block*, int);
extern void addethercard(char*, int(*)(struct ether*));
extern ulong ethercrc(uchar*, int);
extern int parseether(uchar*, char*);

#define NEXT(x, l)	(((x)+1)%(l))
#define PREV(x, l)	(((x) == 0) ? (l)-1: (x)-1)
