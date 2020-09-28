#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
void error_handling(char *message);

void setSocketBufferSize(int sock, int snd_buf_size, int rcv_buf_size){
	int state;
	int snd_buf=snd_buf_size, rcv_buf=rcv_buf_size;
	
	state=setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (void*)&rcv_buf, sizeof(rcv_buf));
	if(state)
		error_handling("setsockopt() error!");
	
	state=setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (void*)&snd_buf, sizeof(snd_buf));
	if(state)
		error_handling("setsockopt() error!");
}

void getSocketBufferSize(int sock, int snd_buf_size, int rcv_buf_size){
	int state;
	socklen_t len;
	int snd_buf=snd_buf_size, rcv_buf=rcv_buf_size;
	len=sizeof(snd_buf);
	state=getsockopt(sock, SOL_SOCKET, SO_SNDBUF, (void*)&snd_buf, &len);
	if(state)
		error_handling("getsockopt() error!");
	
	len=sizeof(rcv_buf);
	state=getsockopt(sock, SOL_SOCKET, SO_RCVBUF, (void*)&rcv_buf, &len);
	if(state)
		error_handling("getsockopt() error!");
	
	printf("Input buffer size: %d \n", rcv_buf);
	printf("Output buffer size: %d \n", snd_buf);
}


int main(int argc, char *argv[])
{
	int sock=socket(PF_INET, SOCK_STREAM, 0);
	int snd_buf_size = 1;
	int rcv_buf_size = 1;
	setSocketBufferSize(sock, snd_buf_size, rcv_buf_size);
	getSocketBufferSize(sock, snd_buf_size, rcv_buf_size);

	snd_buf_size = 0xFFFFFF;
	rcv_buf_size = 0xFFFFFF;
	setSocketBufferSize(sock, snd_buf_size, rcv_buf_size);
	getSocketBufferSize(sock, snd_buf_size, rcv_buf_size);

	shutdown(sock, SHUT_WR);
	snd_buf_size = 0xFFFFFF;
	rcv_buf_size = 0;
	setSocketBufferSize(sock, snd_buf_size, rcv_buf_size);
	getSocketBufferSize(sock, snd_buf_size, rcv_buf_size);
	return 0;
}

void error_handling(char *message)
{
	fputs(message, stderr);
	fputc('\n', stderr);
	exit(1);
}
/*
root@com:/home/swyoon/tcpip# gcc get_buf.c -o getbuf
root@com:/home/swyoon/tcpip# gcc set_buf.c -o setbuf
root@com:/home/swyoon/tcpip# ./setbuf
Input buffer size: 425984 
Output buffer size: 425984 
*/


