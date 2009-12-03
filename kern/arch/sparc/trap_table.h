#ifndef ROS_ARCH_TRAP_TABLE_H
#define ROS_ARCH_TRAP_TABLE_H

#define TRAP_TABLE_ENTRY(label) sethi %hi(handle_trap),%l6; sethi %hi(label),%l5; jmp %lo(handle_trap)+%l6; or %lo(label),%l5,%l5
#define JMP(target) sethi %hi(target),%l4; jmp %lo(target)+%l4; nop; nop

#define ENTER_ERROR_MODE unimp; unimp; unimp; unimp
#define UNHANDLED_TRAP TRAP_TABLE_ENTRY(unhandled_trap)


#endif
