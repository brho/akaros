#include <stdlib.h>
#include <stdio.h>
#include <parlib.h>
#include <channel.h>

int main(int argc, char** argv)
{
	channel_t ch;
	channel_attr_t ch_attr;
	
	//Wait for a channel to be created with me
	channel_create_wait(&ch, &ch_attr);
	
	//Receive a message over that channel
	#define MSG_LENGTH    25
	uint8_t* buf[MSG_LENGTH];
	channel_msg_t msg;
	msg.buf = buf;
	channel_recvmsg(&ch, &msg);
	
	//Print the message
	printf("%s", msg.buf);	
	
	return 0;
}
