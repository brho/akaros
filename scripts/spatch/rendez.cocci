@@
expression RV;
expression RVF;
expression RVFA;
@@
-sleep(RV, RVF, RVFA);
+rendez_sleep(RV, RVF, RVFA);

// i'm assuming this one runs first, matches all the return0s, which really
// just want to delay in place (I think).
@@
expression RV;
expression RVTO;
@@
-tsleep(RV, return0, 0, RVTO);
+udelay_sched(RVTO * 1000);

// and then this one catches all real usage of rendez_sleep_timeout
@@
expression RV;
expression RVF;
expression RVFA;
expression RVTO;
@@
-tsleep(RV, RVF, RVFA, RVTO);
+rendez_sleep_timeout(RV, RVF, RVFA, RVTO);

@@
expression RV;
@@
-wakeup(RV);
+rendez_wakeup(RV);
