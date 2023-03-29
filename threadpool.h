#ifndef _THREAD_POOL_H_
#define _THREAD_POOL_H_

#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <unordered_map>

// Any类型：可以接收任意数据类型
class Any
{
public:
	Any() = default;
	~Any() = default;
	// 因为内部数据是unique_prt 禁用左值引用拷贝构造和赋值
	Any(const Any&) = delete;
	Any& operator=(const Any&) = delete;

	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	// 让Any类型接收任意其他数据
	template<typename T>
	Any(T data):base_(std::make_unique<Derive<T>>(data)){}

	// 能把Any对象中存储的data数据提取出来
	template<typename T>
	T cast_()
	{
		// 从base中拿到所指向的Derive对象，取出data
		// 基类指针转成派生类指针
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());
		// 判断转换是否成功
		if (pd == nullptr)
		{
			throw "type is not expect! check return type.";
		}
		return pd->data_;
	}

private:
	// 基类类型
	class Base
	{
	public:
		virtual ~Base() = default;
	};

	// 派生类
	template<typename T>
	class Derive : public Base
	{
	public:
		Derive(T data): data_(data){}
		T data_;
	};

private:
	// 定义一个基类指针
	std::unique_ptr<Base> base_;
};


// 实现信号量类
class Semaphore
{
public:
	Semaphore(int limit = 0)
		:resLimit_(limit){}
	~Semaphore() = default;

	// 获取一个信号量资源
	void wait()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		
		// 等待信号量有资源，没有资源会阻塞当前进程
		cond_.wait(lock, [&]()->bool {return resLimit_ > 0; });
		// 消耗一个信号量资源
		resLimit_--;
	}

	// 增加一个信号量资源
	void post()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		resLimit_++;

		// 通知已增加资源
		cond_.notify_all();
	}
private:
	// 资源计数
	int resLimit_;
	std::mutex mtx_;
	std::condition_variable cond_;
};


// 提前声明
class Task;

// 实现接收任务提交到线程池的task执行完成后的返回值类型Result
class Result
{
public:
	Result(std::shared_ptr<Task> task, bool isValid = true );
	~Result() = default;

	// 获取任务执行完的返回值
	// 
	void setVal(Any any);

	// 用户调用后获取task的返回值
	Any get();
private:
	// 存储任务的返回值
	Any any_;

	// 线程通信信号量
	Semaphore sem_;

	// 指向将来去获取结果的任务对象，防止任务对象提取析构
	std::shared_ptr<Task> task_;

	// 返回值是否有效 任务提交失败时返回值是无效的
	std::atomic_bool isValid_;
};

// 任务的抽象基类
// 将自定义任务从抽象基类中派生，重写run方法
class Task
{
public:
	Task();
	~Task() = default;
	void exec();
	void setResult(Result* res);
	virtual Any run() = 0; // 定义纯虚函数，让派生类对其重定义
private:

	// 不能用智能指针 result中存的task已经是智能指针
	// result的生命周期长于task
	Result* result_;
};


// 定长/可动态增长模式
enum class PoolMode {MODE_FIXED, MODE_CHACHED,};

// 线程类
class Thread
{
public:
	// 线程函数对象类型
	using ThreadFunc = std::function<void(int)>;
	
	// 构造函数
	Thread(ThreadFunc func);

	// 析构
	~Thread();

	// 启动线程
	void start();

	// 获取线程id
	int getId() const;
private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_;
};


/*
* ThreadPool pool;
* pool.start(n);
* 
* class MyTask : public Task
* {
* public:
*		Any run(){...}
* };
* 
* pool.submitTask(std::make_shared<MyTask>());
*/

// 线程池类
// fix模式不用考虑队列的线程安全
class ThreadPool
{
public:
	ThreadPool();
	~ThreadPool();

	// 禁止拷贝线程池
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

	// 设置模式
	void setMode(PoolMode mode);

	// 设置cache模式下线程阈值
	void setThreadSizeThreshHold(int maxThreshold);

	// 设置任务队列最大值
	void setTaskQueMaxThreshHold(int maxThreshold);

	// 开启线程池
	void start(int initThreadSize = std::thread::hardware_concurrency());

	// 提交任务
	Result submitTask(std::shared_ptr<Task> ptask);

private:
	// 定义线程函数
	void threadFunc(int threadid);

	// 检查pool的运行状态
	bool checkRunningState() const;

private:
	// 在start中将ThreadPool中的threadFunc绑定到了new出来的Thread对象
	// 这里使用unique_ptr让对象自动析构
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;// 线程列表
	size_t initThreadSize_; // 初始线程数量（fixed）
	unsigned int threadSizeThreshHold_;// 线程数量上限阈值
	std::atomic_uint idleThreadSize_; // 空闲线程的数量
	std::atomic_uint curThreadSize_;// 当前线程池里面线程的总数量
	
	std::queue<std::shared_ptr<Task>> taskQue_; // 任务队列
	std::atomic_uint taskSize_; // 任务数量 需要线程安全->atomic
	unsigned int taskQueMaxThrshHold_; // 任务数量最大值

	std::mutex taskQueMtx_; // 保证任务队列线程安全
	std::condition_variable notFull_; // 任务队列不满
	std::condition_variable notEmpty_; // 任务队列不空
	std::condition_variable exitCond_; // 等待线程资源全部回收

	PoolMode poolMode_;
	std::atomic_bool isPoolRunning_; // 当前线程池启动状态


};

#endif
