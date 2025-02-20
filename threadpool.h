#pragma once
#ifndef THREADPOOL_H
#define TNREADPOOL_H

#include<vector>
#include<queue>
#include<memory>
#include<atomic>
#include<mutex>
#include<condition_variable>
#include<functional>
#include<unordered_map>


//any类型，可以接受任意数据
class Any
{
public:
	Any() = default;
	~Any() = default;
	Any(const Any&) = delete;	//禁用拷贝构造函数
	Any& operator=(const Any&) = delete;
	Any(Any&&) = default;	//允许右值引用、移动语义
	Any& operator=(Any&&) = default;

	//构造函数旨在接收各种类型的数据
	template<typename T> 
	Any(T data) : base_(std::make_unique<Derive<T>>(data))
	{}

	//提取接收到的数据data
	template<typename T>
	T cast_()
	{
		//从基类中找到其所指的派生类对象，从它里面取出,基类转派生类
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());	//转换失败则为空
		if (pd == nullptr)
		{
			throw "type is unmatch!";
		}
		return pd->data_;
	}
private:
	//基类类型
	class Base
	{
	public:
		virtual ~Base() = default;
	};

	//派生类类型
	template<typename T>
	class Derive :public Base
	{
	public:
		Derive(T data):data_(data)
		{}
		T data_;	//保存了任意的其他类型
	};

private:
	//定义一个基类指针
	std::unique_ptr<Base> base_;
};


//实现一个信号量类
class Semaphore
{
public:
	Semaphore(int limit = 0)
		:resLimit_(limit)
	{}
	~Semaphore() = default;

	//获取一个信号量资源
	void wait()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		//等待信号量有资源，没有资源，则阻塞当前线程
		cond_.wait(lock, [&]()->bool {return resLimit_ > 0; });
		resLimit_--;
	}

	//增加一个信号量资源
	void post()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		resLimit_++;
		//通知
		cond_.notify_all();
	}
private:
	//资源计数
	int resLimit_;

	std::mutex mtx_;
	std::condition_variable cond_;
};

class Task;
//实现接受线程池的task任务执行完成后的返回值类型
class Result
{
public:
	Result(std::shared_ptr<Task> task, bool isValid=true);
	~Result() = default;

	//setVal方法，获取任务执行完的返回值
	void setValue(Any);

	//get方法，用户调用这个方法获取task的返回值
	Any get();
private:
	Any any_;	//存储任务的返回值
	Semaphore sem_;	//线程通信信号量
	std::shared_ptr<Task> task_;	//指向获取返回值的任务对象，让它不要出了局部作用域就被释放
	std::atomic_bool isValid_;	//判断返回值是否有效
};

//线程池支持的模式
enum PoolMode
{
	MODE_FIXED,		//固定数量的线程
	MODE_CACHED,	//线程数量可动态增长
};

//任务抽象基类
class Task 
{
public:
	Task();
	~Task() = default;

	void exec();
	void setResult(Result* res);
	//用户可以自定义任意任务类型，继承类重写run方法，实现自定义任务处理
	virtual Any run() = 0;

private:
	Result* result_;
};

//线程类型
class Thread 
{
public:
	using ThreadFunc = std::function<void(int)>;

	//线程构造
	Thread(ThreadFunc func);
	
	// 线程析构
	~Thread();

	//启动线程
	void start();

	//获取线程id
	int getId() const;

private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_;	//保存线程id
};

/*
example:
ThreadPool pool;
pool.start(4);

class MyTask:public Task{
public:
	void run(){}
};

pool.submitTask(std::make_shared<MyTask<Thread>>);
*/

//线程池类型
class ThreadPool
{
public:
	//目前的默认构造函数
	ThreadPool();
	~ThreadPool();

	//设置线程池的工作模式
	void setMode(PoolMode mode);


	//设置初始的线程数量
	//void setInitThreadSize(int size);

	//设置task任务队列上限阈值
	void setTaskQueMaxThreshHold(int threshhold);

	//设置线程池cached模式下线程阈值
	void setThreadThreshHold(int threshhold);

	//给线程池提交任务
	Result submitTask(std::shared_ptr<Task> sp);

	//开启线程池,大小默认为4
	void start(int initThreadSize = 4);

	//禁止拷贝构造
	//原因：成员变量复杂，若允许拷贝构造新对象，容易造成两个对象中的数据指向同一块内存的情况
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;


private:
	//定义线程函数
	void threadFunc(int threadid);

	//检查pool的运行状态
	bool checkRunningState() const;


private:
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;	//线程列表
	int initThreadSize_;	//初始的线程数量
	std::atomic_int curThreadSize_;//记录当前线程池里面线程总数量
	int threadSizeThreshHold_;//线程上限阈值
	std::atomic_int idleThreadSize_;	//记录空闲线程数量

	std::queue<std::shared_ptr<Task>> taskQue_;	//任务队列
	std::atomic_uint taskSize_;	//任务的数量
	int taskQueMaxThreshHold_;	//任务队列数量上限阈值

	std::mutex taskQueMtx_;		//保证任务队列的线程安全
	std::condition_variable notFull_;	//表示任务队列不满
	std::condition_variable notEmpty_;	//保证任务队列不空
	std::condition_variable exitCond_;	//等待线程资源全部回收

	PoolMode poolMode_;		//当前线程池的工作模式

	std::atomic_bool isPoolRunning_;	//当前线程池的启动状态
};



#endif