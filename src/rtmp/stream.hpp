#ifndef __STREAM_HPP__
#define __STREAM_HPP__

#define BUF_SIZE 100

#include <vector>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <cstdio>
#include <string>

using namespace std;

void error_handling(string buf);

class Stream
{
public:
	int sink;
	int requestCnt; //요청 받은 릴레이 갯수
	int relayCnt;	//진행 중인 릴레이 갯수
	char buf[BUF_SIZE];
	vector<int> v_src;
	vector<string> v_buf; //편의를 위해 데이터 string으로 받아서 벡터에 저장
	vector<sockaddr_in> v_media_adr;


	/**
	 * media server port
	 * 8000, 8001, ... 순서대로 연결
	*/
	Stream(int _sink, int _mediaOffset, int _mediaCnt)
	{
		sink = _sink;
		requestCnt = _mediaCnt;
		for (int i = 0; i < _mediaCnt; i++)
		{
			//소켓 생성
			int src = socket(PF_INET, SOCK_STREAM, 0);
			v_src.push_back(src);
			//미디어 서버 주소 설정.
			//default :ip = localhost, port = [mediaOffset, mediaOffset+mediaCnt)
			sockaddr_in media_adr;
			memset(&media_adr, 0, sizeof(media_adr));
			media_adr.sin_family = AF_INET;
			media_adr.sin_addr.s_addr = htonl(INADDR_ANY);
			media_adr.sin_port = htons(_mediaOffset + i);
			v_media_adr.push_back(media_adr);
		}
	}

	void setRelayCnt(int _successCnt)
	{
		relayCnt = _successCnt;
	}

	bool connectMediaServer()
	{
		int successCnt = 0;
		for (int i = 0; i < requestCnt; i++)
		{
			if (connect(v_src[i], (struct sockaddr *)&v_media_adr[i], sizeof(v_media_adr[i])) == -1)
			{
				error_handling("connect() error!");
			}
			else
			{
				printf("Connected... %d to %d\n", v_src[i], ntohs(v_media_adr[i].sin_port));
				successCnt++;
			}
		}
		setRelayCnt(successCnt);
		return successCnt == requestCnt;
	}

	void removeIdx(int idx)
	{
		//idx번째 스트림 정보 지우기
		//src_sock에서 지우기
		//media_adr에서 지우기
	}
};

#endif