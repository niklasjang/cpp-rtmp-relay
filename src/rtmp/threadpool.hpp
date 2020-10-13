#ifndef __THREADPOOL_HPP__
#define __THREADPOOL_HPP__

#include <vector>
#include <thread>
#include <future>
#include <functional>
#include <queue>
#include <string>
#include <map>
#include <mutex>
#include <condition_variable>

using namespace std;

class ThreadPool
{
    // to be changed to private
public:
    // 총 Worker 쓰레드의 개수.
    size_t num_threads_;
    // Worker 쓰레드를 보관하는 벡터.
    std::vector<std::thread> worker_threads_;
    // 쓰레드에 [0,num_threads_)의 idx 맵핑
    std::map<std::thread::id, int> threads_idxes_;
    // 할일들을 보관하는 job 큐.
    std::queue<std::function<void()>> *jobs_;
    // 위의 job 큐를 위한 cv 와 m.
    std::condition_variable *cv_job_q_;
    std::mutex *m_job_q_;

    // 모든 쓰레드 종료
    bool stop_all;

    // Worker 쓰레드
    ThreadPool(size_t num_threads)
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

    void WorkerThread()
    {
        int t_idx = threads_idxes_.find(this_thread::get_id())->second;
        while (true)
        {
            std::unique_lock<std::mutex> lock(m_job_q_[t_idx]);
            cv_job_q_[t_idx].wait(lock, [this, &t_idx]() { return !this->jobs_[t_idx].empty() || stop_all; });
            if (stop_all && this->jobs_[t_idx].empty())
            {
                printf("thread idx stop all : %d\n", t_idx);
                return;
            }
            // 맨 앞의 job 을 뺀다.
            std::function<void()> job = std::move(jobs_[t_idx].front());
            jobs_[t_idx].pop();
            lock.unlock();

            // 해당 job 을 수행한다 :)
            job();
        }
    }

    ~ThreadPool()
    {
        stop_all = true;
        for (size_t i = 0; i < num_threads_; ++i)
        {
            cv_job_q_[i].notify_all();
        }

        for (auto &t : worker_threads_)
        {
            t.join();
        }
    }

    template <class F, class... Args>
    std::future<typename std::result_of<F(Args...)>::type> EnqueueJob(
        F &&f, int t_idx, Args &&... args)
    {
        if (stop_all)
        {
            throw std::runtime_error("ThreadPool 사용 중지됨");
        }

        using return_type = typename std::result_of<F(Args...)>::type;
        auto job = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        std::future<return_type> job_result_future = job->get_future();
        {
            std::lock_guard<std::mutex> lock(m_job_q_[t_idx]);
            jobs_[t_idx].push([job]() { (*job)(); });
        }
        cv_job_q_[t_idx].notify_one();
        return job_result_future;
    }
};

#endif