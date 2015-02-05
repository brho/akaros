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
