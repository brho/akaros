@@
type T;
identifier f;
@@
T f(...
+, struct errbuf *perrbuf)
-)
{...}

@@
type T;
identifier f;
@@
T f(...)
{
+struct errbuf errbuf[1];
+struct errbuf *errstack;
+errstack = perrbuf; perrbuf = errbuf; 
... 
waserror() ...}

@@
identifier f;
@@
f(...
+,perrbuf)
-)

@@
@@
-poperror(...);
