﻿#include "threadpool.h"
#include <functional>
#include <thread>
#include <iostream>

const int TASK_MAX_THRESHHOLD = INT32_MAX;
const int THREAD_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_IDLE_TIME = 60;

//线程池构造
ThreadPool::ThreadPool()
	:initThreadSize_(0)
	, taskSize_(0)
	, idleThreadSize_(0)
	, taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
	, threadSizeThreshHold_(300)
	, curThreadSize_(0)
	, poolMode_(PoolMode::MODE_FIXED)
	, isPoolRunning_(false)
{}


//线程池析构
ThreadPool::~ThreadPool()
{
	isPoolRunning_ = false;
	notEmpty_.notify_all();

	//等待线程池里所有的线程返回
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
}

//设置线程池的工作模式
void ThreadPool::setMode(PoolMode mode) 
{
	if (checkRunningState())
		return;
	poolMode_ = mode;
}

bool ThreadPool::checkRunningState() const
{
	return isPoolRunning_;
}

//设置初始的线程数量
//void ThreadPool::setInitThreadSize(int size) 
//{
//	initThreadSize_ = size;
//}

//设置task任务队列上限阈值
void ThreadPool::setTaskQueMaxThreshHold(int threshhold) 
{
	if (checkRunningState())
		return;
	taskQueMaxThreshHold_ = threshhold;
}

//设置thread阈值
void ThreadPool::setThreadThreshHold(int threshhold)
{
	if (checkRunningState())
		return;
	if (poolMode_ == PoolMode::MODE_CACHED)
		threadSizeThreshHold_ = threshhold;
}

//给线程池提交任务
Result ThreadPool::submitTask(std::shared_ptr<Task> sp) 
{
	//获取锁
	std::unique_lock<std::mutex> lock(taskQueMtx_);

	//线程的通信	等待任务队列有空余
	//while (taskQue_.size() == taskQueMaxThreshHold_)
	//{
	//	notFull_.wait(lock);
	//}
	//不能一直阻塞在这，阻塞超过设定时间(1s)则判断提交任务失败
	if (!notFull_.wait_for(lock, std::chrono::seconds(1),
		[&]()->bool {return taskQue_.size() < taskQueMaxThreshHold_; }))
	{
		//表示如果notfull等待超时了，条件仍然没有满足
		std::cerr << "task queue is full, submit task fail." << std::endl;
		//return task->getResult();
		return Result(sp, false);
	}
	
	//如果有空余，将任务放入任务队列中
	taskQue_.emplace(sp);
	taskSize_++;

	//任务队列非空，在notEmpty_上进行通知
	notEmpty_.notify_all();

	//cached模式，很多小而快的任务，根据当前任务数量和空闲线程数量，判断是否需要新创建
	if (poolMode_ == PoolMode::MODE_CACHED 
		&& taskSize_ > idleThreadSize_ 
		&& curThreadSize_ < threadSizeThreshHold_)
	{
		std::cout << ">>> create new thread... " << std::this_thread::get_id() << "exit!" << std::endl;
		//创建新线程
		std::unique_ptr<Thread> ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
		threads_[threadId]->start();	//创建好后启动

		//修改线程个数相关的变量
		curThreadSize_++;
		idleThreadSize_++;
	}

	//返回任务的Result对象
	return Result(sp, true);
}

//开启线程池
void ThreadPool::start(int initThreadSize) 
{
	isPoolRunning_ = true;

	//记录初始线程个数
	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize;

	//创建线程对象, 先集体创建再启动避免问题
	for (int i = 0; i < initThreadSize_; i++) 
	{
		//创建thread线程对象的时候，把线程函数给到thread线程对象
		std::unique_ptr<Thread> ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
		//threads_.emplace_back(ptr);的写法是错误的，因为这里会隐含一个拷贝构造函数，但是unique_ptr是不允许拷贝构造的
	}

	//启动所有线程
	for (int i = 0; i < initThreadSize_; i++)
	{
		threads_[i]->start();
		idleThreadSize_++;	//记录初始空闲线程的数量
	}
}

//定义线程函数	线程池的所有线程从任务队列里面消费任务
void ThreadPool::threadFunc(int threadid)
{
	auto lastTime = std::chrono::high_resolution_clock().now();

	while(isPoolRunning_)
	{
		std::shared_ptr<Task> task;
		{
			//先获取锁
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "tid:" << std::this_thread::get_id()
				<< "尝试获取任务…" << std::endl;

			//空闲超过60s，回收多余线程
			//超过initThreadSize_数量的线程要进行回收
			while (taskQue_.size() == 0)
			{
				if (poolMode_ == PoolMode::MODE_CACHED)
				{
					//条件变量超时返回了
					if (std::cv_status::timeout ==
						notEmpty_.wait_for(lock, std::chrono::seconds(1)))
					{
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() >= THREAD_MAX_IDLE_TIME
							&& curThreadSize_ > initThreadSize_)
						{
							//回收当前线程
							threads_.erase(threadid);	//这个id与系统给线程设置的id不同
							curThreadSize_--;
							idleThreadSize_--;

							std::cout << "threadid:" << std::this_thread::get_id() << "exit!" << std::endl;
							return;
						}
					}
				}

				else
				{
					//等待notEmpty条件
					notEmpty_.wait(lock);
				}

				//线程池要结束，回收线程资源
				if (!isPoolRunning_)
				{
					threads_.erase(threadid);	//这个id与系统给线程设置的id不同
					//curThreadSize_--;
					//idleThreadSize_--;

					std::cout << "threadid:" << std::this_thread::get_id() << "exit!" << std::endl;
					exitCond_.notify_all();
					return;
				}
			}


			idleThreadSize_--;

			std::cout << "tid:" << std::this_thread::get_id()
				<< "获取任务成功" << std::endl;

			//取任务
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			//如果依然有剩余任务，通知其他线程执行任务
			if (taskQue_.size() > 0)
			{
				notEmpty_.notify_all();
			}

			//取出一个任务，通知
			notFull_.notify_all();
		}//出了局部作用域，自动释放锁
		
		//当前线程负责执行这个任务
		if (task != nullptr)
		{
			//task->run();
			//执行完任务需要通知
			task->exec();
		}
		lastTime = std::chrono::high_resolution_clock().now();	//更新线程执行完任务的时间
		idleThreadSize_++;
	}

	threads_.erase(threadid);	
	//curThreadSize_--;
	//idleThreadSize_--;

	std::cout << "threadid:" << std::this_thread::get_id() << "exit!" 
		<< std::endl;	
	exitCond_.notify_all();
}


/*---------------线程方法实现----------------*/
int Thread::generateId_ = 0;

//线程构造
Thread::Thread(ThreadFunc func)
	:func_(func)
	,threadId_(generateId_++)
{}

// 线程析构
Thread::~Thread(){}

//获取线程id
int Thread::getId() const
{
	return threadId_;
}

//启动线程
void Thread::start() 
{
	//创建一个线程来执行一个线程函数
	std::thread t(func_, threadId_);	//线程对象t 和线程函数func_
	t.detach();	//设置分离线程，让t和func_不会绑定。start函数执行结束后，t的执行周期已经结束，而func还是存在的
}




/*---------------Task方法实现----------------*/
Task::Task()
	:result_(nullptr)
{}

void Task::exec()
{
	if(result_!=nullptr)
		result_->setValue(run());	//多态调用
}

void Task::setResult(Result* res)
{
	result_ = res;
}




/*---------------Result方法实现----------------*/
Result::Result(std::shared_ptr<Task> task, bool isValid)
	:task_(task)
	, isValid_(isValid)
{
	task_->setResult(this);
}

Any Result::get()	//用户调用
{
	if (!isValid_)
	{
		return " ";
	}

	sem_.wait();	//task任务如果没有执行完，这里会阻塞用户的线程
	return std::move(any_);
}

void Result::setValue(Any any)
{
	//存储task的返回值
	this->any_ = std::move(any);
	sem_.post();	//已经获取的任务的返回值，增加信号量资源

}