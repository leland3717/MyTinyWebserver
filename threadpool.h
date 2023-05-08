#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <list>
#include <exception>
#include "locker.h"
#include <iostream>

// T是任务类型
template <typename T>
class Threadpool {
public:
	Threadpool(int thread_num = 8, int max_requests = 10000);
	~Threadpool();
	bool AppendTask(T* request);
	void Run();
private:
	static void* Worker(void* arg);
private:
	// 线程数量
	int thread_num_;

	// 线程数组，数组大小为thread_num_
	pthread_t* threads_;

	// 允许的最大请求个数
	int max_requests_;

	// 请求队列
	std::list<T*> work_queue_;

	Locker queue_locker_;

	// 信号量用来判断是否有任务需要处理
	Sem queue_state_;

	// 是否结束线程
	bool stop_;
};

template <typename T>
Threadpool<T>::Threadpool(int thread_num, int max_requests) :
	thread_num_(thread_num), max_requests_(max_requests),
	stop_(false), threads_(NULL) {
	if (thread_num <= 0 || max_requests <= 0) {
		throw std::exception();
	}

	threads_ = new pthread_t[thread_num];
	if (!threads_) {
		throw std::exception();
	}

	for (int i = 0; i < thread_num; ++i) {
		std::cout << "create the " << i << "-th thread..." << std::endl;
		if (pthread_create(threads_ + i, NULL, Worker, this) != 0) {
			delete[] threads_;
			throw std::exception();
		}
		if (pthread_detach(threads_[i]) != 0) {
			delete[] threads_;
			throw std::exception();
		}
	}
}

template <typename T>
Threadpool<T>::~Threadpool() {
	delete[] threads_;
	stop_ = true;
}

template <typename T>
bool Threadpool<T>::AppendTask(T* request) {
	// 上锁添加任务，避免工作线程取任务时发生冲突
	queue_locker_.Lock();

	if (work_queue_.size() >= max_requests_) {
		queue_locker_.Unlock();
		return false;
	}

	work_queue_.push_back(request);
	queue_state_.Post();

	queue_locker_.Unlock();

	return true;
}

template <typename T>
void* Threadpool<T>::Worker(void* arg) {
	// 所有线程指向同一个Threadpool对象
	Threadpool* pool = (Threadpool*)arg;
	pool->Run();
	return pool;
}

template <typename T>
void Threadpool<T>::Run() {
	while (!stop_) {
		queue_state_.Wait();
		// 取出请求队列的任务
		queue_locker_.Lock();
		if (work_queue_.empty()) {
			queue_locker_.Unlock();
			continue;
		}
		T* request = work_queue_.front();
		work_queue_.pop_front();
		queue_locker_.Unlock();

		if (!request) {
			continue;
		}

		request->Process();
	}
}
#endif
