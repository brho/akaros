@@
expression E;
@@
wlock(
-E
+&E->rwlock
 )
@@
expression E;
@@
wunlock(
-E
+&E->rwlock
 )

@@
expression E;
@@
rlock(
-E
+&E->rwlock
 )
@@
expression E;
@@
canrlock(
-E
+&E->rwlock
 )
@@
expression E;
@@
runlock(
-E
+&E->rwlock
 )
@@

expression E;
@@
qlock(
-E
+&E->qlock
 )
@@
expression E;
@@
qunlock(
-E
+&E->qlock
 )
