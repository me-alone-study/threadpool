#include<iostream>
#include <chrono>
#include <thread>


#include "threadpool.h"
using uLong = unsigned long long;

class Mytask :public Task
{
public:
	Mytask(int begin, int end)
		:begin_(begin)
		, end_(end)
	{}

	//��ô����run�����ķ���ֵ�����Ա�ʾ���������
	//C++17 any����(�ڱ���Ŀ�У��Լ��������Ƶ�����)
	Any run()	//run�����������̳߳ط�����߳���ȥִ��
	{
		std::cout << "tid:" << std::this_thread::get_id()
			<< "begin!" << std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(3));
		uLong sum = 0;
		for (uLong i = begin_; i <= end_; i++)
			sum += i;
		std::cout << "tid:" << std::this_thread::get_id()
			<< "end!" << std::endl;

		return sum;
	}

private:
	int begin_;
	int end_;
};

int main() {
	ThreadPool pool;
	pool.setMode(PoolMode::MODE_CACHED);

	//�����̳߳�
	pool.start(4);
	
	//˯�ߣ�������߳�id����ʾ
	//std::this_thread::sleep_for(std::chrono::seconds(5));

	Result res1 = pool.submitTask(std::make_shared<Mytask>(1, 100000));
	Result res2 = pool.submitTask(std::make_shared<Mytask>(100001,200000));
	Result res3 = pool.submitTask(std::make_shared<Mytask>(200000, 300000));
	pool.submitTask(std::make_shared<Mytask>(1, 100000));
	pool.submitTask(std::make_shared<Mytask>(100001, 200000));
	pool.submitTask(std::make_shared<Mytask>(200000, 300000));


	uLong sum1 = res1.get().cast_<uLong>();
	uLong sum2 = res2.get().cast_<uLong>();
	uLong sum3 = res3.get().cast_<uLong>();

	
	std::cout << (sum1 + sum2 + sum3) << std::endl;


	getchar();
}