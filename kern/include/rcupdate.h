/*
 * Read-Copy Update mechanism for mutual exclusion
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * Copyright IBM Corporation, 2001
 *
 * Author: Dipankar Sarma <dipankar@in.ibm.com>
 *
 * Based on the original work by Paul McKenney <paulmck@us.ibm.com>
 * and inputs from Rusty Russell, Andrea Arcangeli and Andi Kleen.
 * Papers:
 * http://www.rdrop.com/users/paulmck/paper/rclockpdcsproof.pdf
 * http://lse.sourceforge.net/locking/rclock_OLS.2001.05.01c.sc.pdf (OLS2001)
 *
 * For detailed explanation of Read-Copy Update mechanism see -
 *		http://lse.sourceforge.net/locking/rcupdate.html
 *
 */

#ifndef __LINUX_RCUPDATE_H
#define __LINUX_RCUPDATE_H

#define ULONG_CMP_GE(a, b)	(ULONG_MAX / 2 >= (a) - (b))
#define ULONG_CMP_LT(a, b)	(ULONG_MAX / 2 < (a) - (b))
#define ulong2long(a)		(*(long *)(&(a)))

void call_rcu(struct rcu_head *head, rcu_callback_t func);

void synchronize_rcu(void);

/*
 * init_rcu_head_on_stack()/destroy_rcu_head_on_stack() are needed for dynamic
 * initialization and destruction of rcu_head on the stack. rcu_head structures
 * allocated dynamically in the heap or defined statically don't need any
 * initialization.
 */
#ifdef CONFIG_DEBUG_OBJECTS_RCU_HEAD
void init_rcu_head(struct rcu_head *head);
void destroy_rcu_head(struct rcu_head *head);
void init_rcu_head_on_stack(struct rcu_head *head);
void destroy_rcu_head_on_stack(struct rcu_head *head);
#else /* !CONFIG_DEBUG_OBJECTS_RCU_HEAD */
static inline void init_rcu_head(struct rcu_head *head) { }
static inline void destroy_rcu_head(struct rcu_head *head) { }
static inline void init_rcu_head_on_stack(struct rcu_head *head) { }
static inline void destroy_rcu_head_on_stack(struct rcu_head *head) { }
#endif	/* #else !CONFIG_DEBUG_OBJECTS_RCU_HEAD */

#define RCU_LOCKDEP_WARN(c, s) do { } while (0)
#define rcu_sleep_check() do { } while (0)
#define rcu_lock_acquire(a)		do { } while (0)
#define rcu_lock_release(a)		do { } while (0)
#define rcu_dereference_sparse(p, space)

static inline void __rcu_read_lock(void)
{
	cmb();
}

static inline void __rcu_read_unlock(void)
{
	cmb();
}

#define __rcu_access_pointer(p, space) \
({ \
	typeof(*p) *_________p1 = (typeof(*p) *__force)READ_ONCE(p); \
	rcu_dereference_sparse(p, space); \
	((typeof(*p) __force __kernel *)(_________p1)); \
})
#define __rcu_dereference_check(p, c, space) \
({ \
	/* Dependency order vs. p above. */ \
	typeof(*p) *________p1 = (typeof(*p) *__force)lockless_dereference(p); \
	RCU_LOCKDEP_WARN(!(c), "suspicious rcu_dereference_check() usage"); \
	rcu_dereference_sparse(p, space); \
	((typeof(*p) __force __kernel *)(________p1)); \
})
#define __rcu_dereference_protected(p, c, space) \
({ \
	RCU_LOCKDEP_WARN(!(c), "suspicious rcu_dereference_protected() usage"); \
	rcu_dereference_sparse(p, space); \
	((typeof(*p) __force __kernel *)(p)); \
})
#define rcu_dereference_raw(p) \
({ \
	/* Dependency order vs. p above. */ \
	typeof(p) ________p1 = lockless_dereference(p); \
	((typeof(*p) __force __kernel *)(________p1)); \
})

/**
 * RCU_INITIALIZER() - statically initialize an RCU-protected global variable
 * @v: The value to statically initialize with.
 */
#define RCU_INITIALIZER(v) (typeof(*(v)) __force __rcu *)(v)

/**
 * rcu_assign_pointer() - assign to RCU-protected pointer
 * @p: pointer to assign to
 * @v: value to assign (publish)
 *
 * Assigns the specified value to the specified RCU-protected
 * pointer, ensuring that any concurrent RCU readers will see
 * any prior initialization.
 *
 * Inserts memory barriers on architectures that require them
 * (which is most of them), and also prevents the compiler from
 * reordering the code that initializes the structure after the pointer
 * assignment.  More importantly, this call documents which pointers
 * will be dereferenced by RCU read-side code.
 *
 * In some special cases, you may use RCU_INIT_POINTER() instead
 * of rcu_assign_pointer().  RCU_INIT_POINTER() is a bit faster due
 * to the fact that it does not constrain either the CPU or the compiler.
 * That said, using RCU_INIT_POINTER() when you should have used
 * rcu_assign_pointer() is a very bad thing that results in
 * impossible-to-diagnose memory corruption.  So please be careful.
 * See the RCU_INIT_POINTER() comment header for details.
 *
 * Note that rcu_assign_pointer() evaluates each of its arguments only
 * once, appearances notwithstanding.  One of the "extra" evaluations
 * is in typeof() and the other visible only to sparse (__CHECKER__),
 * neither of which actually execute the argument.  As with most cpp
 * macros, this execute-arguments-only-once property is important, so
 * please be careful when making changes to rcu_assign_pointer() and the
 * other macros that it invokes.
 */
#define rcu_assign_pointer(p, v)					      \
({									      \
	uintptr_t _r_a_p__v = (uintptr_t)(v);				      \
									      \
	if (__builtin_constant_p(v) && (_r_a_p__v) == (uintptr_t)NULL)	      \
		WRITE_ONCE((p), (typeof(p))(_r_a_p__v));		      \
	else								      \
		smp_store_release(&p, RCU_INITIALIZER((typeof(p))_r_a_p__v)); \
	_r_a_p__v;							      \
})

/**
 * rcu_access_pointer() - fetch RCU pointer with no dereferencing
 * @p: The pointer to read
 *
 * Return the value of the specified RCU-protected pointer, but omit the
 * smp_read_barrier_depends() and keep the READ_ONCE().  This is useful
 * when the value of this pointer is accessed, but the pointer is not
 * dereferenced, for example, when testing an RCU-protected pointer against
 * NULL.  Although rcu_access_pointer() may also be used in cases where
 * update-side locks prevent the value of the pointer from changing, you
 * should instead use rcu_dereference_protected() for this use case.
 *
 * It is also permissible to use rcu_access_pointer() when read-side
 * access to the pointer was removed at least one grace period ago, as
 * is the case in the context of the RCU callback that is freeing up
 * the data, or after a synchronize_rcu() returns.  This can be useful
 * when tearing down multi-linked structures after a grace period
 * has elapsed.
 */
#define rcu_access_pointer(p) __rcu_access_pointer((p), __rcu)

/**
 * rcu_dereference_check() - rcu_dereference with debug checking
 * @p: The pointer to read, prior to dereferencing
 * @c: The conditions under which the dereference will take place
 *
 * Do an rcu_dereference(), but check that the conditions under which the
 * dereference will take place are correct.  Typically the conditions
 * indicate the various locking conditions that should be held at that
 * point.  The check should return true if the conditions are satisfied.
 * An implicit check for being in an RCU read-side critical section
 * (rcu_read_lock()) is included.
 *
 * For example:
 *
 *	bar = rcu_dereference_check(foo->bar, lockdep_is_held(&foo->lock));
 *
 * could be used to indicate to lockdep that foo->bar may only be dereferenced
 * if either rcu_read_lock() is held, or that the lock required to replace
 * the bar struct at foo->bar is held.
 *
 * Note that the list of conditions may also include indications of when a lock
 * need not be held, for example during initialisation or destruction of the
 * target struct:
 *
 *	bar = rcu_dereference_check(foo->bar, lockdep_is_held(&foo->lock) ||
 *					      atomic_read(&foo->usage) == 0);
 *
 * Inserts memory barriers on architectures that require them
 * (currently only the Alpha), prevents the compiler from refetching
 * (and from merging fetches), and, more importantly, documents exactly
 * which pointers are protected by RCU and checks that the pointer is
 * annotated as __rcu.
 */
#define rcu_dereference_check(p, c) \
	__rcu_dereference_check((p), (c) || rcu_read_lock_held(), __rcu)

/**
 * rcu_dereference_bh_check() - rcu_dereference_bh with debug checking
 * @p: The pointer to read, prior to dereferencing
 * @c: The conditions under which the dereference will take place
 *
 * This is the RCU-bh counterpart to rcu_dereference_check().
 */
#define rcu_dereference_bh_check(p, c) \
	__rcu_dereference_check((p), (c) || rcu_read_lock_bh_held(), __rcu)

/**
 * rcu_dereference_sched_check() - rcu_dereference_sched with debug checking
 * @p: The pointer to read, prior to dereferencing
 * @c: The conditions under which the dereference will take place
 *
 * This is the RCU-sched counterpart to rcu_dereference_check().
 */
#define rcu_dereference_sched_check(p, c) \
	__rcu_dereference_check((p), (c) || rcu_read_lock_sched_held(), \
				__rcu)

/*
 * The tracing infrastructure traces RCU (we want that), but unfortunately
 * some of the RCU checks causes tracing to lock up the system.
 *
 * The no-tracing version of rcu_dereference_raw() must not call
 * rcu_read_lock_held().
 */
#define rcu_dereference_raw_notrace(p) __rcu_dereference_check((p), 1, __rcu)

/**
 * rcu_dereference_protected() - fetch RCU pointer when updates prevented
 * @p: The pointer to read, prior to dereferencing
 * @c: The conditions under which the dereference will take place
 *
 * Return the value of the specified RCU-protected pointer, but omit
 * both the smp_read_barrier_depends() and the READ_ONCE().  This
 * is useful in cases where update-side locks prevent the value of the
 * pointer from changing.  Please note that this primitive does -not-
 * prevent the compiler from repeating this reference or combining it
 * with other references, so it should not be used without protection
 * of appropriate locks.
 *
 * This function is only for update-side use.  Using this function
 * when protected only by rcu_read_lock() will result in infrequent
 * but very ugly failures.
 */
#define rcu_dereference_protected(p, c) \
	__rcu_dereference_protected((p), (c), __rcu)


/**
 * rcu_dereference() - fetch RCU-protected pointer for dereferencing
 * @p: The pointer to read, prior to dereferencing
 *
 * This is a simple wrapper around rcu_dereference_check().
 */
#define rcu_dereference(p) rcu_dereference_check(p, 0)

/**
 * rcu_dereference_bh() - fetch an RCU-bh-protected pointer for dereferencing
 * @p: The pointer to read, prior to dereferencing
 *
 * Makes rcu_dereference_check() do the dirty work.
 */
#define rcu_dereference_bh(p) rcu_dereference_bh_check(p, 0)

/**
 * rcu_dereference_sched() - fetch RCU-sched-protected pointer for dereferencing
 * @p: The pointer to read, prior to dereferencing
 *
 * Makes rcu_dereference_check() do the dirty work.
 */
#define rcu_dereference_sched(p) rcu_dereference_sched_check(p, 0)

/**
 * rcu_pointer_handoff() - Hand off a pointer from RCU to other mechanism
 * @p: The pointer to hand off
 *
 * This is simply an identity function, but it documents where a pointer
 * is handed off from RCU to some other synchronization mechanism, for
 * example, reference counting or locking.  In C11, it would map to
 * kill_dependency().  It could be used as follows:
 *
 *	rcu_read_lock();
 *	p = rcu_dereference(gp);
 *	long_lived = is_long_lived(p);
 *	if (long_lived) {
 *		if (!atomic_inc_not_zero(p->refcnt))
 *			long_lived = false;
 *		else
 *			p = rcu_pointer_handoff(p);
 *	}
 *	rcu_read_unlock();
 */
#define rcu_pointer_handoff(p) (p)

/**
 * rcu_read_lock() - mark the beginning of an RCU read-side critical section
 *
 * When synchronize_rcu() is invoked on one CPU while other CPUs
 * are within RCU read-side critical sections, then the
 * synchronize_rcu() is guaranteed to block until after all the other
 * CPUs exit their critical sections.  Similarly, if call_rcu() is invoked
 * on one CPU while other CPUs are within RCU read-side critical
 * sections, invocation of the corresponding RCU callback is deferred
 * until after the all the other CPUs exit their critical sections.
 *
 * Note, however, that RCU callbacks are permitted to run concurrently
 * with new RCU read-side critical sections.  One way that this can happen
 * is via the following sequence of events: (1) CPU 0 enters an RCU
 * read-side critical section, (2) CPU 1 invokes call_rcu() to register
 * an RCU callback, (3) CPU 0 exits the RCU read-side critical section,
 * (4) CPU 2 enters a RCU read-side critical section, (5) the RCU
 * callback is invoked.  This is legal, because the RCU read-side critical
 * section that was running concurrently with the call_rcu() (and which
 * therefore might be referencing something that the corresponding RCU
 * callback would free up) has completed before the corresponding
 * RCU callback is invoked.
 *
 * RCU read-side critical sections may be nested.  Any deferred actions
 * will be deferred until the outermost RCU read-side critical section
 * completes.
 *
 * You can avoid reading and understanding the next paragraph by
 * following this rule: don't put anything in an rcu_read_lock() RCU
 * read-side critical section that would block in a !PREEMPT kernel.
 * But if you want the full story, read on!
 *
 * In non-preemptible RCU implementations (TREE_RCU and TINY_RCU),
 * it is illegal to block while in an RCU read-side critical section.
 * In preemptible RCU implementations (PREEMPT_RCU) in CONFIG_PREEMPT
 * kernel builds, RCU read-side critical sections may be preempted,
 * but explicit blocking is illegal.  Finally, in preemptible RCU
 * implementations in real-time (with -rt patchset) kernel builds, RCU
 * read-side critical sections may be preempted and they may also block, but
 * only when acquiring spinlocks that are subject to priority inheritance.
 */
static inline void rcu_read_lock(void)
{
	__rcu_read_lock();
	__acquire(RCU);
	rcu_lock_acquire(&rcu_lock_map);
	RCU_LOCKDEP_WARN(!rcu_is_watching(),
			 "rcu_read_lock() used illegally while idle");
}

/*
 * So where is rcu_write_lock()?  It does not exist, as there is no
 * way for writers to lock out RCU readers.  This is a feature, not
 * a bug -- this property is what provides RCU's performance benefits.
 * Of course, writers must coordinate with each other.  The normal
 * spinlock primitives work well for this, but any other technique may be
 * used as well.  RCU does not care how the writers keep out of each
 * others' way, as long as they do so.
 */

/**
 * rcu_read_unlock() - marks the end of an RCU read-side critical section.
 *
 * In most situations, rcu_read_unlock() is immune from deadlock.
 * However, in kernels built with CONFIG_RCU_BOOST, rcu_read_unlock()
 * is responsible for deboosting, which it does via rt_mutex_unlock().
 * Unfortunately, this function acquires the scheduler's runqueue and
 * priority-inheritance spinlocks.  This means that deadlock could result
 * if the caller of rcu_read_unlock() already holds one of these locks or
 * any lock that is ever acquired while holding them; or any lock which
 * can be taken from interrupt context because rcu_boost()->rt_mutex_lock()
 * does not disable irqs while taking ->wait_lock.
 *
 * That said, RCU readers are never priority boosted unless they were
 * preempted.  Therefore, one way to avoid deadlock is to make sure
 * that preemption never happens within any RCU read-side critical
 * section whose outermost rcu_read_unlock() is called with one of
 * rt_mutex_unlock()'s locks held.  Such preemption can be avoided in
 * a number of ways, for example, by invoking preempt_disable() before
 * critical section's outermost rcu_read_lock().
 *
 * Given that the set of locks acquired by rt_mutex_unlock() might change
 * at any time, a somewhat more future-proofed approach is to make sure
 * that that preemption never happens within any RCU read-side critical
 * section whose outermost rcu_read_unlock() is called with irqs disabled.
 * This approach relies on the fact that rt_mutex_unlock() currently only
 * acquires irq-disabled locks.
 *
 * The second of these two approaches is best in most situations,
 * however, the first approach can also be useful, at least to those
 * developers willing to keep abreast of the set of locks acquired by
 * rt_mutex_unlock().
 *
 * See rcu_read_lock() for more information.
 */
static inline void rcu_read_unlock(void)
{
	RCU_LOCKDEP_WARN(!rcu_is_watching(),
			 "rcu_read_unlock() used illegally while idle");
	__release(RCU);
	__rcu_read_unlock();
	rcu_lock_release(&rcu_lock_map); /* Keep acq info for rls diags. */
}

/**
 * RCU_INIT_POINTER() - initialize an RCU protected pointer
 *
 * Initialize an RCU-protected pointer in special cases where readers
 * do not need ordering constraints on the CPU or the compiler.  These
 * special cases are:
 *
 * 1.	This use of RCU_INIT_POINTER() is NULLing out the pointer -or-
 * 2.	The caller has taken whatever steps are required to prevent
 *	RCU readers from concurrently accessing this pointer -or-
 * 3.	The referenced data structure has already been exposed to
 *	readers either at compile time or via rcu_assign_pointer() -and-
 *	a.	You have not made -any- reader-visible changes to
 *		this structure since then -or-
 *	b.	It is OK for readers accessing this structure from its
 *		new location to see the old state of the structure.  (For
 *		example, the changes were to statistical counters or to
 *		other state where exact synchronization is not required.)
 *
 * Failure to follow these rules governing use of RCU_INIT_POINTER() will
 * result in impossible-to-diagnose memory corruption.  As in the structures
 * will look OK in crash dumps, but any concurrent RCU readers might
 * see pre-initialized values of the referenced data structure.  So
 * please be very careful how you use RCU_INIT_POINTER()!!!
 *
 * If you are creating an RCU-protected linked structure that is accessed
 * by a single external-to-structure RCU-protected pointer, then you may
 * use RCU_INIT_POINTER() to initialize the internal RCU-protected
 * pointers, but you must use rcu_assign_pointer() to initialize the
 * external-to-structure pointer -after- you have completely initialized
 * the reader-accessible portions of the linked structure.
 *
 * Note that unlike rcu_assign_pointer(), RCU_INIT_POINTER() provides no
 * ordering guarantees for either the CPU or the compiler.
 */
#define RCU_INIT_POINTER(p, v) \
	do { \
		rcu_dereference_sparse(p, __rcu); \
		WRITE_ONCE(p, RCU_INITIALIZER(v)); \
	} while (0)

/**
 * RCU_POINTER_INITIALIZER() - statically initialize an RCU protected pointer
 *
 * GCC-style initialization for an RCU-protected pointer in a structure field.
 */
#define RCU_POINTER_INITIALIZER(p, v) \
		.p = RCU_INITIALIZER(v)

/*
 * Does the specified offset indicate that the corresponding rcu_head
 * structure can be handled by kfree_rcu()?
 */
#define __is_kfree_rcu_offset(offset) ((offset) < 4096)

/*
 * Helper macro for kfree_rcu() to prevent argument-expansion eyestrain.
 */
#define __kfree_rcu(head, offset) \
	do { \
		static_assert(__is_kfree_rcu_offset(offset)); \
		kfree_call_rcu(head, (rcu_callback_t)(unsigned long)(offset)); \
	} while (0)

/**
 * kfree_rcu() - kfree an object after a grace period.
 * @ptr:	pointer to kfree
 * @rcu_head:	the name of the struct rcu_head within the type of @ptr.
 *
 * Many rcu callbacks functions just call kfree() on the base structure.
 * These functions are trivial, but their size adds up, and furthermore
 * when they are used in a kernel module, that module must invoke the
 * high-latency rcu_barrier() function at module-unload time.
 *
 * The kfree_rcu() function handles this issue.  Rather than encoding a
 * function address in the embedded rcu_head structure, kfree_rcu() instead
 * encodes the offset of the rcu_head structure within the base structure.
 * Because the functions are not allowed in the low-order 4096 bytes of
 * kernel virtual memory, offsets up to 4095 bytes can be accommodated.
 * If the offset is larger than 4095 bytes, a compile-time error will
 * be generated in __kfree_rcu().  If this error is triggered, you can
 * either fall back to use of call_rcu() or rearrange the structure to
 * position the rcu_head structure into the first 4096 bytes.
 *
 * Note that the allowable offset might decrease in the future, for example,
 * to allow something like kmem_cache_free_rcu().
 *
 * The BUILD_BUG_ON check must not involve any function calls, hence the
 * checks are done in macros here.
 */
#define kfree_rcu(ptr, rcu_head)					\
	__kfree_rcu(&((ptr)->rcu_head), offsetof(typeof(*(ptr)), rcu_head))


/*
 * Place this after a lock-acquisition primitive to guarantee that
 * an UNLOCK+LOCK pair acts as a full barrier.  This guarantee applies
 * if the UNLOCK and LOCK are executed by the same CPU or if the
 * UNLOCK and LOCK operate on the same lock variable.
 */
#ifdef CONFIG_ARCH_WEAK_RELEASE_ACQUIRE
#define smp_mb__after_unlock_lock()	smp_mb()  /* Full ordering for lock. */
#else /* #ifdef CONFIG_ARCH_WEAK_RELEASE_ACQUIRE */
#define smp_mb__after_unlock_lock()	do { } while (0)
#endif /* #else #ifdef CONFIG_ARCH_WEAK_RELEASE_ACQUIRE */


#endif /* __LINUX_RCUPDATE_H */
