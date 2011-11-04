#include <ros/procinfo.h>

#define DL_FIND_ARG_COMPONENTS(cookie, argc, argv, envp, auxp)	\
  do {									      \
    void **_tmp;							      \
    (argc) = 0;								      \
    while (__procinfo.argp[(argc)])					      \
      (argc)++;								      \
    (argv) = (char **) __procinfo.argp;					      \
    (envp) = (argv) + (argc) + 1;					      \
    for (_tmp = (void **) (envp); *_tmp; ++_tmp)			      \
      continue;								      \
    (auxp) = (void *) ++_tmp;						      \
  } while (0)

#include <elf/dl-sysdep.c>
