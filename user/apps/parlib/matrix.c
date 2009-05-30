#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <arch/types.h>
#include <arch/kbdreg.h>

#define CRT_ROWS	25
#define CRT_COLS	80
#define CRT_SIZE	(CRT_ROWS * CRT_COLS)

char * readline(const char *prompt);

void clrscr(int leaverows) {
for(int i=0; i<(CRT_ROWS-leaverows); i++)
	write(STDOUT_FILENO, "\n", 1);
}

int main(int argc, char** argv)
{	
	printf("Welcome to the Tessellation OS newlib test suite!\n");
	printf("Enter at you're own risk....\n");
	clrscr(3);
	while(1) {
		char* s = readline("nanwan@ros$ ");
		printf("%s\n", s);
	}	
	return 0;
}

char* readline(const char *prompt)
{
	int i, c;
	#define BUFLEN 256
	static char buf[BUFLEN];

	if (prompt != NULL)
		printf("%s", prompt); fflush(stdout);

	i = 0;
	while (1) {
		read(STDIN_FILENO, &c, 1);
		if (c < 0) {
			printf("read error: %e\n", c);
			return NULL;
		} else if (c >= ' ' && i < BUFLEN-1) {
			write(STDOUT_FILENO, &c, 1);
			buf[i++] = c;
		} else if (c == '\b' && i > 0) {
			write(STDOUT_FILENO, &c, 1);
			i--;
		} else if (c == '\n' || c == '\r') {
			write(STDOUT_FILENO, &c, 1);
			buf[i] = 0;
			return buf;
		}
	}
}

