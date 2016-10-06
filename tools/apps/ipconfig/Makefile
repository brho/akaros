include		../../Makefrag

SOURCES:=	main.c ipv6.c

XCC:=		$(CROSS_COMPILE)gcc

LIBS:=		-lndblib -liplib -lbenchutil -lpthread

PHONY:=		all
all:		ipconfig

PHONY+=		ipconfig
ipconfig:	$(SOURCES)
		@echo "  CC      ipconfig"
		$(Q)$(XCC) -O2 -std=gnu99 -o ipconfig $(SOURCES) $(LIBS)

PHONY+=		install
install:	ipconfig
		@echo "  IN      ipconfig"
		$(Q)cp ipconfig $(KFS_ROOT)/bin/ipconfig
		$(Q)chmod 755 $(KFS_ROOT)/bin/ipconfig

PHONY+=		clean
clean:
		@echo "  RM      ipconfig"
		$(Q)rm -f ipconfig

PHONY+=		mrproper
mrproper:	clean

.PHONY:		$(PHONY)
