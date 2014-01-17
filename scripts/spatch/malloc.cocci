@ rulesm @
identifier t;
identifier f;
expression E1;
type T;
@@
T f(...){<...
t = smalloc(E1);
...>}
@@
identifier rulesm.f;
expression E1;
@@

- smalloc(E1
+ kzmalloc(E1, KERN_WAIT
   )

@ rulem @
identifier t;
identifier f;
expression E1;
type T;
@@
T f(...){<...
t = malloc(E1);
...>}
@@
identifier rulem.f;
expression E1;
@@

- malloc(E1
+ kzmalloc(E1, KERN_WAIT
   )

@@
@@
-getcallerpc(...);

@@
@@
-setmalloctag(...);

@@
@@
-free(
+kfree(
...);

@@
@@
-mallocz(
+kzmalloc(
...);
