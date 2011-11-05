#ifndef _ROS_LDSODEFS_H
#define _ROS_LDSODEFS_H

#define HAVE_AUX_VECTOR

/* More recent versions of binutils mark SysV objects as Linux objects
 * if they use certain GNU extensions (which we do).  We could solve
 * this problem directly by declaring and using ELFOSABI_AKAROS,
 * but that requires changes to all of GCC, BFD, GAS and GLIBC.
 * Instead, we take the easy way out: accept all SysV and Linux objects.
 * Most importantly, both Kevin and Andrew concur on this matter. */

#define VALID_ELF_HEADER(hdr, exp, size) \
  ({                                                              \
    static const unsigned char ros_expected[EI_NIDENT] =          \
    {                                                             \
      [EI_MAG0] = ELFMAG0,                                        \
      [EI_MAG1] = ELFMAG1,                                        \
      [EI_MAG2] = ELFMAG2,                                        \
      [EI_MAG3] = ELFMAG3,                                        \
      [EI_CLASS] = ELFW(CLASS),                                   \
      [EI_DATA] = byteorder,                                      \
      [EI_VERSION] = EV_CURRENT,                                  \
      [EI_OSABI] = ELFOSABI_LINUX,                                \
      [EI_ABIVERSION] = 0                                         \
    };                                                            \
    !memcmp(hdr, ros_expected, size) || !memcmp(hdr, exp, size);  \
  })

#define VALID_ELF_OSABI(osabi) \
  ((osabi) == ELFOSABI_SYSV || (osabi) == ELFOSABI_LINUX)

#define VALID_ELF_ABIVERSION(osabi,ver) (ver == 0)

#include_next <ldsodefs.h>

#endif
