#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <list>
#include <exception>
#include "locker.h"
#include <iostream>

// T����������
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
	// �߳�����
	int thread_num_;

	// �߳����飬�����СΪthread_num_
	pthread_t* threads_;

	// ���������������
	int max_requests_;

	// �������
	std::list<T*> work_queue_;

	Locker queue_locker_;

	// �ź��������ж��Ƿ���������Ҫ����
	Sem queue_state_;

	// �Ƿ�����߳�
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
	// ����������񣬱��⹤���߳�ȡ����ʱ������ͻ
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
	// �����߳�ָ��ͬһ��Threadpool����
	Threadpool* pool = (Threadpool*)arg;
	pool->Run();
	return pool;
}

template <typename T>
void Threadpool<T>::Run() {
	while (!stop_) {
		queue_state_.Wait();
		// ȡ��������е�����
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
