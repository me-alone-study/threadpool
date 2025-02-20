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


//any���ͣ����Խ�����������
class Any
{
public:
	Any() = default;
	~Any() = default;
	Any(const Any&) = delete;	//���ÿ������캯��
	Any& operator=(const Any&) = delete;
	Any(Any&&) = default;	//������ֵ���á��ƶ�����
	Any& operator=(Any&&) = default;

	//���캯��ּ�ڽ��ո������͵�����
	template<typename T> 
	Any(T data) : base_(std::make_unique<Derive<T>>(data))
	{}

	//��ȡ���յ�������data
	template<typename T>
	T cast_()
	{
		//�ӻ������ҵ�����ָ����������󣬴�������ȡ��,����ת������
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());	//ת��ʧ����Ϊ��
		if (pd == nullptr)
		{
			throw "type is unmatch!";
		}
		return pd->data_;
	}
private:
	//��������
	class Base
	{
	public:
		virtual ~Base() = default;
	};

	//����������
	template<typename T>
	class Derive :public Base
	{
	public:
		Derive(T data):data_(data)
		{}
		T data_;	//�������������������
	};

private:
	//����һ������ָ��
	std::unique_ptr<Base> base_;
};


//ʵ��һ���ź�����
class Semaphore
{
public:
	Semaphore(int limit = 0)
		:resLimit_(limit)
	{}
	~Semaphore() = default;

	//��ȡһ���ź�����Դ
	void wait()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		//�ȴ��ź�������Դ��û����Դ����������ǰ�߳�
		cond_.wait(lock, [&]()->bool {return resLimit_ > 0; });
		resLimit_--;
	}

	//����һ���ź�����Դ
	void post()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		resLimit_++;
		//֪ͨ
		cond_.notify_all();
	}
private:
	//��Դ����
	int resLimit_;

	std::mutex mtx_;
	std::condition_variable cond_;
};

class Task;
//ʵ�ֽ����̳߳ص�task����ִ����ɺ�ķ���ֵ����
class Result
{
public:
	Result(std::shared_ptr<Task> task, bool isValid=true);
	~Result() = default;

	//setVal��������ȡ����ִ����ķ���ֵ
	void setValue(Any);

	//get�������û��������������ȡtask�ķ���ֵ
	Any get();
private:
	Any any_;	//�洢����ķ���ֵ
	Semaphore sem_;	//�߳�ͨ���ź���
	std::shared_ptr<Task> task_;	//ָ���ȡ����ֵ���������������Ҫ���˾ֲ�������ͱ��ͷ�
	std::atomic_bool isValid_;	//�жϷ���ֵ�Ƿ���Ч
};

//�̳߳�֧�ֵ�ģʽ
enum PoolMode
{
	MODE_FIXED,		//�̶��������߳�
	MODE_CACHED,	//�߳������ɶ�̬����
};

//����������
class Task 
{
public:
	Task();
	~Task() = default;

	void exec();
	void setResult(Result* res);
	//�û������Զ��������������ͣ��̳�����дrun������ʵ���Զ���������
	virtual Any run() = 0;

private:
	Result* result_;
};

//�߳�����
class Thread 
{
public:
	using ThreadFunc = std::function<void(int)>;

	//�̹߳���
	Thread(ThreadFunc func);
	
	// �߳�����
	~Thread();

	//�����߳�
	void start();

	//��ȡ�߳�id
	int getId() const;

private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_;	//�����߳�id
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

//�̳߳�����
class ThreadPool
{
public:
	//Ŀǰ��Ĭ�Ϲ��캯��
	ThreadPool();
	~ThreadPool();

	//�����̳߳صĹ���ģʽ
	void setMode(PoolMode mode);


	//���ó�ʼ���߳�����
	//void setInitThreadSize(int size);

	//����task�������������ֵ
	void setTaskQueMaxThreshHold(int threshhold);

	//�����̳߳�cachedģʽ���߳���ֵ
	void setThreadThreshHold(int threshhold);

	//���̳߳��ύ����
	Result submitTask(std::shared_ptr<Task> sp);

	//�����̳߳�,��СĬ��Ϊ4
	void start(int initThreadSize = 4);

	//��ֹ��������
	//ԭ�򣺳�Ա�������ӣ��������������¶�������������������е�����ָ��ͬһ���ڴ�����
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;


private:
	//�����̺߳���
	void threadFunc(int threadid);

	//���pool������״̬
	bool checkRunningState() const;


private:
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;	//�߳��б�
	int initThreadSize_;	//��ʼ���߳�����
	std::atomic_int curThreadSize_;//��¼��ǰ�̳߳������߳�������
	int threadSizeThreshHold_;//�߳�������ֵ
	std::atomic_int idleThreadSize_;	//��¼�����߳�����

	std::queue<std::shared_ptr<Task>> taskQue_;	//�������
	std::atomic_uint taskSize_;	//���������
	int taskQueMaxThreshHold_;	//�����������������ֵ

	std::mutex taskQueMtx_;		//��֤������е��̰߳�ȫ
	std::condition_variable notFull_;	//��ʾ������в���
	std::condition_variable notEmpty_;	//��֤������в���
	std::condition_variable exitCond_;	//�ȴ��߳���Դȫ������

	PoolMode poolMode_;		//��ǰ�̳߳صĹ���ģʽ

	std::atomic_bool isPoolRunning_;	//��ǰ�̳߳ص�����״̬
};



#endif