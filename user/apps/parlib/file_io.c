#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#define IN_BUF_SIZE 1024

extern char * readline(const char *prompt);

void file_io()
{	
	printf("Beginning Serial Based File IO Test...\n\n");
	int in_fd = open("./test/input", O_RDWR, 0);
	char buf[IN_BUF_SIZE];
	int read_amt = read(in_fd, buf, IN_BUF_SIZE - 1);
	buf[read_amt] = '\0';
	printf("Opened:       input\n");
	printf("FD:           %d\n", in_fd);
	printf("Read:         %d bytes\n", read_amt);
	printf("Data read:    %s", buf);


	char * readline_result = readline("\nEnter filename for writing: ");
	if (readline_result == NULL) {
		printf("Error reading from console.\n");
		return;
	}

    unsigned int fname_len = strlen(readline_result) + 1;
	char * file_name = malloc(fname_len);
	strncpy(file_name, readline_result, fname_len);

	readline_result = readline("Enter text to write to file: ");

        if (readline_result == NULL) {
                printf("Error reading from console.\n");
                return;
        }


	unsigned int buf2_len = strlen(readline_result) + 1;
	char *buf2 = malloc(buf2_len);
	strncpy(buf2, readline_result, buf2_len);


	unsigned int ofp_len = strlen(file_name) + 8;
	char * output_full_path = malloc(ofp_len);
	snprintf(output_full_path, ofp_len, "./test/%s", file_name);

	int out_fd = open(output_full_path, O_RDWR | O_CREAT | O_TRUNC,
                      S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);

        printf("\nOpened:       %s\n", file_name);
        printf("FD:           %d\n", out_fd);


	int write_amt = write(out_fd, buf2, strlen(buf2));
	
        printf("Wrote:        %d bytes\n", write_amt);


        int in_fd2 = open(output_full_path, O_RDWR, 0);
        read_amt = read(in_fd2, buf, IN_BUF_SIZE - 1);
        buf[read_amt] = '\0';
        printf("Data written: %s\n\n", buf);

	printf("Closing remote file descriptor: %d.... %s\n", in_fd, ((close(in_fd) == 0) ? "successful" : "failure"));
	printf("Closing remote file descriptor: %d.... %s\n", out_fd, ((close(out_fd) == 0) ? "successful" : "failure"));
	close(in_fd2);

	printf("\nTests Complete.\n\n");
}
