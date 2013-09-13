#include <stdlib.h>
#include <stdio.h>
#include <parlib.h>
#include <unistd.h>
#include <signal.h>
#include <nixip.h>

#include <sys/types.h>

void
main(void)
{
	struct ipifc *ifc, *list;
	struct iplifc *lifc;
	int i;

	//	fmtinstall('I', eipfmt);
	//	fmtinstall('M', eipfmt);

	list = readipifc("/net", NULL, -1);
	for(ifc = list; ifc; ifc = ifc->next){
		printd("ipifc %s %d\n", ifc->dev, ifc->mtu);
		for(lifc = ifc->lifc; lifc; lifc = lifc->next)
			printd("\t%I %M %I\n", lifc->ip, lifc->mask, lifc->net);
	}
}
