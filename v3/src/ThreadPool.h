//v3 在v2的基础上添加一个管理线程，根据任务数量和工作线程数量的比值，动态调整线程池大小
//为实现缩容，增加了一个私有变量need_remove_worker_作为标志位，若某个工作线程收到了该
//信号，并发现自己的id不在workers_中，则将自己退出，从而真正减少工作线程的数量。

//github上看别人的c++实现，在做缩容时，只是将workers_的back()执行detach，然后就让workers_
//pop_back。但这样的话detach出去的孤儿线程依旧能通过捕获到的this指针获取到主线程的资源，
//还是可以作为工作线程处理task，它的生命周期还是以stop_作为终止信号。而缩容的场景就是工作线程
//过剩，所以这样做并没有真正减少工作线程数，只是自以为将其从workers_中清理出去就完事了，掩耳盗铃。

//也有可能是我自己理解不到位，别人的做法没问题？如果想验证的话，可以检查线程id是否仍在运行
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
    explicit ThreadPool(std::size_t, std::size_t);
    ~ThreadPool();
    template<typename F, typename... Args>
    auto enqueue(F&&, Args&& ...) -> std::future<typename std::result_of<F(Args...)>::type>;
private:
    void add_worker_();
    void remove_worker_();
    bool stop_;
    bool need_remove_worker_;
    std::queue<Task> tasks_;
    std::vector<std::thread> workers_;
    std::mutex queue_mutex_;
    std::condition_variable cond_var_;
    std::thread manager_thread_;
};

inline ThreadPool::ThreadPool(std::size_t min_threads_num, std::size_t max_threads_num)
    :   stop_(false),
        need_remove_worker_(false) {
    for (std::size_t i = 0; i < min_threads_num; ++i) {
        add_worker_();
    }
    //设置管理线程
    manager_thread_ = std::thread([this, min_threads_num, max_threads_num] {
        while (!stop_) {
            std::unique_lock<std::mutex> lock{queue_mutex_};
            //管理线程休眠，每隔1s尝试获取一次锁，或收到notify直接获得锁
            cond_var_.wait_for(lock, std::chrono::seconds(1));
            //醒来若发现要stop_，说明析构流程已触发，则直接return
            if (stop_) return;
            auto workers_num = workers_.size();
            auto tasks_num = tasks_.size();
            //根据worker和task的数量比值，来决定是否增减worker
            if (workers_num < max_threads_num && workers_num * 2 < tasks_num)
                add_worker_();
            else if (workers_num > min_threads_num && tasks_num * 2 < workers_num)
                remove_worker_();
            printf("Worker number changed to %lu\n", workers_.size());
        }
    });
}

inline ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock{queue_mutex_};
        stop_ = true;
    }
    cond_var_.notify_all();
    manager_thread_.join();
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
        //传入指针即可，降低内存消耗，shared_ptr用完自己释放资源，方便管理
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
                bool remove_current_thread = true;
                //遍历workers_中的线程，从而判断this_thread是否还在workers_中，不在则说明this_thread就是需要退出的线程，即在
                //remove_()中pop_back()出去的thread对象曾经管理的线程，让其return掉。
                //这个做法存在效率问题，每个工作线程被唤醒时都需要做这个遍历操作，但又不必过于担心，理由如下：
                //CPU密集型程序 最佳工作线程数 = CPU核数(逻辑) + 1
                //I/O密集型程序 最佳工作线程数 = (1/CPU利用率) = 1 + (I/O耗时/CPU耗时) 理论最佳值会设为 2 * CPU核数(逻辑) + 1
                //受CPU核数的限制，线程池不宜开的太大，故而遍历workers_耗时不大
                for (const std::thread& current_thread : workers_) {
                    if (std::this_thread::get_id() == current_thread.get_id()) {
                        remove_current_thread = false;
                        break;
                    }
                }
                if (need_remove_worker_ && remove_current_thread) {
                    need_remove_worker_ = false; //一次只移除一个worker，故而标志位复位
                    return;
                }
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    });
}

void ThreadPool::remove_worker_() {
    //此处不能再加锁，调用处已经有了unique_lock，再次加锁会崩溃
    need_remove_worker_ = true;
    workers_.back().detach();
    workers_.pop_back();
    cond_var_.notify_all();
}