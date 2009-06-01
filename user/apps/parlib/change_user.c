#include <stdio.h>
#include <unistd.h>

#define DEFAULT_USER "nanwan"
#define DEFAULT_HOST "ros"

void set_default_user();
void set_user();
void change_user();

extern char * readline(const char *prompt);


char prompt[256];

void set_default_user() {
	set_user(DEFAULT_USER);
}

void set_user(char * user) {

	sprintf(prompt, "%s@%s$ ", user, DEFAULT_HOST);
}

void change_user() {
	char *s = readline("Enter new username: ");

	if (s == NULL)
		printf("Error: Could not change user!\n");	
	else
		set_user(s);
}



