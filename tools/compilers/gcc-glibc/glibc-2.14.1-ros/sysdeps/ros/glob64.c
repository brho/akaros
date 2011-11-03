#include <limits.h>

#if __WORDSIZE != 64
# include <posix/glob64.c>
#endif
