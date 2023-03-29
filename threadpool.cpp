#include "threadpool.h"

#include <functional>
#include <thread>
#include <iostream>
#include <mutex>
#include <condition_variable>


const int TASK_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_THRESHHOLD = 10;
const int THREAD_MAX_IDLE_TIME = 60;
// 构造
ThreadPool::ThreadPool()
	:initThreadSize_(0),
	 taskSize_(0),
	 taskQueMaxThrshHold_(TASK_MAX_THRESHHOLD),
	 poolMode_(PoolMode::MODE_FIXED),
	 isPoolRunning_(false),
	 idleThreadSize_(0),
	 threadSizeThreshHold_(THREAD_MAX_THRESHHOLD),
	 curThreadSize_(0)
{

}

// 析构
ThreadPool::~ThreadPool()
{
	isPoolRunning_ = false;

	// 等待所有线程返回
	std::unique_lock<std::mutex> lock(taskQueMtx_);

	// 唤醒所有等待在任务队列的线程
	notEmpty_.notify_all();

	// 等待所有线程退出
	exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
}

// 设置模式
void ThreadPool::setMode(PoolMode mode)
{
	if (checkRunningState()) return;
	poolMode_ = mode;
}

void ThreadPool::setThreadSizeThreshHold(int maxThreshold)
{
	if (checkRunningState()) return;
	if (poolMode_ == PoolMode::MODE_CHACHED)
	{
		threadSizeThreshHold_ = maxThreshold;
	}
}

// 设置任务队列最大值
void ThreadPool::setTaskQueMaxThreshHold(int maxThreshold)
{
	taskQueMaxThrshHold_ = maxThreshold;
}

// 开启线程池
void ThreadPool::start(int initThreadSize)
{
	// 变更状态
	isPoolRunning_ = true;

	// 设置初始线程个数
	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize;
	
	// 创建线程对象（先集中创建，再启动）
	for (int i = 0; i < initThreadSize_; i++)
	{
		// 创建thread对象时把线程函数给thread对象
		std::unique_ptr<Thread> ptr = 
			std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
	}
	for (int i = 0; i < initThreadSize_; i++)
	{
		threads_[i]->start();
		idleThreadSize_++; // 记录初始空闲线程数量
	}
}

// 提交任务
// 生产者 生产任务
Result ThreadPool::submitTask(std::shared_ptr<Task> ptask)
{
	// 获取锁
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	// 线程通信 等待任务队列有空余，
	//while (taskque_.size() == taskquemaxthrshhold_){notfull_.wait(lock);}
	// 用户提交任务，最长不能阻塞超过一秒，否则判断任务失败，返回 wait wait_for wait_until
	if (!notFull_.wait_for(lock, std::chrono::seconds(1),
		[&]()->bool {return taskQue_.size() < taskQueMaxThrshHold_; }))
	{
		// 等待一秒任务队列依旧无法放入任务（满）
		std::cerr << "task queue if full, submit task fail." << std::endl;
		return Result(ptask,  false);
	}
	// 有空余，将任务放入队列
	taskQue_.emplace(ptask);
	taskSize_++;

	// 放入任务后，任务队列不空，通知消费者notEmpty_
	notEmpty_.notify_all();

	// cached模式 根据任务数量和空闲线程数量判断是否需要创建新的线程
	if (poolMode_ == PoolMode::MODE_CHACHED
		&& taskSize_ > idleThreadSize_
		&& curThreadSize_ < threadSizeThreshHold_)
	{
		std::cout << "creat new thread" << std::endl;
		// 创建新线程
		std::unique_ptr<Thread> ptr = 
			std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
		// 启动线程
		threads_[threadId]->start();
		curThreadSize_++;
		idleThreadSize_++;
	}

	// 返回任务的Result对象
	return Result(ptask);
}

// 定义线程函数 
// 消费者，从任务队列中消费任务
void ThreadPool::threadFunc(int threadid)
{
	auto lastTime = std::chrono::high_resolution_clock().now();

	// 所有任务必须执行完成才能回收资源

	// 不断循环从任务队列取任务
	for (;;)
	{
		std::shared_ptr<Task> task;
		// 定义代码段自动释放锁
		{
			// 获取锁
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "tid: " << std::this_thread::get_id() << "消费者尝试获取任务..." << std::endl;

			// cached模式下，有可能已经创建了其他线程，如果线程空闲时间超过60s，将多余线程回收掉
			// 结束回收掉（超过initThreadSize_数量的线程要回收）
			// 当前时间 - 上一次线程执行时间 > 60s
			
			// 每一秒钟返回一次	区分超时返回/有任务待执行返回
			// 锁 + 双重判断
			while (taskQue_.size() == 0)
			{
				if (!isPoolRunning_)
				{
					threads_.erase(threadid);
					std::cout << "threadid:" << std::this_thread::get_id() << "exit" << std::endl;
					exitCond_.notify_all();
					return;
				}
				if (poolMode_ == PoolMode::MODE_CHACHED)
				{
					// 超时返回
					if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1)))
					{
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() >= THREAD_MAX_IDLE_TIME
							&& curThreadSize_ > initThreadSize_)
						{
							// 回收当前线程
							// 记录线程数量相关变量值更新
							// 从线程列表中去除线程对象 到底要删哪个线程？
							// threadid -> thread -> delete
							threads_.erase(threadid);
							curThreadSize_--;
							idleThreadSize_--;

							std::cout << "threadid:" << std::this_thread::get_id() << "exit" << std::endl;
							return;
						}
					}
				}
				else
				{
					// 等待notempty
					notEmpty_.wait(lock);
				}

				//// 线程池结束回收资源
				//if (!isPoolRunning_)
				//{
				//	threads_.erase(threadid);
				//	std::cout << "threadid:" << std::this_thread::get_id() << "exit" << std::endl;
				//	exitCond_.notify_all();
				//	return;
				//}
			}


			idleThreadSize_--;

			std::cout << "tid: " << std::this_thread::get_id() << "获取任务成功！" << std::endl;

			// 从任务队列取任务
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			// 如果有剩余任务，继续通知其他线程取任务
			if (taskQue_.size() > 0) 
			{
				notEmpty_.notify_all();
			}

			// 取出任务后通知生产者
			notFull_.notify_all();
		}
		if (task != nullptr)
		{
			// 执行任务
			task->exec();
			// 返回任务的返回值给Result

		}
		
		idleThreadSize_++;
		// 更新线程执行完任务的时间
		lastTime = std::chrono::high_resolution_clock().now();
	}

	//threads_.erase(threadid);
	//std::cout << "threadid:" << std::this_thread::get_id() << "exit" << std::endl;
	//exitCond_.notify_all();
}

bool ThreadPool::checkRunningState() const
{
	return isPoolRunning_;
}




int Thread::generateId_ = 0;

// 构造
Thread::Thread(ThreadFunc func)
	:func_(func),
	 threadId_(generateId_++)
{

}

// 析构
Thread::~Thread()
{
}

// 启动线程
void Thread::start()
{
	// 创建一个线程，执行线程函数
	std::thread t(func_, threadId_);
	t.detach(); // 设置分离线程，将子线程和主线程的关联分离，防止线程对象作用域周期结束后自动析构
}

int Thread::getId() const
{
	return threadId_;
}




// Result构造
Result::Result(std::shared_ptr<Task> task, bool isValid)
	: isValid_(isValid), task_(task)
{
	task->setResult(this);
}

// thread
void Result::setVal(Any any)
{
	// 先存储task的返回值
	this->any_ = std::move(any);
	// 获取了返回值
	sem_.post();
}

Any Result::get()
{
	if (!isValid_)
	{
		return "";
	}

	// 等待任务执行完
	sem_.wait();
	return std::move(any_);
}






Task::Task():result_(nullptr){}

void Task::exec()
{
	if (result_ != nullptr)
	{
		// 此处发生多态调用
		result_->setVal(run());
	}
}

void Task::setResult(Result* res)
{
	result_ = res;
}
