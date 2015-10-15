#ifndef ROS_INC_STRING_H
#define ROS_INC_STRING_H

#include <ros/common.h>

int	strlen(const char *s);
int	strnlen(const char *s, size_t size);
char *strstr(char *s1, char *s2);

/* zra : These aren't being used, and they are dangerous, so I'm rm'ing them
STRING	strcpy(STRING dst, const STRING src);
STRING	strcat(STRING dst, const STRING src);
*/
char *strncpy(char *dst, const char *src, size_t size);
size_t	strlcpy(char *dst, const char *src, size_t size);
size_t	strlcat(char *dst, const char *src, size_t size);
int	strcmp(const char *s1, const char *s2);
int	strncmp(const char *s1, const char *s2, size_t size);
int cistrcmp(char *s1, char *s2);
char *strchr(const char *s, char c);
char *strrchr(const char *s, char c);
char *strfind(const char *s, char c);

void *memset(void* p, int what, size_t sz);
int   memcmp(const void* s1, const void* s2, size_t sz);
void *memcpy(void* dst, const void* src, size_t sz);
void *memmove(void *dst, const void* src, size_t sz);
void *memchr(void* mem, int chr, int len);

void *memfind(const void *s, int c, size_t len);

long	strtol(const char *s, char **endptr, int base);
unsigned long strtoul(const char *s, char **endptr, int base);
int	atoi(const char*s);
int sigchecksum(void *address, int length);
void *sigscan(uint8_t *address, int length, char *signature);


/* In arch/support64.S */
void bcopy(const void *src, void *dst, size_t len);

#ifdef CONFIG_RISCV
#warning Implement bcopy
#endif

#endif /* ROS_INC_STRING_H */
