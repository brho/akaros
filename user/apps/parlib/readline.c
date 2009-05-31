#include <unistd.h>
#include <printf.h>
#include <fcntl.h>
#include <arch/kbdreg.h>

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
			buf[i] = '\0';
			return buf;
		}
	}
}

