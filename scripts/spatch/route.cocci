@@
identifier route;
@@
route
- ->ifcid
+->routeTree.ifcid

@@
identifier route;
@@
route
- ->tag
+->routeTree.tag

@@
identifier route;
@@
route
- ->ipifc
+->routeTree.ipifc

@@
identifier route;
@@
route
- ->type
+->routeTree.type

@@
identifier route;
@@
route
- ->ifc
+->routeTree.ifc

@@
identifier route;
@@
route
- ->left
+->routeTree.left

@@
identifier route;
@@
route
- ->right
+->routeTree.right

@@
identifier route;
@@
route
- ->mid
+->routeTree.mid

@@
identifier route;
@@
route
- ->depth
+->routeTree.depth

@@
identifier route;
@@
route
- ->ref
+->routeTree.ref

@@
typedef RouteTree;
@@
-RouteTree
+struct routeTree

@@
typedef V4route;
@@
-V4route
+struct V4route

@@
typedef V6route;
@@
-V6route
+struct V6route



