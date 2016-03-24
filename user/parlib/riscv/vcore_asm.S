#include <sys/asm.h>

.global __pop_ros_tf_regs
__pop_ros_tf_regs:

  REG_L s0,  2*SZREG(a0)
  REG_L s1,  3*SZREG(a0)
  REG_L s2,  4*SZREG(a0)
  REG_L s3,  5*SZREG(a0)
  REG_L s4,  6*SZREG(a0)
  REG_L s5,  7*SZREG(a0)
  REG_L s6,  8*SZREG(a0)
  REG_L s7,  9*SZREG(a0)
  REG_L s8, 10*SZREG(a0)
  REG_L s9, 11*SZREG(a0)
  REG_L s10,12*SZREG(a0)
  REG_L s11,13*SZREG(a0)
  REG_L sp, 14*SZREG(a0)

  REG_L ra,33*SZREG(a0)

  jr    a3

.global __save_ros_tf_regs
__save_ros_tf_regs:

  REG_S s0,  2*SZREG(a0)
  REG_S s1,  3*SZREG(a0)
  REG_S s2,  4*SZREG(a0)
  REG_S s3,  5*SZREG(a0)
  REG_S s4,  6*SZREG(a0)
  REG_S s5,  7*SZREG(a0)
  REG_S s6,  8*SZREG(a0)
  REG_S s7,  9*SZREG(a0)
  REG_S s8, 10*SZREG(a0)
  REG_S s9, 11*SZREG(a0)
  REG_S s10,12*SZREG(a0)
  REG_S s11,13*SZREG(a0)
  REG_S sp, 14*SZREG(a0)

  REG_S ra,33*SZREG(a0)

  ret
