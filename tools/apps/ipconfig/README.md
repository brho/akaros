# ipconfig for Akaros

This is Plan 9 ipconfig(8), ported to Akaros.

ipconfig is an IP stack configuration tool.  It acts as
both a frontend to the filesystem-based configuration dance
one does when configuring a Plan 9-derived IP stack, as well
as a implementing a DHCP client and IPv6 autoconfiguration
agent.  It will be called from `/ifconfig` if present in the
KFS.

Changes to Plan 9 `ipconfig` include reformatting the code
to match Akaros kernel style, replacing `alarm` with Akaros
system calls, removing Plan 9- and GNU-specific code and
replacing with more portable idioms and general cleaning up.

Man page: http://plan9.bell-labs.com/magic/man2html/8/ipconfig
