#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <vector>
#include <string>
#include <iostream>

#define BUF_SIZE 100
#define EPOLL_SIZE 50

using namespace std;

void error_handling(string buf)
{
	cout<< buf<<"\n";
	exit(1);
}


int main(int argc, char *argv[])
{
	int serv_sock, clnt_sock;
	struct sockaddr_in serv_adr, clnt_adr, media_adr;
	socklen_t adr_sz;
	int str_len, i;
	char buf[BUF_SIZE];

	struct epoll_event *ep_events;
	struct epoll_event event;
	int epfd, event_cnt;
    int optval = 1;

    //ip port 입력
	if(argc!=2) {
		printf("Usage : %s <port>\n", argv[0]);
		exit(1);
	}

    //소켓 생성, ip, port, option 설정
	serv_sock=socket(PF_INET, SOCK_STREAM, 0);
	memset(&serv_adr, 0, sizeof(serv_adr));
	serv_adr.sin_family=AF_INET;
	serv_adr.sin_addr.s_addr=htonl(INADDR_ANY);
	serv_adr.sin_port=htons(atoi(argv[1]));
    setsockopt(serv_sock, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));

    //bind()
	if(bind(serv_sock, (struct sockaddr*) &serv_adr, sizeof(serv_adr))==-1)
		error_handling("bind() error");

	//listen()
    if(listen(serv_sock, 5)==-1)
		error_handling("listen() error");

    //epoll 객체 생성
	epfd=epoll_create(EPOLL_SIZE);
	ep_events= new epoll_event[EPOLL_SIZE];

    //클라이언트 요청 이벤트 등록
	event.events=EPOLLIN;
	event.data.fd=serv_sock;	
	epoll_ctl(epfd, EPOLL_CTL_ADD, serv_sock, &event);


    //퍼블리셔 접속시 미디어 서버 두 개씩 연결
    int mediaServerPort[4] = {8000, 8001, 8002, 8003};
    vector<int> readClient;
    int mediaClient[4] = {0,0,0,0};
    int pubCnt = 0;
	while(1)
	{
		event_cnt=epoll_wait(epfd, ep_events, EPOLL_SIZE, -1);
		if(event_cnt==-1)
		{
			puts("epoll_wait() error");
			break;
		}

		for(i=0; i<event_cnt; i++)
		{
			if(ep_events[i].data.fd==serv_sock)
			{
				adr_sz=sizeof(clnt_adr);
				clnt_sock= accept(serv_sock, (struct sockaddr*)&clnt_adr, &adr_sz);
                readClient.push_back(clnt_sock);
				event.events=EPOLLIN;
				event.data.fd=clnt_sock;
				epoll_ctl(epfd, EPOLL_CTL_ADD, clnt_sock, &event);
				printf("connected client: %d \n", clnt_sock);

                for(int i= pubCnt*2; i < pubCnt*2+2; i++){
                    mediaClient[i]=socket(PF_INET, SOCK_STREAM, 0);   
                    if(mediaClient[i]==-1)
                        error_handling("socket() error");
                    memset(&media_adr, 0, sizeof(media_adr));
                    media_adr.sin_family=AF_INET;
                    media_adr.sin_addr.s_addr=htonl(INADDR_ANY);
                    media_adr.sin_port=htons(mediaServerPort[i]);
                    
                    if(connect(mediaClient[i], (struct sockaddr*)&media_adr, sizeof(media_adr))==-1)
                        error_handling("connect() error!");
                    else
                        printf("Connected... %d to %d\n", mediaClient[i], mediaServerPort[i]);
                }
                pubCnt++;
			}
			else
			{
                for(int i=0; i< readClient.size(); i++){
                    if(readClient[i] == ep_events[i].data.fd){
                        str_len=read(ep_events[i].data.fd, buf, BUF_SIZE);
                        buf[str_len] = 0;
                        printf("read from pub %s\n",buf);
                        if(str_len==0)    // close request!
                        {
                            epoll_ctl(
                                epfd, EPOLL_CTL_DEL, ep_events[i].data.fd, NULL);
                            close(ep_events[i].data.fd);
                            printf("closed client: %d \n", ep_events[i].data.fd);
                        }
                        else
                        {
                            for(int j=(pubCnt-1)*2; j < (pubCnt-1)*2+2; j++){
                                write(mediaClient[j], buf, str_len);    // relay!
                                printf("write to media %s", buf);
                            }
                            buf[0] = 0;
                        }
                    }
                }
					
	
			}
		}
	}
	close(serv_sock);
	close(epfd);
	return 0;
}

