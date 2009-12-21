#include <stdlib.h>
#include <stdio.h>

extern char * readline(const char *prompt);

void run_binary_colored()
{	
	char* colors = readline("Enter number of colors: ");
	if (colors == NULL) {
		printf("Error reading from console.\n");
		return;
	}

	extern void run_binary(size_t);
	run_binary((size_t)atoi(colors));
}

