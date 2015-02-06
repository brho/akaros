@@
expression T;
@@
-msleep(T);
+kthread_usleep(1000 * T);

@@
expression TMIN;
expression TMAX;
@@
-usleep_range(TMIN, TMAX);
+kthread_usleep(TMIN);

// barriers
@@
@@
-barrier();
+cmb();

// akaros RMW, locking atomics provide hw memory barriers.
// (excluding set, init, and read)
@@
@@
-smp_mb__before_atomic();
+cmb();

@@
@@
-smp_mb__after_atomic();
+cmb();

@@
@@
-smp_mb();
+mb();

@@
@@
-smp_rmb();
+rmb();

@@
@@
-smp_wmb();
+wmb();

@@
expression A0;
expression A1;
@@
-min(A0, A1)
+MIN(A0, A1)

@@
expression A0;
expression A1;
@@
-max(A0, A1)
+MAX(A0, A1)

@@
expression LO;
expression HI;
expression V;
@@
-clamp(V, LO, HI)
+CLAMP(V, LO, HI)

@@
expression A0;
expression A1;
type T;
@@
-min_t(T, A0, A1)
+MIN_T(T, A0, A1)

@@
expression A0;
expression A1;
type T;
@@
-max_t(T, A0, A1)
+MAX_T(T, A0, A1)

@@
expression LO;
expression HI;
expression V;
type T;
@@
-clamp_t(T, V, LO, HI)
+CLAMP_T(T, V, LO, HI)


// locking
// being conservative: they might not need irqsave
@@
expression E;
@@
-spin_lock_init(E)
+spinlock_init_irqsave(E)

@@
expression E;
@@
-spin_lock_bh(E)
+spin_lock(E)

@@
expression E;
@@
-spin_unlock_bh(E)
+spin_unlock(E)
@@

expression E;
@@
-spin_lock_irq(E)
+spin_lock_irqsave(E)

@@
expression E;
@@
-spin_unlock_irq(E)
+spin_unlock_irqsave(E)

@@
expression E;
@@
-ilog2(E)
+LOG2_UP(E)
