#include <stdlib.h>
#include <stdio.h>
#include <parlib.h>
#include <channel.h>

#define SERVER_ID     1
#define MSG_LENGTH   25

int main(int argc, char** argv)
{
	printf("Beginning of channel test client.\n");
	channel_t ch;
	channel_attr_t ch_attr;
	
	//Create the channel
	channel_create(SERVER_ID, &ch, &ch_attr);
	
	//Send a message over the channel
	#define MSG_LENGTH    25
	uint8_t* buf[MSG_LENGTH]; //eventually replace with a call to channel_malloc()
	channel_msg_t msg;
	msg.len = MSG_LENGTH;
	msg.buf = buf;
	channel_sendmsg(&ch, &msg);
	
	channel_destroy(&ch);
	
	printf("End of channel test client.\n");
	return 0;
}
