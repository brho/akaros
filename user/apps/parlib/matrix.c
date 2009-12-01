#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <ros/common.h>
#include <sys/stat.h>

extern char * readline(const char *prompt);
extern void draw_nanwan();
extern void clrscrn(int leaverows);
extern void change_user();
extern void set_default_user();
extern void file_io();
extern void file_error();
extern void run_binary();
extern void run_binary_colored();
extern char prompt[256];

void help() {
	printf("Possible commands to run:\n"
	       "  draw_nanwan:      Draw a picture of Nanwan, our mascot giraffe\n"
	       "  clear_screen:     Clear the Screen\n"
	       "  change_user:      Change Username\n"
           "  file_io:          Run File Related IO Tests\n"
           "  file_error:       Run File Error Related Tests\n"
           "  run_binary:       Load and run a binary located on the remote server\n"
           "  run_binary_colored:       Load and run a binary located on the remote server with a specified number of page colors\n"
	      );
}

int main(int argc, char** argv)
{	
	set_default_user();
	printf("Welcome to the Tessellation OS newlib test suite!\n");
	printf("Enter at your own risk....\n");
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
		else if (strcmp(s, "file_error") == 0)
			file_error();
		else if (strcmp(s, "run_binary") == 0)
			run_binary();
		else if (strcmp(s, "run_binary_colored") == 0)
			run_binary_colored();
		else
			help();	

	}	
	return 0;
}
