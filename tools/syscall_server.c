#include "syscall_server.h"
#include <sys/ioctl.h>
#include <termios.h>

/* Remote Syscall Server app. 
   Written by Paul Pearce.
   This is an UGLY HACK to avoid sockets to work with the 
   ugly hack that is the current tesselaton udp wrapping.

   If you search for the comment UDP you can see how to revert this back
   to socket niceness.

   Note: The server.h has a shared structure with the newlib_backend.h. These must be kept in sync
         The only difference is Paul's GCC won't allow unions without names, so the 'subheader' name was added
*/

// UDP BRANCH
#include <netdb.h>
char udp_arr[4096];
int udp_arr_size = 0;
int udp_cur_pos = 0;
// END UDP BRANCH

// TODO: A syscall_id_t that says close the link, and deal with that in the main switch statement.
int main()
{
	// Listen for connections.
	int data_port = listen_for_connections();

	// Once here, we are inside of new processes, setup the new port.
	int data_fd = setup_data_connection(data_port);

	// Begin processing data from the connection
	process_connections(data_fd);
}

// Bind to LISTEN_PORT and listen for connections on the specified port.
// If a client requests a connection, spawn off a new process, then continue.
// In new process, return with the port we are going to listen for data on
int listen_for_connections()
{
	//UDP BRANCH. Do nothing, as this is UDP
	return -1;
	// END UDP BRANCH

	// Sockets
	int listen_socket, data_socket;

	// Address structures
	struct sockaddr_in server_addr, client_addr;

	// Self explanatory
	int num_connections, response_msg;

	// Size of our address structure
	unsigned int addr_len = sizeof(struct sockaddr_in);

	// Fork response for determining if child or parent
	int child;

	// Create socket
	if ((listen_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
	{
		printf("Error: Could not create socket.\n");
		exit(1);
	}

	// Setup sockaddr_in
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(LISTEN_PORT);
	server_addr.sin_addr.s_addr = INADDR_ANY;
	bzero(&(server_addr.sin_zero), 8);  	// This is apparently a source of bugs? Who knew.

	// Bind to the given port.
	if (bind(listen_socket, (struct sockaddr *) &server_addr, sizeof(struct sockaddr)))
	{
		printf("Error: Could not bind to port: %i\n", LISTEN_PORT);
		exit(1);
	}

	// Listen on the socket
    if (listen(listen_socket, BACKLOG_LEN) < 0)
    {
    	printf("Error: Could not listen on socket.\n");
    	exit(1);
    }

	printf("Server started.\n");

	// Main loop to listen for connections.
	while(1)
	{
		printf("Waiting for connections on port: %i\n", LISTEN_PORT);

		if ((data_socket = accept(listen_socket, (struct sockaddr *)&client_addr,  &addr_len)) < 0)
		{
			printf("Error: Could not accept a new connection.\n");
		}

		num_connections++;
		response_msg = num_connections + LISTEN_PORT;

		printf("New connection detected. Assigning port: %i\n", response_msg);
		if (send(data_socket, (char*)(&response_msg), sizeof(int), 0) < 0)
		{
			printf("Error: Could not send response. New client may not have received port. Continuing.\n");
			close(data_socket);
			continue;
		}
		close(data_socket);

		// Spawn a new process for the connection and offload further communications to that process.
		if ((child = fork()) == -1)
		{
			printf("Error: Fork failed. Client responses will be ignored. Continuing.\n");
			continue;
		}
		else if (child)
		{
			// Spawned Process, break out of loop for cleanup and initialization of the listen function.
			break;
		}
		else
		{
			// Original Process
			// Nothing to do. Continue loop.
		}
	}

	// Only reach here inside of new process, after spawning. Clean up.
	close(listen_socket);

	return response_msg;
}


// Perform socket setup on the new port
int setup_data_connection(int port)
{
	// UDP BRANCH
	port = 44444;
	// END UDP BRANCH

	// Sockets
	int listen_socket, data_socket;

	// Address structures
	struct sockaddr_in server_addr, client_addr;

	// Size of our address structure
	unsigned int addr_len = sizeof(struct sockaddr_in);

	// Create socket
	// UDP BRANCH
	// if ((listen_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
	if ((listen_socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
	{
		printf("Error: Spawned Process, PORT: %i. Could not create socket.\n", port);
		exit(1);
	}

	// Setup sockaddr_in
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	server_addr.sin_addr.s_addr=INADDR_ANY;
	bzero(&(server_addr.sin_zero), 8);  	// This is apparently a source of bugs? Who knew.

	// Bind to the given port.
	if (bind(listen_socket, (struct sockaddr *) &server_addr, sizeof(struct sockaddr)))
	{
		printf("Error: Spawned Process, PORT: %i. Could not bind to port.\n", port);
		exit(1);
	}

	//UDP BRANCH
	data_socket = listen_socket;

	// UDP BRANCH COMMENTED
	/*

	// Listen on the socket
    if (listen(listen_socket, BACKLOG_LEN) < 0)
    {
    	printf("Error: Spawned Process, PORT: %i. Could not listen on socket.\n", port);
    	exit(1);
    }

	if ((data_socket = accept(listen_socket, (struct sockaddr *)&client_addr,  &addr_len)) < 0)
	{
		printf("Error:  Spawned Process, PORT: %i. Could not accept a new connection.\n", port);
	}

	*/ // UDP BRANCH END COMMENT

	printf("Spawned Process, PORT: %i. FD: %i. Established.\n", port, data_socket);

	// END SOCKET SETUP

	return data_socket;
}

// Poll for a message, return a pointer to message. Caller is required to manage memory of the buffer
// Called by the server to get data
void process_connections(int fd) {

	// Variables for the fixed filds.
	syscall_id_t id;

	int just_read;
	
	msg_t temp_msg;

	// TODO: Better msg cleanup on close. Channel cleanup in general
	while(1)
	{
		// Read data from the socket.
		// Peek == 1, thus if no data is avaiable, return. Else, wait until full syscall_id_t is avaiable
		just_read = read_header_from_socket(&temp_msg, fd);

		if (just_read == 0)
		{
			//sleep(1);
			continue;
		}
		
		id = temp_msg.id;

		// If we couldnt read data, or the ID we got is bad, terminate process.
		if ((just_read == -1) || (id < 0) || (id > NUM_CALLS))
		{
			printf("error on id: %d\n", id);
			send_error(fd);

			return;
		}

		msg_t * msg = NULL;
		response_t * return_msg = NULL;
		int return_len = -1;

		switch (id) {
			case OPEN_ID:
				msg = malloc(sizeof(msg_t) + temp_msg.subheader.open.len);
				if (msg == NULL) {
					send_error(fd);
					return;
				}
				
				*msg = temp_msg;
				just_read = read_buffer_from_socket(msg->subheader.open.buf, fd, msg->subheader.open.len);
				
				if (just_read != msg->subheader.open.len) {
					free(msg);
					send_error(fd);
					return;
				}

				return_msg = handle_open(msg);
				free(msg);
				return_len = sizeof(response_t);
				break;

			case CLOSE_ID:
				return_msg = handle_close(&temp_msg);
				return_len = sizeof(response_t);
				break;

			case READ_ID:
				return_msg = handle_read(&temp_msg);
				if (return_msg != NULL)
					return_len = sizeof(response_t) + ((return_msg->ret >= 0)  ? return_msg->ret : 0);
				break;

			case WRITE_ID:

				msg = malloc(sizeof(msg_t) + temp_msg.subheader.write.len);
				if (msg == NULL) {
					send_error(fd);
					return;
				}
				
				*msg = temp_msg;
				just_read = read_buffer_from_socket(msg->subheader.write.buf, fd, msg->subheader.write.len);

				if (just_read != msg->subheader.write.len) {
					free(msg);
					send_error(fd);
					return;
				}

				return_msg = handle_write(msg);
				free(msg);

				return_len = sizeof(response_t);
				break;

			case LSEEK_ID:

				return_msg = handle_lseek(&temp_msg);
				return_len = sizeof(response_t);
				break;

			case ISATTY_ID:
				return_msg = handle_isatty(&temp_msg);
				return_len = sizeof(response_t);
				break;

			case UNLINK_ID:
				msg = malloc(sizeof(msg_t) + temp_msg.subheader.unlink.len);
				if (msg == NULL) {
					send_error(fd);
					return;
				}
				
				*msg = temp_msg;
				just_read = read_buffer_from_socket(msg->subheader.unlink.buf, fd, msg->subheader.unlink.len);
				
				if (just_read != msg->subheader.unlink.len) {
					free(msg);
					send_error(fd);
					return;
				}
			
				return_msg = handle_unlink(msg);
				free(msg);
				return_len = sizeof(response_t);
				break;

			case LINK_ID:
			
				msg = malloc(sizeof(msg_t) + temp_msg.subheader.link.old_len + temp_msg.subheader.link.new_len);
				if (msg == NULL) {
					send_error(fd);
					return;
				}
				
				*msg = temp_msg;
				just_read = read_buffer_from_socket(msg->subheader.link.buf, fd, temp_msg.subheader.link.old_len + temp_msg.subheader.link.new_len);
				
				if (just_read != (temp_msg.subheader.link.old_len + temp_msg.subheader.link.new_len)) {
					free(msg);
					send_error(fd);
					return;
				}
			
				return_msg = handle_link(msg);
				free(msg);
				return_len = sizeof(response_t);
				break;

			case FSTAT_ID:
				return_msg = handle_fstat(&temp_msg);
				return_len = sizeof(response_t);

				break;

			case STAT_ID:
				msg = malloc(sizeof(msg_t) + temp_msg.subheader.stat.len);
				if (msg == NULL) {
					send_error(fd);
					return;
				}
			
				*msg = temp_msg;
				just_read = read_buffer_from_socket(msg->subheader.stat.buf, fd, msg->subheader.stat.len);
			
				if (just_read != msg->subheader.stat.len) {
					free(msg);
					send_error(fd);
					return;
				}

				return_msg = handle_stat(msg);
				free(msg);
				return_len = sizeof(response_t);

				break;
			default:
				send_error(fd);
				return;
		}

		if (return_msg == NULL)
		{
			send_error(fd);
			return;
		}

		// UDP BRANCH. 
		struct hostent *hp;
		hp = gethostbyname("192.168.0.10");
		struct sockaddr_in addr;
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		memcpy(&addr.sin_addr, hp->h_addr, hp->h_length);
		addr.sin_port = htons(44443);
		// END UDP BRANCH

		if ( sendto(fd, return_msg, return_len, 0, (struct sockaddr *)&addr, sizeof(addr)) != return_len)
		//if (write(fd, return_msg, return_len) != return_len)
		{
			free(return_msg);
			// If we cant write to the socket, cant send CONNECTION_TERMINATED. Just die.
			printf("Error: Spawned Process, FD: %i. Could not send data out. Dying.\n", fd);
			return;
		}

		free(return_msg);
	}
}

int read_header_from_socket(msg_t* msg, int socket) {
	return read_from_socket((char*)msg, socket, sizeof(msg_t), PEEK);
}

int read_buffer_from_socket(char* buf, int socket, int len) {
	return read_from_socket(buf, socket, len, NO_PEEK);
}

// UDP HACK VERSION
// Read len bytes from the given socket to the buffer.
// If peek is NO_PEEK, will wait indefinitely until that much data is read.
// If peek is PEEK, if no data is available, will return immediately.
//		However once some data is available, it will block until the entire amount is available.
// Return values are:
// -1 if error
// 0 if peek and nothing avaiable
// else len
int read_from_socket(char* buf, int socket, int len, int peek) {

	// This function is now super hacked to deal with UDP uglyness. Do not use this to revert once we get tcpip online!

	int total_read = 0;

	if (udp_arr_size == 0) {
		udp_arr_size = recvfrom(socket, udp_arr, 4096, 0, 0, 0);

		if (udp_arr_size == 0)
			return -1;
	}

	if ((udp_cur_pos + len) > udp_arr_size)
		return -1;

	memcpy(buf, udp_arr + udp_cur_pos, len);

	udp_cur_pos = udp_cur_pos + len;

	if (udp_cur_pos == udp_arr_size) {
		udp_cur_pos = 0;
		udp_arr_size = 0;
	}

	return len;

	// UDP BRANCH
	//int just_read = read(socket, buf, len);
	int just_read = recvfrom(socket, buf, len, 0, 0, 0);

	if (just_read < 0) return just_read;
	if (just_read == 0 && peek) return just_read;

	total_read += just_read;

	while (total_read != len) {
		// UDP BRANCH
		//just_read = read(socket, buf + total_read, len - total_read);
		just_read = recvfrom(socket, buf + total_read, len - total_read, 0, 0, 0);

		if (just_read == -1) return -1;

		total_read += just_read;
	}

	return total_read;
}

// Non hacky UDP version
/*
int read_from_socket(char* buf, int socket, int len, int peek) {
        int total_read = 0;
        //printf("\t\treading %i bytes on socket %i with peek %i\n", len, socket, peek);
        int just_read = read(socket, buf, len);
        //printf("\t\tread    %i bytes on socket %i with peek %i\n", just_read, socket, peek);
                                        
        if (just_read < 0) return just_read;
        if (just_read == 0 && peek) return just_read;
                                
        total_read += just_read;
                                
        while (total_read != len) {
                just_read = read(socket, buf + total_read, len - total_read);
                if (just_read == -1) return -1;
                total_read += just_read;
        }                       
                
        return total_read;
}*/


// Send CONNECTION_TERMINATED over the FD (if possible)
// Not fully functional.
void send_error(int fd)
{
	printf("Error: Spawned Process, FD: %i. Could not read from from socket. Sending CONNECTION_TERMINATED.... ", fd);

	int error_msg = CONNECTION_TERMINATED;

	/*if (write(fd, (char*)(&error_msg) , sizeof(int)) != sizeof(int))
	{
		printf("Could not send CONNECTION_TERMINATED.\n");
	}
	else
	{
		printf("Sent.\n");

	}*/

	close(fd);
}


response_t* handle_open(msg_t *msg)
{
	printf("opening: %s\n", msg->subheader.open.buf);
	
	response_t * response = malloc(sizeof(response_t));
	if (response == NULL) return NULL;


	response->ret = open(msg->subheader.open.buf, msg->subheader.open.flags, msg->subheader.open.mode);
	if (response->ret == -1) {
		response->err = errno;
	}

	return response;
}

response_t* handle_close(msg_t *msg)
{		
	response_t * response = malloc(sizeof(response_t));
	if (response == NULL) return NULL;


	response->ret = close(msg->subheader.close.fd);
	if (response->ret == -1) {
		response->err = errno;
	}

	return response;
}

response_t* handle_read(msg_t * msg) 
{	
	response_t * response = malloc(sizeof(response_t) + msg->subheader.read.len);
	if (response == NULL) return NULL;

	response->ret = read(msg->subheader.read.fd, response->buf, msg->subheader.read.len);
	if (response->ret == -1) {
		response->err = errno;
	}

	return response;
}

response_t* handle_write(msg_t * msg) 
{
	response_t * response = malloc(sizeof(response_t));
	if (response == NULL) return NULL;

	response->ret = write(msg->subheader.write.fd, msg->subheader.write.buf, msg->subheader.write.len);
	if (response->ret == -1) {
		response->err = errno;
	}

	return response;
}

response_t* handle_lseek(msg_t * msg)
{
	response_t * response = malloc(sizeof(response_t));
	if (response == NULL) return NULL;

	response->ret = lseek(msg->subheader.lseek.fd, msg->subheader.lseek.ptr, msg->subheader.lseek.dir);
	if (response->ret == -1) {
		response->err = errno;
	}
}

response_t* handle_isatty(msg_t * msg) 
{
	response_t * response = malloc(sizeof(response_t));
	if (response == NULL) return NULL;
	
	response->ret = isatty(msg->subheader.isatty.fd);
	if (response->ret == -1) {
		response->err = errno;
	}

	return response;
}

response_t* handle_unlink(msg_t * msg)
{
	response_t * response = malloc(sizeof(response_t));
	if (response == NULL) return NULL;


	response->ret = unlink(msg->subheader.unlink.buf);
	if (response->ret == -1) {
		response->err = errno;
	}

	return response;
}

response_t* handle_link(msg_t * msg) 
{
	response_t * response = malloc(sizeof(response_t));
	if (response == NULL) return NULL;


	response->ret = link(msg->subheader.link.buf, msg->subheader.link.buf + msg->subheader.link.old_len);
	if (response->ret == -1) {
		response->err = errno;
	}

	return response;
}

response_t* handle_stat(msg_t * msg) {
	response_t * response = malloc(sizeof(response_t));
	if (response == NULL) return NULL;


	response->ret = stat(msg->subheader.stat.buf, &(response->st));
	if (response->ret == -1) {
		response->err = errno;
	}

	return response;
}

response_t* handle_fstat(msg_t *msg) {
	
	response_t * response = malloc(sizeof(response_t));
	if (response == NULL) return NULL;

	response->ret = fstat(msg->subheader.fstat.fd, &(response->st));
	
	if (response->ret == -1) {
		response->err = errno;
	}

	return response;
}
