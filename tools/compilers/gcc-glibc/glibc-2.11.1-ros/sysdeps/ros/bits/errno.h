#ifndef _BITS_ERRNO_H
#define _BITS_ERRNO_H

#ifndef __ASSEMBLER__
  extern int* __errno_location(void);
  #define errno (*__errno_location())
#endif

#include <ros/errno.h>

#endif
