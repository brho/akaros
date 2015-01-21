@@
typedef ECORE_MUTEX_SPIN;
typedef spinlock_t;
@@
-ECORE_MUTEX_SPIN
+spinlock_t

@@
typedef ECORE_MUTEX;
typedef qlock_t;
@@
-ECORE_MUTEX
+qlock_t

@@
expression E1;
expression E2;
expression E3;
expression E4;
@@
-mtx_init(E1, E2, E3, E4)
+qlock_init(E1)
