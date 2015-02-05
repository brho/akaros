// struct ether->netif.whatever -> ether->whatever
@@
identifier E;
identifier A;
@@
-E->netif.A
+E->A

@@
identifier E;
identifier A;
@@
-E.netif.A
+E.A

@@
identifier E;
@@
-&E->netif
+E

@@
@@
struct
-netif
+ether
