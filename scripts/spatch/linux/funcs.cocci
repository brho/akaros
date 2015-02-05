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
