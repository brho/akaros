@@
@@
cleanname (...
-,perrbuf)
+)
@@
@@
memmove (...
-,perrbuf)
+)
@@
@@
memset (...
-,perrbuf)
+)
@@
@@
malloc (...
-,perrbuf)
+)
@@
@@
smalloc (...
-,perrbuf)
+)
@@
@@
free (...
-,perrbuf)
+)
@@
@@
strlen (...
-,perrbuf)
+)
@@
@@
strcpy (...
-,perrbuf)
+)
@@
@@
strncpy (...
-,perrbuf)
+)

@@
@@
strchr (...
-,perrbuf)
+)

@@
@@
strcmp (...
-,perrbuf)
+)

@@
expression E;
@@
-lock(E
+spin_lock(&E->lock
-,perrbuf)
+)

@@
expression E;
@@
-unlock(E
+spin_unlock(&E->lock
-,perrbuf)
+)

@@
expression E;
@@
incref(E
-,perrbuf)
+)

@@
expression E;
@@
decref(E
-,perrbuf)
+)

@@
@@
validname(E
-, perrbuf)
+)
@@
@@
validnamedup(E
-, perrbuf)
+)
@@
@@
waserror(E
-, perrbuf)
+)
@@
expression E;
@@
skipslash(E
-, perrbuf)
+)
@@
@@
waserror(
-perrbuf)
+)
@@
@@
kstrcpy(
-perrbuf)
+)
@@
@@
skipslash(
-perrbuf)
+)
@@
@@
getcallerpc(
-perrbuf)
+)
@@
@@
assert(
-perrbuf)
+)
@@
@@
newchan(
-perrbuf)
+)
