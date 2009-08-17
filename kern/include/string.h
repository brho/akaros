#ifndef ROS_INC_STRING_H
#define ROS_INC_STRING_H

#include <arch/types.h>

#define STRING char *NTS
#define STRBUF(n) char *NT COUNT(n)

int	strlen(const STRING s);
int	strnlen(const STRBUF(size) s, size_t size);
/* zra : These being used, and they are dangerous, so I'm rm'ing them
STRING	strcpy(STRING dst, const STRING src);
STRING	strcat(STRING dst, const STRING src);
*/
STRING	strncpy(STRBUF(size) dst, const STRING src, size_t size);
size_t	strlcpy(STRBUF(size-1) dst, const STRING src, size_t size);
int	strcmp(const STRING s1, const STRING s2);
int	strncmp(const STRING s1, const STRING s2, size_t size);
STRING	strchr(const STRING s, char c);
STRING	strfind(const STRING s, char c);

void *COUNT(len) memset(void *COUNT(len) dst, int c, size_t len);
void *COUNT(len) memcpy(void *COUNT(len) dst, const void *COUNT(len) src, size_t len);
void *COUNT(len) memmove(void *COUNT(len) dst, const void *COUNT(len) src, size_t len);
int	memcmp(const void *COUNT(len) s1, const void *COUNT(len) s2, size_t len);
void *BND(s,s+len)	memfind(const void *COUNT(len) s, int c, size_t len);

long	strtol(const char *NTS s, char **endptr, int base);

#endif /* not ROS_INC_STRING_H */
