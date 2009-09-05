#ifdef __IVY__
#include <assert.h>

int __ivy_checking_on = 1;

void __sharc_lock_error(const void *lck, const void *what,
                        unsigned int sz, char *msg)
{
	int old;
	if (!__ivy_checking_on) return;
	old = __ivy_checking_on;
	__ivy_checking_on = 0;
	warn("Ivy: The lock %p was not held for (%p,%d): %s\n",
          lck, what, sz, msg);
	__ivy_checking_on = old;
}

void __sharc_lock_coerce_error(void *dstlck, void *srclck, char *msg)
{
	int old;
	if (!__ivy_checking_on) return;
	old = __ivy_checking_on;
	__ivy_checking_on = 0;
	warn("Ivy: The locks in the coercion at %s must be the same\n", msg);
	__ivy_checking_on = old;
}

#endif // __IVY__
