@  @
type kref;
identifier f;
expression E;
@@
-f->ref = E
+kref_init(&f->ref, fake_release, E)

@  @
expression E;
@@
-incref(E)
+kref_get(&E->ref, 1)

@  @
expression E;
@@
-decref(E)
+kref_put(&E->ref)
