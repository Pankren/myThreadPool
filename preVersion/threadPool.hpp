#pragma once

#include <memory>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <queue>
#include <unordered_map>
#include <vector>
#include <thread>

class Any { // 类型擦除（type erasure）将任意类型的值存储在一个同一的容器之中
public:
    Any() = default;
    ~Any() = default;
    Any(const Any&) = delete;
    Any& operator=(const Any&) = delete;
    Any(Any&&) = default;
    Any& operator=(Any&&) = default;

    template<typename T>
    Any(T data) : base_(std::make_unique<Derived<T>>(data)) {}

    template<typename T>
    T cast_() {
        if (base_ == nullptr) throw std::bad_cast("Any is empty!");

        Derived<T>* pd = dynamic_cast<Derived<T>*>(base_.get());
        if (pd == nullptr) throw std::bad_cast("type is unmatch!");
        return pd->data_;
    }
private:
    class Base { // 抽象基类
    public:
        virtual ~Base() = default;
    };

    template<typename T>
    class Derived : public Base { // 模板派生类，实际用于存储具体类型的数据
        Derived(T data) : data_(data) {}
        T data_;
    };
private:
    std::unique_ptr<Base> base_;
};
    
class Semaphore {
public:
    Semaphore(int limit = 0) : resLimit_(limit) {}
    ~Semaphore() = default;

    void wait() {
        std::unique_lock<std::mutex> lock(mtx_);
        cond_.wait(lock, [&]()->bool {return resLimit_ > 0;});
        resLimit_--;
    }

    void post() {
        std::unique_lock<std::mutex> lock(mtx_);
        resLimit_++;
        cond_.notify_all();
    }
private:
    int resLimit_;
    std::mutex mtx_;
    std::condition_variable cond_;
};

class Task;
class Result { // 任务执行结果
public:
    Result(std::shared_ptr<Task> task, bool isValid = true);
    ~Result() = default;
    void setVal(Any any);
    Any get(); 
private:
    Any any_;
    Semaphore sem_;
    std::shared_ptr<Task> task_;
    std::atomic_bool isValid_;
};

class Task { // 任务抽象基类
public:
    Task();
    ~Task() = default;
    void exec();
    void setResult(Result* res);
    virtual Any run() = 0;
private:
    Result* result_;
};

enum class PoolMode {
    FIXED_MODE,
    CACHED_MODE
};

class Thread {
public:
    using ThreadFunc = std::function<void(int)>;
    Thread(ThreadFunc func);
    ~Thread();
    void start();
    int getId() const;
private:
    ThreadFunc func_;
    static int generateId_;
    int threadId_;
};

class ThreadPool {
public:
    ThreadPool();
    ~ThreadPool();
    void setMode(PoolMode mode);
    void setTaskQueMaxThreshHold(int size);
    void setThreadSizeThreshHold(int size);

    Result submitTask(std::shared_ptr<Task> task);

    void start(int initThreadSize = std::thread::hardware_concurrency());

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
private:
    void threadFunc(int threadid);
    bool checkRunningState() const;
private:
    std::unordered_map<int, std::unique_ptr<Thread>> threads_;

    int initThreadSize_;
    int threadSizeThreshHold_;
    std::atomic<int> curThreadSize_;
    std::atomic<int> idleThreadSize_;

    std::queue<std::shared_ptr<Task>> taskQue_;
    std::atomic<int> taskSize_;
    int taskQueMaxThreshHold_;

    std::mutex taskQueMtx_;
    std::condition_variable notFull_;
    std::condition_variable notEmpty_;
    std::condition_variable exitCond_;

    PoolMode mode_;
    std::atomic<bool> isPoolRunning_;
};