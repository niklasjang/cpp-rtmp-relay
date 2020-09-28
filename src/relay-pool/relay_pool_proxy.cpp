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


#define BUF_SIZE 100
#define EPOLL_SIZE 50
#define THREAD_POOL_SIZE 4

using namespace std;

int serv_sock, clnt_sock;
sockaddr_in serv_adr, clnt_adr, media_adr;
socklen_t adr_sz;
int str_len, i;


//object for publish, rest apit
epoll_event *accept_ep_events;
epoll_event accept_event;
int accept_epfd, accept_event_cnt;

int optval = 1;
//퍼블리셔 접속시 미디어 서버 두 개씩 연결
int strmCnt = 0;
vector<thread> stream;
vector<int> epollObj;

namespace ThreadPool
{
	class ThreadPool
	{
	public:
		ThreadPool(size_t num_threads);
		~ThreadPool();

		// job 을 추가한다.
		template <class F, class... Args>
		std::future<typename std::result_of<F(Args...)>::type> EnqueueJob(
			F &&f, int t_idx, Args &&... args);

	// to be changed to private
	public:
		// 총 Worker 쓰레드의 개수.
		size_t num_threads_;
		// Worker 쓰레드를 보관하는 벡터.
		std::vector<std::thread> worker_threads_;
		// 쓰레드에 [0,num_threads_)의 idx 맵핑
		std::map<std::thread::id, int> threads_idxes_;
		// 할일들을 보관하는 job 큐.
		std::queue<std::function<void()>>* jobs_;
		// 위의 job 큐를 위한 cv 와 m.
		std::condition_variable* cv_job_q_;
		std::mutex* m_job_q_;

		// 모든 쓰레드 종료
		bool stop_all;

		// Worker 쓰레드
		void WorkerThread();
	};

	ThreadPool::ThreadPool(size_t num_threads)
		: num_threads_(num_threads), stop_all(false)
	{
		jobs_ = new std::queue<std::function<void()>>[num_threads];
		cv_job_q_ = new std::condition_variable[num_threads];
		m_job_q_ = new std::mutex[num_threads];
		worker_threads_.reserve(num_threads_);
		for (size_t i = 0; i < num_threads_; ++i)
		{
			worker_threads_.emplace_back([this]() { this->WorkerThread(); });
			threads_idxes_.insert(make_pair(worker_threads_[i].get_id(), i));
		}
	}

	void ThreadPool::WorkerThread()
	{
		int t_idx = threads_idxes_.find(this_thread::get_id())->second;
		while (true)
		{
			printf("thread idx wait : %d\n", t_idx);
			std::unique_lock<std::mutex> lock(m_job_q_[t_idx]);
			cv_job_q_[t_idx].wait(lock, [this, &t_idx]() { return !this->jobs_[t_idx].empty() || stop_all; });
			if (stop_all && this->jobs_[t_idx].empty())
			{
				printf("thread idx stop all : %d\n", t_idx);
				return;
			}
			printf("thread idx awake : %d\n", t_idx);

			// 맨 앞의 job 을 뺀다.
			std::function<void()> job = std::move(jobs_[t_idx].front());
			jobs_[t_idx].pop();
			lock.unlock();

			// 해당 job 을 수행한다 :)
			job();
		}
	}

	ThreadPool::~ThreadPool()
	{
		stop_all = true;
		for (size_t i = 0; i < num_threads_; ++i){
			cv_job_q_[i].notify_all();
		}

		for (auto &t : worker_threads_)
		{
			t.join();
		}
	}

	template <class F, class... Args>
	std::future<typename std::result_of<F(Args...)>::type> ThreadPool::EnqueueJob(
		F &&f, int t_idx, Args &&... args)
	{
		if (stop_all)
		{
			throw std::runtime_error("ThreadPool 사용 중지됨");
		}

		printf("enqueue t_idx %d\n", t_idx);

		using return_type = typename std::result_of<F(Args...)>::type;
		auto job = std::make_shared<std::packaged_task<return_type()>>(
			std::bind(std::forward<F>(f), std::forward<Args>(args)...));
		std::future<return_type> job_result_future = job->get_future();
		{
			std::lock_guard<std::mutex> lock(m_job_q_[t_idx]);
			jobs_[t_idx].push([job]() { (*job)(); });
		}
		printf("enqueue t_idx noti %d\n", t_idx);

		cv_job_q_[t_idx].notify_one();
		return job_result_future;
	}

} // namespace ThreadPool

void error_handling(string buf)
{
	cout << buf << "\n";
	exit(1);
}

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

map<int, Stream> strms_; //스트림 정보 저장
size_t n_thrd = THREAD_POOL_SIZE;
condition_variable cv_q;
mutex mtx_q;
bool stop_all = false;

Stream create_stream(int _sink)
{
	Stream strm(_sink, 8000, 2);
	if (!strm.connectMediaServer())
	{
		cout << "Error:" << __FUNCTION__ << ":모든 요청 완료되지 못함\n";
	}
	return strm;
}

int run(Stream& strm)
{
	printf("before read----\n");
	memset(strm.buf, 0, BUF_SIZE);
	str_len = read(strm.sink, strm.buf, BUF_SIZE);
	printf("after read----\n");
	strm.buf[str_len] = 0;
	printf("read from client %s", strm.buf);

	if (str_len == 0) // close request!
	{
		printf("closed client: %d \n", strm.sink);
		//TODO media server clost, stream 정보 삭제,
		//break go to : exit
		return -1;
	}
	else
	{
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
	//ip port 입력
	if (argc != 2)
	{
		printf("Usage : %s <port>\n", argv[0]);
		exit(1);
	}

	//소켓 생성, ip, port, option 설정
	serv_sock = socket(PF_INET, SOCK_STREAM, 0);
	memset(&serv_adr, 0, sizeof(serv_adr));
	serv_adr.sin_family = AF_INET;
	serv_adr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_adr.sin_port = htons(atoi(argv[1]));
	setsockopt(serv_sock, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));

	//bind()
	if (bind(serv_sock, (struct sockaddr *)&serv_adr, sizeof(serv_adr)) == -1)
		error_handling("bind() error");

	//listen()
	if (listen(serv_sock, 5) == -1)
		error_handling("listen() error");

	//epoll 객체 생성
	accept_epfd = epoll_create(EPOLL_SIZE);
	accept_ep_events = new epoll_event[EPOLL_SIZE];

	//클라이언트 요청 이벤트 등록
	accept_event.events = EPOLLIN;
	accept_event.data.fd = serv_sock;
	epoll_ctl(accept_epfd, EPOLL_CTL_ADD, serv_sock, &accept_event);

	//쓰레드 풀 생성
	ThreadPool::ThreadPool pool(THREAD_POOL_SIZE);
	// SAVE : job()이 리턴한 값이 set되는 것을 기다리는 것이 필요한 경우 사용
	// std::vector<std::future<int>> futures; 

	while (1)
	{
		accept_event_cnt = epoll_wait(accept_epfd, accept_ep_events, EPOLL_SIZE, -1);
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
				//스트림 입력 설정
				// 엣지 트리거 : noti 이후 실제로 쓰레드가 awake할 때까지 계속 이벤트를 보내면 context switching 비용이 증가한다.
				// 따라서 엣지 트리거로 하되, 앱 레벨에서 모든 데이터를 다 읽었음을 보장하는 로직이 추가 되어야 한다.
				accept_event.events = EPOLLIN|EPOLLET;
				accept_event.data.fd = clnt_sock;
				epoll_ctl(accept_epfd, EPOLL_CTL_ADD, clnt_sock, &accept_event);
				//스트림 생성
				Stream strm = create_stream(clnt_sock);
				if( 1 == 1) { // TODO : 스트림 생성에 오류가 없으면
					//스트림 입력 설정
					strmCnt++;
					strms_.insert(make_pair(clnt_sock, strm));
					printf("stream insert\n");
				}
			}
			else
			{
				printf("accept_epdf data event occur %d,  %d\n", i, clnt_sock);
				// 1. 어떤 fd로 데이터가 들어왔는지 파악해서 해당하는 스트림의 정보를 찾는다. 
				// 2. 해당 스트림 정보에서 read, relay하는 run()을 추가한다.
				int data_sink = accept_ep_events[i].data.fd;
				// 잡이 실행된 결과가 필요할 때 이렇게 사용하기
				// SAVE : futures.emplace_back(pool.EnqueueJob(run, strms_.find(data_sink)->second));
				pool.EnqueueJob(run, clnt_sock % 4, strms_.find(data_sink)->second);
			}
		}
	}
	close(serv_sock);
	close(accept_epfd);
	return 0;
}