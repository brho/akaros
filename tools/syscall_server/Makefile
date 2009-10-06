#CC = ivycc --deputy
CFLAGS += -I.
V = @

SYSCALL_SERVER_OBJS = newlib_trans.o syscall_server.o
SYSCALL_SERVER_HEADS = syscall_server.h newlib_trans.h

all: syscall_server_udp syscall_server_pty syscall_server_pipe
	$(V)rm -rf *.o

.syscall_server_pipe.in: 
	$(V)mkfifo $@
	$(V)chmod ug+rw $@

.syscall_server_pipe.out: 
	$(V)mkfifo $@
	$(V)chmod ug+rw $@

syscall_server_pipes: .syscall_server_pipe.in .syscall_server_pipe.out 

%.o: %.c $(SYSCALL_SERVER_HEADS)
	@echo + cc [SYSCALL_SERVER] $<
	$(V)$(CC) -c $(CFLAGS) -o $@ $<

SYSCALL_SERVER_PTY_OBJS = $(SYSCALL_SERVER_OBJS) pty.o
syscall_server_pty: $(SYSCALL_SERVER_PTY_OBJS)
	@echo + cc [SYSCALL_SERVER] $@
	$(V)$(CC) $(CFLAGS) -o $@ $(SYSCALL_SERVER_PTY_OBJS)
        
SYSCALL_SERVER_PIPE_OBJS = $(SYSCALL_SERVER_OBJS) pipe.o
syscall_server_pipe: $(SYSCALL_SERVER_PIPE_OBJS) syscall_server_pipes
	@echo + cc [SYSCALL_SERVER] $@
	$(V)$(CC) $(CFLAGS) -o $@ $(SYSCALL_SERVER_PIPE_OBJS)

SYSCALL_SERVER_UDP_OBJS = $(SYSCALL_SERVER_OBJS) udp.o
syscall_server_udp: $(SYSCALL_SERVER_UDP_OBJS)
	@echo + cc [SYSCALL_SERVER] $@
	$(V)$(CC) $(CFLAGS) -o $@ $(SYSCALL_SERVER_UDP_OBJS)

clean:
	rm -rf *.o
	rm -rf syscall_server_*
	rm -rf .syscall_server_*

