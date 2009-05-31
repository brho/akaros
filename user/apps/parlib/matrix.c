#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <arch/types.h>

extern char * readline(const char *prompt);
extern void draw_nanwan();
extern void clrscrn(int leaverows);

void help() {
	printf("Possible commands to run:\n"
	       "  draw_nanwan: Draw a picture of Nanwan, our mascot giraffe\n"
	       "  clear_screen:     Clear the Screen\n"
	      );
}

int main(int argc, char** argv)
{	
	printf("Welcome to the Tessellation OS newlib test suite!\n");
	printf("Enter at you're own risk....\n");
	clrscrn(2);
	while(1) {
		char* s = readline("nanwan@ros$ ");
		printf("%s\n", s);
		if(strcmp(s, "draw_nanwan") == 0)
			draw_nanwan();		
		else if(strcmp(s, "clear_screen") == 0)
			clrscrn(0);		
		else 
			help();	

	}	
	return 0;
}

