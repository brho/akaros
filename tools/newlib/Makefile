SRC_DIR=$(shell pwd)/../../
INSTALL_DIR=$(SRC_DIR)/user/parlib/newlib
PACKAGE = newlib-1.17.0
PACKAGE_TAR = $(PACKAGE).tar.gz

KEVINS_CFLAGS= "-U__linux__ -U__linux -U__gnu_linux__ \
                -fno-stack-protector -m32"
OPTIONS=--build=i386 --target=i386 --with-newlib --disable-shared --prefix=$(INSTALL_DIR)
KEVINS_LDFLAGS="-nostdlib"

all: build         

build:$(PACKAGE_TAR)
	export KEVINS_CFLAGS=$(KEVINS_CFLAGS); \
	export KEVINS_LDFLAGS=$(KEVINS_LDFLAGS); \
	cd $(PACKAGE)/newlib; \
	cat configure | sed 's/@CFLAGS@,$$CFLAGS,/@CFLAGS@,$$KEVINS_CFLAGS,/' \
    	          | sed 's/@LDFLAGS@,$$LDFLAGS,/@LDFLAGS@,$$KEVINS_LDFLAGS,/' \
        	                   > configure.tmp; \
	mv configure.tmp configure; \
	chmod a+x configure; \
	./configure $(OPTIONS); \
	make; \
	cd doc; \
	make;

$(PACKAGE_TAR):
	wget ftp://sources.redhat.com/pub/newlib/$(PACKAGE_TAR);
	tar -zxvf newlib-1.17.0.tar.gz; \

install:
	cd newlib-1.17.0/newlib; \
	make install; \
	cd $(INSTALL_DIR)/lib; \
	mkdir -p i386; \
	mv lib* i386;

clean:
	rm -rf newlib-1.17.0.tar.gz; \
	rm -rf newlib-1.17.0; \
	 
