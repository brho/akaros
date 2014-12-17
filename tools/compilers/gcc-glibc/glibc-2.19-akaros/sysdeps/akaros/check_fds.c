#include <fcntl.h>
#include <unistd.h>
#include <assert.h>

void    
__libc_check_standard_fds (void)
{
  #define check_one_fd(fd) assert(fcntl((fd),F_GETFD) != -1)
  check_one_fd(STDIN_FILENO);
  check_one_fd(STDOUT_FILENO);
  check_one_fd(STDERR_FILENO);
}
