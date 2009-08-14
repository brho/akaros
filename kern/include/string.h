#ifndef ROS_INC_STRING_H
#define ROS_INC_STRING_H

#include <arch/types.h>

#define STRING char *NTS NONNULL
#define STRBUF(n) char *NT COUNT(n) NONNULL

int	strlen(const STRING s);
int	strnlen(const STRBUF(size) s, size_t size);
char *	strcpy(STRING dst, const STRING src);
char *	strcat(char *dst, const char *src);
char *	strncpy(STRBUF(size) dst, const STRING src, size_t size);
size_t	strlcpy(STRBUF(size-1) dst, const STRING src, size_t size);
int	strcmp(const STRING s1, const STRING s2);
int	strncmp(const STRING s1, const STRING s2, size_t size);
char *	strchr(const STRING s, char c);
char *	strfind(const STRING s, char c);

void *	memset(void *COUNT(len) dst, int c, size_t len);
void *	memcpy(void *COUNT(len) dst, const void *COUNT(len) src, size_t len);
void *	memmove(void *COUNT(len) dst, const void *COUNT(len) src, size_t len);
int	memcmp(const void *COUNT(len) s1, const void *COUNT(len) s2, size_t len);
void *	memfind(const void *COUNT(len) s, int c, size_t len);

long	strtol(const char *s, char **endptr, int base);

#endif /* not ROS_INC_STRING_H */
