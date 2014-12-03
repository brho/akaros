#include <setjmp/setjmp.h>

void ____longjmp_chk(struct __jmp_buf_tag __env[1], int __val)
{
  return __longjmp(__env,__val);
}
