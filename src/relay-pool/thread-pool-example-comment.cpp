#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace ThreadPool
{
    /**
     * 쓰레드풀 사용자는 실행을 원하는 함수를 쓰레드풀에 전달한다.
     * 
     * 
     * 
     * 
    */
    class ThreadPool
    {
    public:
        ThreadPool(size_t num_threads);
        ~ThreadPool();

        // job 을 추가한다.
        template <class F, class... Args>
        std::future<typename std::result_of<F(Args...)>::type> EnqueueJob(
            F &&f, Args &&... args);

    private:
        // 총 Worker 쓰레드의 개수.
        size_t num_threads_;
        // Worker 쓰레드를 보관하는 벡터.
        std::vector<std::thread> worker_threads_;
        /**
         * 할일들을 보관하는 job 큐.
         * C++에는 일반적인 타입의 함수 포인터를 저장할 수 있는 컨테이너가 없어서 void()타입의 컨테이너를 사용한다.
        */
        std::queue<std::function<void()>> jobs_;
        // 위의 job 큐를 위한 cv 와 m.
        std::condition_variable cv_job_q_;
        std::mutex m_job_q_;

        // 모든 쓰레드 종료를 위한 flag
        bool stop_all;

        // Worker 쓰레드
        void WorkerThread();
    };

    ThreadPool::ThreadPool(size_t num_threads) : num_threads_(num_threads), stop_all(false)
    {
        //worker_threads_ vector의 capacity가 num_threads_보다 크도록 설정 
        worker_threads_.reserve(num_threads_);
        /**
         * emplace_back()은 thread()의 생성자에 인자를 `완벽한 전달`하여 불필요한 이동 연산을 하지 않는다. 
         * 
        */
        for (size_t i = 0; i < num_threads_; ++i)
        {
            /**
             * [] : 람다 내부에서 사용할 외부의 값.
             * () : 람다로 전달할 parameter
            */
            worker_threads_.emplace_back([this]() { this->WorkerThread(); });
        }
    }

    void ThreadPool::WorkerThread()
    {
        while (true)
        {
            /**
             * 뮤텍스에 lock을 하고 unlock을 하지않으면 데드락의 위험이 있다.
             * unlock을 까먹지 않도록 하기 위해서 뮤텍스를 객체로 감싸고,
             * 그 객체의 deconstructor에 unlock을 추가하면 unlock을 까먹지 않을 수 있다.
             * 뮤텍스를 감싸는 cpp std 객체는 lock_quard와 unique_lock이 있다.  
             * 
             * lock_quard
             * 1. 뮤텍스를 인자로 전달받아서 unique_lock 객체가 소멸될 때 unlock을 한다.
             * unique_lock
             * 1. 뮤텍스를 인자로 전달받아서 unique_lock 객체가 소멸될 때 unlock을 한다.
             * 2. unlock 후에 다시 lock을 할 수 있다.
             */
            std::unique_lock<std::mutex> lock(m_job_q_);
            /**
             * cv.wait(lck, pref)
             * lck : lock
             * pref : 참이 될 때까지 기다릴 조건
             * 
             * wait()이 호출되면 notified될 때까지 block된다. block되기 전에 자동적으로 lck을 unlock한다.
             * 기본적으로는 notified되면 unblock되는데, pred가 전달되었다면 pred가 참이될 때만 unblock될 수 있다.
             * 
            */
            cv_job_q_.wait(lock, [this]() { return !this->jobs_.empty() || stop_all; });


            //wait이 return된 이유가 stop_all 때문인 경우
            if (stop_all && this->jobs_.empty())
            {
                return;
            }

            // 맨 앞의 job 을 뺀다.
            std::function<void()> job = std::move(jobs_.front());
            jobs_.pop();
            lock.unlock();

            // 해당 job 을 수행한다 :)
            job();
        }
    }

    ThreadPool::~ThreadPool()
    {
        stop_all = true;
        cv_job_q_.notify_all();

        for (auto &t : worker_threads_)
        {
            t.join();
        }
    }

    /**
     * 이동생성자 
     * 
     * 좌측값 : 주소를 얻을 수 있는 값. 
     * 우측값 : 주소를 얻을 수 없는 값. ex) 임시 객체의 형태로 return되는 값
     * & : 좌측값 reference. 
     *   : 좌측값 reference도 좌측값이 된다.
     *   : const T&의 경우 우측값의 reference도 받을 수 있음 
     * && : 우측값 reference
     *    : 우측값 reference는 이름이 있기 때문에 좌측값이다.
     *    : ex) T &&name = (우측값)
     * 
     * 
     * T::T() : 일반 생성자
     * T::T(const T& t) : 복사 생성자
     * T::T(T&& t){ // 이동 생성자
     *  ptr_ = t.ptr_; // 우측값의 메모리를 가져온다.
     *  t.ptr_ = nullptr // 임시 객체 메모리를 소멸하지 못하게 한다. 
     * }
     * 
     * 이동 생성자의 경우 이동 과정에서 메모리에 문제가 생기면
     * 데이터가 아예 사라지기 때문에 문제가 발생하면 안된다.
     * 따라서 생성자에 noexcept를 반드시 표기해야 이동 생성자가 호출된다. 
     * 
     * 백터의 경우 capacity가 부족할 때 2배로 늘리고 데이터를 복사하는데,
     * 이 때 이동생성자를 except로 설정하면 복사를 하지 않고 이동생성자로 수행한다. 
     * 
     * std::move()는 lvalue는 rvalue로 바꾸어주기만 한다. 이동생성자를 호출하지 않는다. 
     * 따라서 std::move()를 사용하는 것뿐만 아니라 이동생성자를 직접 구현해야 이동생성자를 사용할 수 있다. 
     * Reference : https://modoocode.com/227
     * 
     */

    /** 
     * 우측 값 reference
     * vector의 emplace_back은 인자를 전달받아서, 객체를 생성하고 push를 해준다.
     * 이 때 인자의 복사/이동은 일어나지 않는다. 인자들은 의도한 객체의 생성자의 인자로 전달되어야 한다.
     * 
     * class A {};
     * void g(A& a) { std::cout << "좌측값 레퍼런스 호출" << std::endl; }
     * void g(const A& a) { std::cout << "좌측값 상수 레퍼런스 호출" << std::endl; }
     * void g(A&& a) { std::cout << "우측값 레퍼런스 호출" << std::endl; }
     * 에 대해서 
     * 
     * template <typename T> void wrapper(T u){ g(u); }의 경우 모든 값이 좌측값 reference로 전달된다.  
     * 아래 처럼 고치면 
     * template <typename T> void wrapper(T& u){ g(u); }의 경우 const가 아닌 좌측값 객체 전달에 쓰인다.
     * template <typename T> void wrapper(const T& u){ g(u); } 의 경우 const 좌측값 객체, 우측값 객체 전달에 쓰인다.
     * //const T&의 경우 예외적으로 우측값도 받을 수 있다.
     * 
     * T&, const T& 두개로 나누어서 구현하면 되지만, 인자가 M개인 경우 생성자를 2^M개 만들어야 한다. 
     * 따라서 아래와 같이 보편적인 레퍼런스를 사용한다.  
     * 
     * template <typename T>
     * void wrapper(T&& u) {
     *   g(std::forward<T>(u));
     * }
     * 
     * 템플릿 인자 T에 대해서 우측갑 레퍼런스로 받는 형태를 `보편적 레퍼런스`라고 한다. 
     * 이는 우측값만 레퍼런스로 받는 형태와 달리 좌측값도 받는다. 
     * 
     * 이 때 레퍼런스 겹침 규칙을 사용해서 
     * (우측값 ref가 전달된 경우는 우측값 ref로,
     * 좌측값 ref가 전달된 경우는 좌측값 ref로 받는다.)
     * 
     * typedef int& T;
     * T& r1;   // int& &; r1 은 int&
     * T&& r2;  // int & &&;  r2 는 int&
     * typedef int&& U;
     * U& r3;   // int && &; r3 는 int&
     * U&& r4;  // int && &&; r4 는 int&&
     *   
     * A a;
     * const A ca;
     * wrapper(a); // A&로 추론됨. A& && = A&
     * wrapper(ca); // const A&로 추론됨. const A& && = const A&
     * wrapper(A()); // A&&로 추론됨(우측값 ref) A&& && = A&&
     * 
     * u = A& | const A& | A&&로 추론되는데, u는 좌측갑으로 선언되어 있음.
     * 결론적으로는, 전달받은 u가 우측값 reference일 때만 std:move()를 해서 전달해주어야 wrapper()에 전달한 의도가 그대로 전달됨.
     * 
     * g(forward<T>(u));
     * 
     * forward<T>는 u의 타입을 레퍼런스 겹침 규칙을 사용해서 우측값 ref가 전달된 경우는 우측값 ref로,
     * 좌측값 ref가 전달된 경우는 좌측값 ref로 만든다.
     * 
     * Reference : https://modoocode.com/228#page-heading-2
     * 
    */
    template <class F, class... Args>
    std::future<typename std::result_of<F(Args...)>::type> ThreadPool::EnqueueJob(F &&f, Args &&... args)
    {
        if (stop_all)
        {
            throw std::runtime_error("ThreadPool 사용 중지됨");
        }
        /**
         * std::function<T>
         * ()를 붙여서 호출할 수 있는 Callable(람다, 함수 etc)를 객체의 형태로 보관하는 컨테이너.
         * 함수포인터는 함수만 보관하다면, function은 Callable을 모두 보관하는 객체
         * <T>는 인자와 리턴값의 타입. 
         * ex)std::function<int(const string&)> f1 = some_func1;
         * ex)std::function<void(char)> f2 = S();
         * ex)std::function<void()> f3 = []() { std::cout << "Func3 호출! " << std::endl; };
         * 
         * 단, 멤버함수를 전달하는 경우 f3을 f3()과 같이 호출할 때 어떤 인스턴스에 대한 호출인지 알 수 없음.
         * 따라서 모든 멤버함수는 자신을 호출한 객체를 암묵적으로 인자로 받는 것을 사용해서 ref로 전달한다.
         * 그리고 function 객체를 사용할 때는 호출하고자 하는 객체를 전달한다. 
         * 
         * 그리고 멤버함수는 함수 이름만으로는 함수의 주소값을 전달할 수 없는 특징을 가진다.
         * 멤버 함수는 함수 이름이 함수 주소값으로 암시적 변환이 일어나지 않기 때문에 &를 붙혀서 주소를 명시적으로 전달한다. 
         * 
         * A a(5);
         * std::function<int(A&)> f1 = &A::some_func;
         * std::function<int(const A&)> f2 = &A::some_const_function;
         * f1(a);
         * f2(a);
         * Reference : https://modoocode.com/254
         * 
        */
       /**
        * std:bind(pred, param)
        * 
        * bind()를 호출할 때 전달할 값을 std::ref()없이 그냥 전달하면 값이 복사되어서 전달된다.
        * std:ref()로 감싸고 전달해야 reference가 복사되어 do_something_with_s1 호출 이후에도
        * s1의 값이 변경된다. 
        * 
        * void do_something(S& s1, const S& s2) { s1.data = s2.data + 3; }
        * auto do_something_with_s1 = std::bind(do_something, std::ref(s1), std::placeholders::_1);
        * do_something_with_s1(s2);
        * 
        * Reference : https://modoocode.com/254
       */

        /**
         * std::packaged_task
         * 
         * packaged_task 에 전달된 함수가 리턴할 때, 그 리턴값을 promise 에 set_value 하고, 
         * 만약에 예외를 던졌다면 promise 에 set_exception 을 한다. 
         * 해당 future 는 packaged_task 가 리턴하는 future 에서 접근할 수 있다. 
         * 
         * packaged_task객체 job은 생성자에서 함수만 전달받는다.
         * 1. 함수만 전달하고 job(args...)로 호출해도 되고
         * 2. args...를 bind() 해서 전달해도 된다.
         * 아래는 2번의 방법을 적용한 모습이다. 
         * using return_type = typename std::result_of<F(Args...)>::type;
         * std::packaged_task<return_type()> job(std::bind(f, args...));
         * 
         * 
        */
        using return_type = typename std::result_of<F(Args...)>::type;
        auto job = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(f, std::forward<Args>(args)...));
        std::future<return_type> job_result_future = job->get_future();
        {
            std::lock_guard<std::mutex> lock(m_job_q_);
            /**
             * job을 수행하면서 전달하는 것이 아니라 인자로 아무것도 전달하지 않는 함수를 push하는 과정
             * 실제 수행은 WorkerThread에서 진행한다.  
             * 
             * 그냥 아래와 같이 수행하면 f의 return value을 알 수 없다.
             * jobs_.push([f, args...]() { f(args...); });
             * 
             * EnqueueJob이 job_result_future를 return하고 있음을 주목하자.
             * 
             * std::packaged_task<return_type()> job(std::bind(f, args...));
             * jobs_.push([&job]() { job(); });
             * 를 하면 job이 지역 객체라서 이미 사라진 객체애 대해서 promise를 기다린다는 에러가 출력된다.
             * shard_ptr를 사용하고 
             * jobs_.push([job]() { (*job)(); });와 같이 사용하면 람다 안에서도 job객체를 복사해서 가지고 있어서
             * job객체가 사라지지 않는다. 
             * shard_ptr에 복사 생성자를 실행해서 reference_count가 1증가한다.
             * 지역 변수job이 사라지면서 reference_count는 -1되고 경국 람다 안의 job만 유일한 포인터가 된다. 
             * 
             * 그리고 결국 push되는 형태를 람다의 형태로 인자와 return이 없는 void()형태 이다.
             * 
             * 실제 수행하는 부분에서 
             *  std::function<void()> job = std::move(jobs_.front());
             *  job();
             *  를 통해서 실행된다. 
            */
            jobs_.push([job]() { (*job)(); });
        }
        cv_job_q_.notify_one();
        return job_result_future;
    }

} // namespace ThreadPool

// 사용 예시
int work(int t, int id)
{
    printf("%d start \n", id);
    std::this_thread::sleep_for(std::chrono::seconds(t));
    printf("%d end after %ds\n", id, t);
    return t + id;
}

int main()
{
    ThreadPool::ThreadPool pool(3);

    //1. futures 백터 생성
    std::vector<std::future<int>> futures;
    for (int i = 0; i < 10; i++)
    {
        // futures 백터에
        futures.emplace_back(pool.EnqueueJob(work, i % 3 + 1, i));
    }
    for (auto &f : futures)
    {
        printf("result : %d \n", f.get());
    }
}