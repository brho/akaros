/* Debugging app.
 *
 * write_to PATH STRING
 *
 * opens PATH, writes STRING to it */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <net.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int main(int argc, char *argv[])
{
	char *path, *string;
	int fd, ret;

	if (argc != 3) {
		printf("Usage: %s PATH STRING\n", argv[0]);
		exit(-1);
	}
	path = argv[1];
	string = argv[2];

	fd = open(path, O_RDWR);
	if (fd < 0) {
		perror("Can't open path");
		exit(-1);
	}
	ret = write(fd, string, strlen(string));
	if (ret < 0) {
		perror("Failed to write string");
		close(fd);
		exit(-1);
	}
	close(fd);
}
