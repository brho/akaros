#include <stdio.h> 
int main() 
{ 
	FILE *file; 
	file = fopen("test.txt","w+b");
	if (file == NULL)
		printf ("Failed to write to file \n");
	fprintf(file,"%s","hello, world\n"); 
	fclose(file); 
}
