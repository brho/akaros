
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define min(a, b)                               \
    ({ __typeof__(a) _a = (a);                  \
		__typeof__(b) _b = (b);                 \
		_a < _b ? _a : _b; })
#define max(a, b)                               \
    ({ __typeof__(a) _a = (a);                  \
		__typeof__(b) _b = (b);                 \
		_a > _b ? _a : _b; })

static void usage(const char *prg)
{
	fprintf(stderr, "Use: %s -s SERVER_IP -p PORT -i FILE\n", prg);
	exit(1);
}

int main(int argc, const char **argv)
{
	int i, sfd, ffd, port = -1;
	off_t written;
	const char *sip = NULL, *fpath = NULL;
	struct sockaddr_in address;
	struct stat stb;
	char buffer[4096];

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-s") == 0) {
			if (++i < argc)
				sip = argv[i];
		} else if (strcmp(argv[i], "-p") == 0) {
			if (++i < argc)
				port = atoi(argv[i]);
		} else if (strcmp(argv[i], "-i") == 0) {
			if (++i < argc)
				fpath = argv[i];
		}
	}
	if (port < 0 || !sip || !fpath)
		usage(argv[0]);

	ffd = open(fpath, O_RDONLY);
	if (ffd < 0) {
		fprintf(stderr, "Unable to open input file '%s': %s\n", fpath,
				strerror(errno));
		return 1;
	}
	if (fstat(ffd, &stb)) {
		fprintf(stderr, "Unable to stat input file '%s': %s\n", fpath,
				strerror(errno));
		return 1;
	}

	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons(port);
	if (inet_pton(AF_INET, sip, &address.sin_addr) <= 0) {
		fprintf(stderr, "Invalid server IP '%s': %s\n", sip, strerror(errno));
		return 1;
	}
	sfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sfd < 0) {
		fprintf(stderr, "Invalid create stream socket: %s\n", strerror(errno));
		return 1;
	}
	if (connect(sfd, (struct sockaddr *) &address, sizeof(address)) < 0) {
		fprintf(stderr, "Unable to connect to server IP '%s': %s\n", sip,
				strerror(errno));
		return 1;
	}

	written = 0;
	while (written < stb.st_size) {
		size_t csize = min((off_t) sizeof(buffer), stb.st_size - written);

		if (read(ffd, buffer, csize) != csize) {
			fprintf(stderr, "Unable to read input file '%s': %s\n", fpath,
					strerror(errno));
			return 1;
		}
		if (write(sfd, buffer, csize) != csize) {
			fprintf(stderr, "Unable to write to server IP '%s': %s\n", sip,
					strerror(errno));
			return 1;
		}

		written += csize;
	}

	close(sfd);

	return 0;
}
