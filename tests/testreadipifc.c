#include <stdlib.h>
#include <stdio.h>
#include <parlib.h>
#include <nixip.h>

#include <sys/types.h>

void
main(int argc, char *argv[])
{
	struct ipifc *ifc, *list;
	struct iplifc *lifc;
	int i;

	//	fmtinstall('I', eipfmt);
	//	fmtinstall('M', eipfmt);

	list = readipifc(argv[1], NULL, -1);
	for(ifc = list; ifc; ifc = ifc->next){
		printf("ipifc %s %d\n", ifc->dev, ifc->mtu);
		for(lifc = ifc->lifc; lifc; lifc = lifc->next)
			printf("\t%I %M %I\n", lifc->ip, lifc->mask, lifc->net);
	}
}
