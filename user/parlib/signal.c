#include <signal.h>

// Process wide table of signal handlers
__sighandler_t sighandlers[_NSIG-1];

int sigaddset(sigset_t *__set, int __signo)
{
}

int sigdelset(sigset_t *__set, int __signo)
{
}

int sigismember(__const sigset_t *__set, int __signo)
{
}

int sigprocmask(int __how, __const sigset_t *__restrict __set, sigset_t *__restrict __oset)
{
}

int sigsuspend(__const sigset_t *__set)
{
}

int sigaction(int __sig, __const struct sigaction *__restrict __act,
 struct sigaction *__restrict __oact)
{
}

int sigpending(sigset_t *__set)
{
}

int sigwait(__const sigset_t *__restrict __set, int *__restrict __sig)
{
}

int sigwaitinfo(__const sigset_t *__restrict __set, siginfo_t *__restrict __info)
{
}

int sigtimedwait(__const sigset_t *__restrict __set,
    siginfo_t *__restrict __info,
    __const struct timespec *__restrict __timeout)
{
}

int sigqueue(__pid_t __pid, int __sig, __const union sigval __val)
{
}


int sigvec(int __sig, __const struct sigvec *__vec, struct sigvec *__ovec)
{
}


int sigreturn(struct sigcontext *__scp)
{
}


int siginterrupt(int __sig, int __interrupt)
{
}


int sigstack(struct sigstack *__ss, struct sigstack *__oss)
{
}


int sigaltstack(__const struct sigaltstack *__restrict __ss,
   struct sigaltstack *__restrict __oss)
{
}

