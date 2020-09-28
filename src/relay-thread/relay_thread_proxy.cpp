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
#include <thread>

#define BUF_SIZE 100
#define EPOLL_SIZE 50
using namespace std;

int serv_sock, clnt_sock;
sockaddr_in serv_adr, clnt_adr, media_adr;
socklen_t adr_sz;
int str_len, i;
char buf[BUF_SIZE];

//object for publish, rest apit
epoll_event *accept_ep_events;
epoll_event accept_event;
int accept_epfd, accept_event_cnt;

//object for data stream
epoll_event *data_ep_events;
epoll_event data_event;
int data_epfd, data_event_cnt;

int optval = 1;
//퍼블리셔 접속시 미디어 서버 두 개씩 연결
int mediaServerPort[4] = {8000, 8001, 8002, 8003};
vector<int> readClient;
int mediaClient[4] = {0,0,0,0};
int strmCnt = 0;
vector<thread> stream;
vector<int> epollObj;

void error_handling(string buf)
{
	cout<< buf<<"\n";
	exit(1);
}

class Stream{
public:
	int sink;
	int requestCnt; //요청 받은 릴레이 갯수
	int relayCnt;   //진행 중인 릴레이 갯수
	vector<int> v_src;
	vector<string> v_buf; //편의를 위해 데이터 string으로 받아서 벡터에 저장
	vector<sockaddr_in> v_media_adr;

	Stream(int _sink, int _mediaOffset, int _mediaCnt){
		sink = _sink;   
		requestCnt = _mediaCnt;
		for(int i=0; i<_mediaCnt; i++){
			//소켓 생성
			int src = socket(PF_INET, SOCK_STREAM, 0);
			v_src.push_back(src);
			//미디어 서버 주소 설정. 
			//default :ip = localhost, port = [mediaOffset, mediaOffset+mediaCnt)
			sockaddr_in media_adr;
			memset(&media_adr, 0, sizeof(media_adr));
			media_adr.sin_family=AF_INET;
			media_adr.sin_addr.s_addr=htonl(INADDR_ANY);
			media_adr.sin_port=htons(_mediaOffset+i);
			v_media_adr.push_back(media_adr);
		}
	}

	void setRelayCnt(int _successCnt){
		relayCnt = _successCnt;
	}

	bool connectMediaServer(){
		int successCnt = 0;
		for(int i=0; i< requestCnt; i++){
			if(connect(v_src[i], (struct sockaddr*)&v_media_adr[i], sizeof(v_media_adr[i]))==-1){
				error_handling("connect() error!");
			}
			else{
				printf("Connected... %d to %d\n", v_src[i], ntohs(v_media_adr[i].sin_port));
				successCnt++;
			}
				
		}
		setRelayCnt(successCnt);
		return successCnt == requestCnt;
	}
	
	void removeIdx(int idx){
		//idx번째 스트림 정보 지우기
		//src_sock에서 지우기
		//media_adr에서 지우기
	}
};

vector<Stream> v_strm; //스트림 정보 저장
vector<thread> v_thrd; //쓰레드 정보 저장

Stream create_stream(int _sink){
	Stream strm(_sink, 8000, 2);
	if(!strm.connectMediaServer()){
		cout<<"Error:"<< __FUNCTION__<<":모든 요청 완료되지 못함\n";
	}
	v_strm.push_back(strm);
	return strm;
}


void run(Stream strm){
	while(true)
	{
		data_event_cnt=epoll_wait(data_epfd, data_ep_events, EPOLL_SIZE, -1);
		if(data_event_cnt==-1)
		{
			puts("data_epoll_wait() error");
			break;
		}

		for(i=0; i<data_event_cnt; i++)
		{
			if(data_ep_events[i].data.fd==strm.sink)
			{
				adr_sz=sizeof(clnt_adr);
				// TOTO clnt_addr이 critical section인지 확인
				str_len=read(strm.sink, buf, BUF_SIZE);
				buf[str_len] = 0;
				printf("read from client %s", buf);
				// TODO 읽은 데이터 버퍼로 저장
				// strm.v_buf.push_back(buf.toString());
				if(str_len==0)    // close request!
				{
					epoll_ctl(
						data_epfd, EPOLL_CTL_DEL, data_ep_events[i].data.fd, NULL);
					close(data_ep_events[i].data.fd);
					printf("closed client: %d \n", data_ep_events[i].data.fd);
					//TODO media server clost, stream 정보 삭제,
					//break go to : exit
				}
				else
				{
					for(int j=0; j<strm.relayCnt; j++){
						write(strm.v_src[j], buf, str_len);    // relay!
						printf("write to media %s", buf);
					}
					buf[0] = 0;
				}
			}else{
				printf("Ignore: 스트림 : thread 현재는 1 : 1\n");
			}
		}
	}
}

int main(int argc, char *argv[])
{
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
	accept_epfd=epoll_create(EPOLL_SIZE);
	accept_ep_events= new epoll_event[EPOLL_SIZE];
	data_epfd=epoll_create(EPOLL_SIZE);
	data_ep_events= new epoll_event[EPOLL_SIZE];

    //클라이언트 요청 이벤트 등록
	accept_event.events=EPOLLIN;
	accept_event.data.fd=serv_sock;	
	epoll_ctl(accept_epfd, EPOLL_CTL_ADD, serv_sock, &accept_event);

	while(1)
	{
		accept_event_cnt=epoll_wait(accept_epfd, accept_ep_events, EPOLL_SIZE, -1);
		if(accept_event_cnt==-1)
		{
			puts("accept_epoll_wait() error");
			break;
		}

		for(i=0; i<accept_event_cnt; i++)
		{
			if(accept_ep_events[i].data.fd==serv_sock)
			{
				adr_sz=sizeof(clnt_adr);
				clnt_sock= accept(serv_sock, (struct sockaddr*)&clnt_adr, &adr_sz);
                readClient.push_back(clnt_sock);

				//스트림 생성				
				Stream strm = create_stream(clnt_sock);
				//스트림 입력 설정
				data_event.events=EPOLLIN;
				data_event.data.fd=clnt_sock;
				epoll_ctl(data_epfd, EPOLL_CTL_ADD, clnt_sock, &data_event);
				// thread thrd(run, strm);
				v_thrd.push_back(thread(run, strm));
                strmCnt++;
			}else{
				printf("Error: As for now, rest api is not reachable\n");
				printf("Error: Data Stream must be catched in data_epfd\n");
			}
		}
	}
	close(serv_sock);
	close(accept_epfd);
	for(int i=0; i< v_thrd.size(); i++){
		v_thrd[i].join();
	}
	return 0;
}


// else
// {
// 	for(int i=0; i< readClient.size(); i++){
// 		if(readClient[i] == accept_ep_events[i].data.fd){
// 			str_len=read(accept_ep_events[i].data.fd, buf, BUF_SIZE);
// 			buf[str_len] = 0;
// 			printf("read from pub %s\n",buf);
// 			if(str_len==0)    // close request!
// 			{
// 				epoll_ctl(
// 					accept_epfd, EPOLL_CTL_DEL, accept_ep_events[i].data.fd, NULL);
// 				close(accept_ep_events[i].data.fd);
// 				printf("closed client: %d \n", accept_ep_events[i].data.fd);
// 			}
// 			else
// 			{
// 				for(int j=(strmCnt-1)*2; j < (strmCnt-1)*2+2; j++){
// 					write(mediaClient[j], buf, str_len);    // relay!
// 					printf("write to media %s", buf);
// 				}
// 				buf[0] = 0;
// 			}
// 		}
// 	}
// }