#include <iostream>
#include "threadpool.h"
#include <chrono>
#include <thread>

class MyTask : public Task
{
public:
	MyTask(int begin, int end):begin_(begin), end_(end)
	{}
	Any run()
	{
		std::cout << "tid: " << std::this_thread::get_id() << "start!" << std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(3));
		int sum = 0;
		for (int i = begin_; i <= end_; i++) sum += i;
		std::cout << "tid: " << std::this_thread::get_id() << "end!" << std::endl;

		return sum;
	}
private:
	int begin_;
	int end_;
};

int main()
{
	ThreadPool testpool;

	// 设置模式
	testpool.setMode(PoolMode::MODE_CHACHED);

	testpool.start(4);

	Result res1 = testpool.submitTask(std::make_shared<MyTask>(1, 1000));
	Result res2 = testpool.submitTask(std::make_shared<MyTask>(1001, 2000));
	Result res3 = testpool.submitTask(std::make_shared<MyTask>(2001, 3000));
	Result res4 = testpool.submitTask(std::make_shared<MyTask>(3001, 4000));
	Result res5 = testpool.submitTask(std::make_shared<MyTask>(4001, 5000));
	Result res6 = testpool.submitTask(std::make_shared<MyTask>(5001, 6000));



	int i1 = res1.get().cast_<int>();
	int i2 = res2.get().cast_<int>();
	int i3 = res3.get().cast_<int>();

	std::cout << (i1 + i2 + i3) << std::endl;
	//testpool.submitTask(std::make_shared<MyTask>());
	//testpool.submitTask(std::make_shared<MyTask>());
	//testpool.submitTask(std::make_shared<MyTask>());
	//testpool.submitTask(std::make_shared<MyTask>());
	//testpool.submitTask(std::make_shared<MyTask>());
	//testpool.submitTask(std::make_shared<MyTask>());
	//testpool.submitTask(std::make_shared<MyTask>());
	int sum = 0;
	for (int i = 1; i <= 3000; i++) sum += i;
	std::cout << sum << std::endl;

	getchar();

}
