#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <arch/types.h>

extern char * readline(const char *prompt);
extern void draw_nanwan();
extern void clrscrn(int leaverows);
extern void change_user();
extern void set_default_user();
extern void file_io();
extern char prompt[256];

void help() {
	printf("Possible commands to run:\n"
	       "  draw_nanwan:      Draw a picture of Nanwan, our mascot giraffe\n"
	       "  clear_screen:     Clear the Screen\n"
	       "  change_user:      Change Username\n"
               "  file_io:          Run File Related IO Tests\n"
	      );
}

int main(int argc, char** argv)
{	
	set_default_user();
	printf("Welcome to the Tessellation OS newlib test suite!\n");
	printf("Enter at you're own risk....\n");
	clrscrn(2);
	while(1) {
		char* s = readline(prompt);

		if (s == NULL)
			continue;

		if(strcmp(s, "draw_nanwan") == 0)
			draw_nanwan();		
		else if(strcmp(s, "clear_screen") == 0)
			clrscrn(0);		
		else if (strcmp(s, "change_user") == 0)
			change_user();
		else if (strcmp(s, "file_io") == 0)
			file_io();
		else
			help();	

	}	
	return 0;
}
