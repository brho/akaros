#include <stdio.h> 
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arch/arch.h>

int main() 
{ 
	FILE *file; 
	file = fopen("/dir1/f1.txt","w+b");
	if (file == NULL)
		printf ("Failed to open file \n");
	fprintf(file,"%s","hello, world\n"); 
	fclose(file); 

	int fd = open("/bin/test.txt", O_RDWR | O_CREAT );
	breakpoint();
}
