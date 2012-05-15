#include <sys/asm.h>

.abicalls

.global __pop_ros_tf_regs
.ent __pop_ros_tf_regs
__pop_ros_tf_regs:

  REG_L s0,20*SZREG(a0)
  REG_L s1,21*SZREG(a0)
  REG_L s2,22*SZREG(a0)
  REG_L s3,23*SZREG(a0)
  REG_L s4,24*SZREG(a0)
  REG_L s5,25*SZREG(a0)
  REG_L s6,26*SZREG(a0)
  REG_L s7,27*SZREG(a0)
  REG_L s8,28*SZREG(a0)
  REG_L s9,29*SZREG(a0)
  REG_L sp,30*SZREG(a0)

  REG_L ra,33*SZREG(a0)

  jr    a3
.end __pop_ros_tf_regs

.global __save_ros_tf_regs
.ent __save_ros_tf_regs
__save_ros_tf_regs:

  REG_S s0,20*SZREG(a0)
  REG_S s1,21*SZREG(a0)
  REG_S s2,22*SZREG(a0)
  REG_S s3,23*SZREG(a0)
  REG_S s4,24*SZREG(a0)
  REG_S s5,25*SZREG(a0)
  REG_S s6,26*SZREG(a0)
  REG_S s7,27*SZREG(a0)
  REG_S s8,28*SZREG(a0)
  REG_S s9,29*SZREG(a0)
  REG_S sp,30*SZREG(a0)

  REG_S ra,33*SZREG(a0)

  ret
.end __save_ros_tf_regs
