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
#include <condition_variable>
#include <mutex>
#include <map>

#include <chrono>
#include <cstdio>
#include <functional>
#include <future> 
#include <queue>

#include "stream.hpp"
#include "threadpool.hpp"

#define THREAD_POOL_SIZE 4

using namespace std;

void error_handling(string buf);


/**
 * 1. 이벤트가 발생한 스트림에 대해서 하나의 패킷을 읽고 전달한다. 
 * 2. TODO : restful 요청인지 판단한다.
 * 3. 접속이 끊기면 해당 스트림을 제거한다.
*/
int run(map<int, Stream>& strms, Stream& strm)
{
	int str_len = 0;
	memset(strm.buf, 0, BUF_SIZE);
	str_len = read(strm.sink, strm.buf, BUF_SIZE);
	strm.buf[str_len] = 0;
	if (str_len == 0) // close request!
	{
		printf("close stream with data_sink %d \n", strm.sink);
		strms.erase(strm.sink);
		return -1;
	}
	else
	{
		printf("read from client %s\n", strm.buf);
		for (int i = 0; i < strm.relayCnt; i++)
		{
			write(strm.v_src[i], strm.buf, str_len); // relay!
			printf("write to media %s", strm.buf);
		}
	}
	return 0;
}

int main(int argc, char *argv[])
{
	const int kReUsePort = 1;
	const int kEpoolSize = 50;
	const int kThreadPoolSize = 4;

	int serv_sock;
	int clnt_sock;
	int accept_epfd;
	int accept_event_cnt = -1;
	int i;

	sockaddr_in serv_adr;
	sockaddr_in clnt_adr, media_adr;
	socklen_t adr_sz;
	epoll_event *accept_ep_events;

	ThreadPool pool(kThreadPoolSize);

	map<int, Stream> strms; //스트림 정보 저장

	/**
	 * 1. check input
	*/
	if (argc != 2)
	{
		printf("Usage : %s <port>\n", argv[0]);
		exit(1);
	}
	/**
	 * 1. sockek()
	 * 2. bind()
	 * 3. listen()
	*/
	serv_sock = socket(PF_INET, SOCK_STREAM, 0);
	memset(&serv_adr, 0, sizeof(serv_adr));
	serv_adr.sin_family = AF_INET;
	serv_adr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_adr.sin_port = htons(atoi(argv[1]));
	setsockopt(serv_sock, SOL_SOCKET, SO_REUSEPORT, &kReUsePort, sizeof(kReUsePort));
	if (bind(serv_sock, (struct sockaddr *)&serv_adr, sizeof(serv_adr)) == -1)
		error_handling("bind() error");
	if (listen(serv_sock, 5) == -1)
		error_handling("listen() error");
	/**
	 * 1. epoll_create()
	 * 2. epool_ctl()
	*/
	accept_epfd = epoll_create(kEpoolSize);
	accept_ep_events = new epoll_event[kEpoolSize];
	epoll_event accept_event;
	accept_event.events = EPOLLIN;
	accept_event.data.fd = serv_sock;
	epoll_ctl(accept_epfd, EPOLL_CTL_ADD, serv_sock, &accept_event);


	/**
	 * 1. epool_wait()
	 * 2. accpet()
	 * 3-1. create stream
	 * 3-2. enqueue stream
	*/
	while (1)
	{
		accept_event_cnt = epoll_wait(accept_epfd, accept_ep_events, kEpoolSize, -1);
		if (accept_event_cnt == -1)
		{
			puts("accept_epoll_wait() error");
			break;
		}

		for (i = 0; i < accept_event_cnt; i++)
		{
			if (accept_ep_events[i].data.fd == serv_sock)
			{
				adr_sz = sizeof(clnt_adr);
				clnt_sock = accept(serv_sock, (struct sockaddr *)&clnt_adr, &adr_sz);
				/**
				 * EPOOLET
				 * 
				 * 엣지 트리거 : noti 이후 실제로 쓰레드가 awake할 때까지 계속 이벤트를 보내면 context switching 비용이 증가한다.
				 * 따라서 엣지 트리거로 하되, 앱 레벨에서 모든 데이터를 다 읽었음을 보장하는 로직이 추가 되어야 한다.
				 * 
				*/
				accept_event.events = EPOLLIN|EPOLLET;
				accept_event.data.fd = clnt_sock;
				epoll_ctl(accept_epfd, EPOLL_CTL_ADD, clnt_sock, &accept_event);

				// create stream
				Stream strm(clnt_sock, 1935, 2);
				if (strm.connectMediaServer())
				{
					strms.insert(make_pair(clnt_sock, strm));
					printf("stream created successfully\n");
				}else{
					error_handling("create stream error");
				}
			}
			else
			{
				/**
				 * 1. data_sink를 기반으로 몇 번째 쓰레드를 사용할 것인지 결정한다. 
				 * 2. 데이터 수신 이벤트가 발생한 stream을 enqueue한다. 
				*/
				int data_sink = accept_ep_events[i].data.fd;
				pool.EnqueueJob(run, data_sink % 4, strms, strms.find(data_sink)->second);
			}
		}
	}
	close(serv_sock);
	close(accept_epfd);
	return 0;
}

void error_handling(string buf)
{
	printf("%s\n", buf.c_str());
	exit(1);
}