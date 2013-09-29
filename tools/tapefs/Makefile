#CC = ivycc --deputy
CFLAGS += -I.
V = @

TAPEFS_OBJS = fs.o
TAPEFS_HEADS = tapefs.h

all: cpiofs
	$(V)rm -rf *.o

%.o: %.c $(TAPEFS_HEADS)
	@echo + cc [TAPEFS] $<
	$(V)$(CC) -c $(CFLAGS) -o $@ $<

CPIOFS_OBJS = $(TAPEFS_OBJS) cpiofs.o
cpiofs: $(CPIOFS_OBJS) 
	@echo + cc [CPIOFS] $@
	$(V)$(CC) $(CFLAGS) -o $@ $(CPIOFS_OBJS)

clean:
	rm -rf *.o
	rm -rf syscall_server_*
	rm -rf .syscall_server_*

