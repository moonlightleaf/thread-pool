//v2 子线程有返回值，在主线程中获取各子线程返回值

#pragma once

#include <thread>
#include <future>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <vector>

class ThreadPool{
public:
    using Task = std::function<void()>;
    explicit ThreadPool(std::size_t = 8);
    ~ThreadPool();
    template<typename F, typename... Args>
    auto enqueue(F&&, Args&& ...) -> std::future<typename std::result_of<F(Args...)>::type>;
private:
    void add_worker_();
    bool stop_;
    std::queue<Task> tasks_;
    std::vector<std::thread> workers_;
    std::mutex queue_mutex_;
    std::condition_variable cond_var_;
};

inline ThreadPool::ThreadPool(std::size_t threadsNum)
    :   stop_(false) {
    for (std::size_t i = 0; i < threadsNum; ++i) {
        add_worker_();
    }
}

inline ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock{queue_mutex_};
        stop_ = true;
    }
    cond_var_.notify_all();
    for (auto& worker : workers_) worker.join();
}

template<typename F, typename... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type> {
    //F(Args...)的返回值类型，注意，并不是enqueue的返回类型，多线程中为了异步获取子线程返回值，需要再包一层future
    using return_type = typename std::result_of<F(Args...)>::type;
    //为了跟tasks_的类型std::function<void()>匹配，需要构建一个无参可调用对象，因此使用std::bind，再包一层
    //packaged_task是因为需要在不立即执行的情况下，将未来的结果存放到某个位置，需要依赖future特性使用
    auto task_ptr = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
    //返回值
    std::future<return_type> res = task_ptr->get_future();
    {
        std::unique_lock<std::mutex> lock{queue_mutex_};
        if (stop_) throw std::runtime_error("Enqueue on stopped ThreadPool\n");
        //此处体现出为什么要创建一个函数指针形式的task_ptr，这样只需将可调用对象在内存中存一份，给此处的lambda
        //传入指针即可，降低内存消耗，shared_ptr用完自己释放资源，方便管理。(引用传递maybe也可以)
        tasks_.emplace([task_ptr] { (*task_ptr)(); });
    }
    cond_var_.notify_one();
    return res;
}

void ThreadPool::add_worker_() {
    workers_.emplace_back([this] {
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