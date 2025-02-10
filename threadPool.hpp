#pragma once

#include <iostream>
#include <vector>
#include <queue>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>
#include <condition_variable>
#include <thread>
#include <unordered_map>
#include <future>

using namespace std::placeholders;

const int TASK_MAX_THRESHHOLD = INT32_MAX;
const int THREAD_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_IDLE_TIME = 60;

enum class PoolMode {
    FIXED_MODE,
    CACHED_MODE,
};

class Thread {
public:
    using ThreadFunc = std::function<void(int)>;

    Thread(ThreadFunc func) : func_(func), threadId_(generateId_++) {}
    ~Thread() = default;

    void start() {
        std::thread t(func_, threadId_);
        t.detach();
    }

    int getId() const {
        return threadId_;
    }
private:
    ThreadFunc func_;
    static int generateId_;
    int threadId_;
};
int Thread::generateId_ = 0;

class ThreadPool {
public:
    ThreadPool()
        : initThreadSize_(0)
        , threadSizeThreshHold_(THREAD_MAX_THRESHHOLD)
        , curThreadSize_(0)
        , idleThreadSize_(0)
        // , taskQue_()
        , taskSize_(0)
        , taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
        , mode_(PoolMode::FIXED_MODE)
        , isPoolRunning_(false) {}

    ~ThreadPool() {
        isPoolRunning_ = false;
        
        std::unique_lock<std::mutex> lock(taskQueMtx_);
        notEmpty_.notify_all();
        exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0;});
    }

    // 线程池对象不允许拷贝构造和赋值
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    void setMode(PoolMode mode) {
        if(checkRunningState()) return; // can be change mode only the pool isn't running
        mode_ = mode;
    }

    void setTaskQueMaxThreshHold(int threshhold) {
        if(checkRunningState()) return;
        taskQueMaxThreshHold_ = threshhold;
    }

    void setThreadSizeThreshHold(int threshhold) {
        if(checkRunningState()) return;
        if(mode_ == PoolMode::CACHED_MODE) {
            threadSizeThreshHold_ = threshhold;
        }
    }

    /*
    std::future std::package_task decltype 以及 可变参模板函数
    */
    template<typename Func, typename... Args>
    auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))> {
        using rType = decltype(func(args...));
        auto task = std::make_shared<std::packaged_task<rType()>>(
            std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
        std::future<rType> result = task->get_future();

        std::unique_lock<std::mutex> lock(taskQueMtx_); // 获取锁
        // 如果用户提交任务阻塞超过一秒，判断提交任务失败并返回
        if(!notFull_.wait_for(lock, std::chrono::seconds(1), [&]()->bool {
            return taskQue_.size() < (size_t)taskQueMaxThreshHold_;
        })) {
            std::cerr << "task queue is full, submit task fail!" << std::endl;
            auto task = std::make_shared<std::packaged_task<rType()>>([]()->bool {
                return rType();
            });
            (*task)();
            return task->get_future();
        }

        taskQue_.emplace([task]() {(*task)();});
        taskSize_++;

        notEmpty_.notify_all();

        if(mode_ == PoolMode::CACHED_MODE && taskSize_ > idleThreadSize_ && curThreadSize_ < threadSizeThreshHold_) {
            std::cout << ">>>create new thread>>>" << std::endl;

            auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, _1));
            int threadId = ptr->getId();
            threads_.emplace(threadId, std::move(ptr));
            threads_[threadId]->start();
            curThreadSize_++;
            idleThreadSize_++;
        }

        return result;
    }

    void start(int initThreadSize = std::thread::hardware_concurrency()) {
        isPoolRunning_ = true;
        initThreadSize_ = initThreadSize;
        idleThreadSize_ = initThreadSize;

        for(int i = 0; i < initThreadSize_; i++) { // 创建线程
            auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, _1));
            int threadId = ptr->getId();
            threads_.emplace(threadId, std::move(ptr));
        }

        for(int i = 0; i < initThreadSize_; i++) { // 启动线程
            threads_[i]->start();
            idleThreadSize_++;
        }
    }
private:
    void threadFunc(int threadid) {
        auto lastTime = std::chrono::high_resolution_clock().now();

        while(true) {
            Task task;
            {
                std::unique_lock<std::mutex> lock(taskQueMtx_);

                std::cout << "tid: " << std::this_thread::get_id() << "trying get task..." << std::endl;

                while (taskQue_.size() == 0) {
                    if(!isPoolRunning_) {
                        threads_.erase(threadid);
                        std::cout << "threadId: " << std::this_thread::get_id << " exit!" << std::endl;
                        exitCond_.notify_all();
                        return;
                    }
                    if(mode_ == PoolMode::CACHED_MODE) {
                        if(std::cv_status::timeout == 
                            notEmpty_.wait_for(lock, std::chrono::seconds(1))) {
                            auto now = std::chrono::high_resolution_clock().now();
                            auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
                            if(dur.count() >= THREAD_MAX_IDLE_TIME && curThreadSize_ > initThreadSize_) {
                                threads_.erase(threadid);
                                curThreadSize_--;
                                idleThreadSize_--;

                                std::cout << "threadId: " << std::this_thread::get_id() << " exit!" << std::endl;
                                return;
                            }
                        }
                    }
                    else {
                        notEmpty_.wait(lock);
                    }
                }
                idleThreadSize_--;
                std::cout << "tid: " << std::this_thread::get_id() << "get task successfully!" << std::endl;
                task = taskQue_.front();
                taskQue_.pop();
                taskSize_--;

                if(taskQue_.size() > 0) {
                    notEmpty_.notify_all();
                }

                notFull_.notify_all();
            } // 作用域

            if(task != nullptr) {
                task();
            }

            idleThreadSize_++;
            lastTime = std::chrono::high_resolution_clock().now();
        }
    }

    bool checkRunningState() const {
        return isPoolRunning_;
    }
private:
    std::unordered_map<int, std::unique_ptr<Thread>> threads_;

    int initThreadSize_;
    int threadSizeThreshHold_;
    std::atomic<int> curThreadSize_;
    std::atomic<int> idleThreadSize_;

    using Task = std::function<void()>;
    std::queue<Task> taskQue_;
    std::atomic<int> taskSize_;
    int taskQueMaxThreshHold_;

    std::mutex taskQueMtx_;
    std::condition_variable notFull_;
    std::condition_variable notEmpty_;
    std::condition_variable exitCond_;

    PoolMode mode_;
    std::atomic<bool> isPoolRunning_;
};