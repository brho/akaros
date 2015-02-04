@@
typedef qlock_t;
@@
-struct mutex
+qlock_t

@@
expression E;
@@
-mutex_lock(
+qlock(
 E)

@@
expression E;
@@
-mutex_unlock(
+qunlock(
 E)
