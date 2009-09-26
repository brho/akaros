
all: syscall_server

translate_state.o: translate_state.c syscall_server.h
	gcc -c -I. -o translate_state.o translate_state.c 

syscall_server: translate_state.o syscall_server.h syscall_server.c
	gcc -I. -o syscall_server translate_state.o syscall_server.c 

clean:
	rm -rf *.o
	rm -rf syscall_server
