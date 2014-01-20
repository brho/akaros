//INFERNO
#include <string.h>

/*
 * Return pointer to first occurrence of s2 in s1,
 * 0 if none
 */
char*
strstr(char *s1, char *s2)
{
	char *p;
	int f, n;

	f = s2[0];
	if(f == 0)
		return s1;
	n = strlen(s2);
	for(p=strchr(s1, f); p; p=strchr(p+1, f))
		if(strncmp(p, s2, n) == 0)
			return p;
	return 0;
}

/* Case insensitive strcmp */
int
cistrcmp(char *s1, char *s2)
{
	int c1, c2;

	while(*s1){
		c1 = *( uint8_t *)s1++;
		c2 = *( uint8_t *)s2++;

		if(c1 == c2)
			continue;

		if(c1 >= 'A' && c1 <= 'Z')
			c1 -= 'A' - 'a';

		if(c2 >= 'A' && c2 <= 'Z')
			c2 -= 'A' - 'a';

		if(c1 != c2)
			return c1 - c2;
	}
	return -*s2;
}
