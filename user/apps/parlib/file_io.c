#include <string.h>
#include <stdlib.h>
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

	char * file_name = malloc(strlen(readline_result) + 1);
	strcpy(file_name, readline_result);

	readline_result = readline("Enter text to write to file: ");

        if (readline_result == NULL) {
                printf("Error reading from console.\n");
                return;
        }


	char *buf2 = malloc(strlen(readline_result) + 1);
	strcpy(buf2, readline_result);


	char * output_full_path = malloc(strlen(file_name) + 8);
	sprintf(output_full_path, "./test/%s", file_name);

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
