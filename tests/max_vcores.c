#include <stdlib.h>
#include <stdio.h>
#include <parlib.h>
#include <vcore.h>

/* Ghetto, sets its retval to max_vc to communicate without pipes */
int main(int argc, char** argv)
{
	return max_vcores();
}
