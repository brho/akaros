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

@@
expression E1;
@@
-allocb(E1
+block_alloc(E1, MEM_WAIT
 )

@@
expression E1;
@@
-iallocb(E1
+block_alloc(E1, MEM_ATOMIC
 )
