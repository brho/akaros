#ifndef ROS_INC_STRING_H
#define ROS_INC_STRING_H

#include <ros/common.h>

#define STRING char *NTS
#define STRBUF(n) char *NT COUNT(n)

int	strlen(const STRING s);
int	strnlen(const STRBUF(size) s, size_t size);
STRING  strstr(char *s1, char *s2);

/* zra : These aren't being used, and they are dangerous, so I'm rm'ing them
STRING	strcpy(STRING dst, const STRING src);
STRING	strcat(STRING dst, const STRING src);
*/
STRING	strncpy(STRBUF(size) dst, const STRING src, size_t size);
size_t	strlcpy(STRBUF(size-1) dst, const STRING src, size_t size);
int	strcmp(const STRING s1, const STRING s2);
int	strncmp(const STRING s1, const STRING s2, size_t size);
int cistrcmp(char *s1, char *s2);
STRING	strchr(const STRING s, char c);
STRING	strrchr(const STRING s, char c);
STRING	strfind(const STRING s, char c);

void * (DMEMSET(1, 2, 3) memset)(void* p, int what, size_t sz);
int    (DMEMCMP(1, 2, 3) memcmp)(const void* s1, const void* s2, size_t sz);
void * (DMEMCPY(1, 2, 3) memcpy)(void* dst, const void* src, size_t sz);
void * (DMEMCPY(1, 2, 3) memmove)(void *dst, const void* src, size_t sz);
void * memchr(void* mem, int chr, int len);

void *BND(s,s+len)	memfind(const void *COUNT(len) s, int c, size_t len);

long	strtol(const char *NTS s, char **endptr, int base);
unsigned long strtoul(const char *s, char **endptr, int base);
int	atoi(const char*NTS s);
int sigchecksum(void *address, int length);
void *sigscan(uint8_t *address, int length, char *signature);

#endif /* not ROS_INC_STRING_H */
