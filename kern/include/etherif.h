#include <netif.h>

enum {
	Eaddrlen = 6,
	ETHERMINTU = 60,	/* minimum transmit size */
	ETHERMAXTU = 1514,	/* maximum transmit size */
	ETHERHDRSIZE = 14,	/* size of an ethernet header */

	Maxether = 48,
	Ntypes = 8,
};

struct ether {
	int port;
	int irq;
	int ctlrno;
	int tbdf;					/* type+busno+devno+funcno */
	uint8_t ea[Eaddrlen];

	void (*attach) (struct ether *, struct errbuf *);	/* filled in by reset routine */
	void (*detach) (struct ether *, struct errbuf *);
	void (*transmit) (struct ether *);
	void (*interrupt) ( /*Ureg */ void *, void *);
	long (*ifstat) (struct ether *, void *, long, unsigned long,
					struct errbuf *);
	long (*ctl) (struct ether *, void *, long);	/* custom ctl messages */
	void (*power) (struct ether *, int);	/* power on/off */
	void (*shutdown) (struct ether *);	/* shutdown hardware before reboot */
	void *ctlr;

	int scan[Ntypes];			/* base station scanning interval */
	int nscan;					/* number of base station scanners */

	struct netif netif;
};

struct etherpkt {
	uint8_t d[Eaddrlen];
	uint8_t s[Eaddrlen];
	uint8_t type[2];
	uint8_t data[1500];
};

extern struct block *etheriq(struct ether *, struct block *, int);
extern void addethercard(char *, int (*)(struct ether *));
extern uint32_t ethercrc(uint8_t *, int);
extern int parseether(uint8_t *, char *);

#define NEXT(x, l)	(((x)+1)%(l))
#define PREV(x, l)	(((x) == 0) ? (l)-1: (x)-1)
