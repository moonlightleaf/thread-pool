//v1，最基础的线程池模型，子线程无返回值，执行完退出

#pragma once

#include <thread>
#include <functional>
#include <future>
#include <queue>
#include <mutex>
#include <condition_variable>

class ThreadPool{
public:
    using Task = std::function<void()>;

    explicit ThreadPool(std::size_t = 8);
    ~ThreadPool();
    template<typename F>
    void enqueue(F&& f);
private:
    void add_worker_();
    bool stop_;
    std::queue<Task> tasks_;
    std::vector<std::thread> workers_;
    std::mutex queue_mutex_;
    std::condition_variable cond_var_;
};

//构造函数负责向workers_容器中添加threads个工作线程
inline ThreadPool::ThreadPool(std::size_t threads) 
    :   stop_(false) {
    for (std::size_t i = 0; i < threads; ++i) {
        add_worker_();
    }
}

//析构函数通知停止，所有子线程在完成所有任务后join
inline ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock{queue_mutex_};
        stop_ = true;
    }
    cond_var_.notify_all();
    for (auto& worker : workers_) worker.join();
}

//向任务队列中添加一个任务
template<typename F>
void ThreadPool::enqueue(F&& f) {
    {
        std::unique_lock<std::mutex> lock{queue_mutex_};
        if (stop_) throw std::runtime_error("Enqueue on stopped ThreadPool\n");
        tasks_.emplace(std::forward<Task>(f));
    }
    cond_var_.notify_one();
}

//添加一个工作线程
void ThreadPool::add_worker_() {
    workers_.emplace_back([this]{
        while (true) {
            Task task;
            {
                std::unique_lock<std::mutex> lock{queue_mutex_};
                cond_var_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                if (stop_ && tasks_.empty()) return;
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    });
}