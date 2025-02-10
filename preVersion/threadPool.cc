#include "threadPool.hpp"

#include <iostream>
#include <thread>
#include <functional>

const int TASK_MAX_THRESHHOLD = INT32_MAX;
const int THREAD_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_IDLE_TIME = 60;

ThreadPool::ThreadPool()
	: initThreadSize_(0)
	, taskSize_(0)
	, idleThreadSize_(0)
	, curThreadSize_(0)
	, taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
	, threadSizeThreshHold_(THREAD_MAX_THRESHHOLD)
	, mode_(PoolMode::FIXED_MODE)
	, isPoolRunning_(false) {}

ThreadPool::~ThreadPool() {
	isPoolRunning_ = false;
	
	// 等待线程池里面所有的线程返回  有两种状态：阻塞 & 正在执行任务中
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	notEmpty_.notify_all();
	exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
}

void ThreadPool::setMode(PoolMode mode) {
	if (checkRunningState()) return;
	mode_ = mode;
}

void ThreadPool::setTaskQueMaxThreshHold(int threshhold) {
	if (checkRunningState()) return;
	taskQueMaxThreshHold_ = threshhold;
}

void ThreadPool::setThreadSizeThreshHold(int threshhold) {
	if (checkRunningState()) return;
	if (mode_ == PoolMode::CACHED_MODE) {
		threadSizeThreshHold_ = threshhold;
	}
}

Result ThreadPool::submitTask(std::shared_ptr<Task> sp) {
	// 获取锁
	std::unique_lock<std::mutex> lock(taskQueMtx_);

	// 用户提交任务，最长不能阻塞超过1s，否则判断提交任务失败，返回
	if (!notFull_.wait_for(lock, std::chrono::seconds(1),
		[&]()->bool { return taskQue_.size() < (size_t)taskQueMaxThreshHold_; })) {
		// 表示notFull_等待1s种，条件依然没有满足
		std::cerr << "task queue is full, submit task fail." << std::endl;
		return Result(sp, false);
	}

	// 如果有空余，把任务放入任务队列中
	taskQue_.emplace(sp);
	taskSize_++;

	// 因为新放了任务，任务队列肯定不空了，在notEmpty_上进行通知，赶快分配线程执行任务
	notEmpty_.notify_all();

	// cached模式 任务处理比较紧急 场景：小而快的任务 需要根据任务数量和空闲线程的数量，判断是否需要创建新的线程出来
	if (mode_ == PoolMode::CACHED_MODE
		&& taskSize_ > idleThreadSize_
		&& curThreadSize_ < threadSizeThreshHold_) {
		std::cout << ">>> create new thread..." << std::endl;

		// 创建新的线程对象
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
		// 启动线程
		threads_[threadId]->start(); 
		// 修改线程个数相关的变量
		curThreadSize_++;
		idleThreadSize_++;
	}

	// 返回任务的Result对象
	return Result(sp);
	// return task->getResult();
}

// 开启线程池
void ThreadPool::start(int initThreadSize) {
	// 设置线程池的运行状态
	isPoolRunning_ = true;

	// 记录初始线程个数
	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize;

	// 创建线程对象
	for (int i = 0; i < initThreadSize_; i++) {
		// 创建thread线程对象的时候，把线程函数给到thread线程对象
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
		// threads_.emplace_back(std::move(ptr));
	}

	// 启动所有线程  std::vector<Thread*> threads_;
	for (int i = 0; i < initThreadSize_; i++) {
		threads_[i]->start(); // 需要去执行一个线程函数
		idleThreadSize_++;    // 记录初始空闲线程的数量
	}
}

void ThreadPool::threadFunc(int threadid) {
	auto lastTime = std::chrono::high_resolution_clock().now();

	// 所有任务必须执行完成，线程池才可以回收所有线程资源
	while(true) {
		std::shared_ptr<Task> task;
		{
			// 先获取锁
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "tid:" << std::this_thread::get_id() << "trying to get task..." << std::endl;

			while (taskQue_.size() == 0) {
				// 线程池要结束，回收线程资源
				if (!isPoolRunning_) {
					threads_.erase(threadid); // std::this_thread::getid()
					std::cout << "threadid:" << std::this_thread::get_id() << " exit!"
						<< std::endl;
					exitCond_.notify_all();
					return; // 线程函数结束，线程结束
				}

				if (mode_ == PoolMode::CACHED_MODE) {
					// 条件变量，超时返回了
					if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1))) {
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() >= THREAD_MAX_IDLE_TIME && curThreadSize_ > initThreadSize_) {
							// 开始回收当前线程
							// 记录线程数量的相关变量的值修改
							// 把线程对象从线程列表容器中删除   没有办法 threadFunc《=》thread对象
							// threadid => thread对象 => 删除
							threads_.erase(threadid); // std::this_thread::getid()
							curThreadSize_--;
							idleThreadSize_--;

							std::cout << "threadid:" << std::this_thread::get_id() << " exit!"
								<< std::endl;
							return;
						}
					}
				}
				else {
					// 等待notEmpty条件
					notEmpty_.wait(lock);
				}
			}

			idleThreadSize_--;

			std::cout << "tid:" << std::this_thread::get_id()
				<< "get task successfully..." << std::endl;

			// 从任务队列种取一个任务出来
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			// 如果依然有剩余任务，继续通知其它得线程执行任务
			if (taskQue_.size() > 0) {
				notEmpty_.notify_all();
			}

			// 取出一个任务，进行通知，通知可以继续提交生产任务
			notFull_.notify_all();
		} // 就应该把锁释放掉
		
		// 当前线程负责执行这个任务
		if (task != nullptr) {
			// task->run(); // 执行任务；把任务的返回值setVal方法给到Result
			task->exec();
		}
		
		idleThreadSize_++;
		lastTime = std::chrono::high_resolution_clock().now(); // 更新线程执行完任务的时间
	}
}

bool ThreadPool::checkRunningState() const {
	return isPoolRunning_;
}

int Thread::generateId_ = 0;
Thread::Thread(ThreadFunc func) : func_(func), threadId_(generateId_++) {}

Thread::~Thread() {}

// 启动线程
void Thread::start() {
	std::thread t(func_, threadId_);
	t.detach(); // 设置分离线程   pthread_detach  pthread_t设置成分离线程
}

int Thread::getId() const {
	return threadId_;
}

Task::Task() : result_(nullptr) {}

void Task::exec() {
	if (result_ != nullptr) {
		result_->setVal(run()); // 这里发生多态调用
	}
}

void Task::setResult(Result* res) {
	result_ = res;
}

Result::Result(std::shared_ptr<Task> task, bool isValid) : isValid_(isValid), task_(task) {
	task_->setResult(this);
}

Any Result::get() { // 用户调用的
	if (!isValid_) return "";
	sem_.wait(); // task任务如果没有执行完，阻塞用户的线程
	return std::move(any_);
}

void Result::setVal(Any any) {
	// 存储task的返回值
	this->any_ = std::move(any);
	sem_.post(); // 已经获取的任务的返回值，增加信号量资源
}