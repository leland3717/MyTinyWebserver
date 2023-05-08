#ifndef LOCKER_H
#define LOCKER_H

#include <pthread.h>
#include <exception>
#include <semaphore.h>

// 线程同步机制封装类

// 互斥锁类
class Locker {
public:
	Locker() {
		if (pthread_mutex_init(&mutex_, NULL) != 0) {
			throw std::exception();
		}
	}

	~Locker() {
		pthread_mutex_destroy(&mutex_);
	}

	bool Lock() {
		return pthread_mutex_lock(&mutex_) == 0;
	}

	bool Unlock() {
		return pthread_mutex_unlock(&mutex_) == 0;
	}

	pthread_mutex_t* mutex() {
		return &mutex_;
	}
private:
	pthread_mutex_t mutex_;
};


// 条件变量类
class Condition {
public:
	Condition() {
		if (pthread_cond_init(&cond_, NULL) != 0) {
			throw std::exception();
		}
	}

	~Condition() {
		pthread_cond_destroy(&cond_);
	}

	bool Wait(pthread_mutex_t* mutex) {
		return pthread_cond_wait(&cond_, mutex) == 0;
	}

	bool TimedWait(pthread_mutex_t* mutex, struct timespec t) {
		return pthread_cond_timedwait(&cond_, mutex, &t) == 0;
	}

	bool Signal() {
		return pthread_cond_signal(&cond_) == 0;
	}

	bool Broadcast() {
		return pthread_cond_broadcast(&cond_) == 0;
	}

private:
	pthread_cond_t cond_;
};


// 信号量类
class Sem {
public:
	Sem() {
		if (sem_init(&sem_, 0, 0) != 0) {
			throw std::exception();
		}
	}

	Sem(int num) {
		if (sem_init(&sem_, 0, num) != 0) {
			throw std::exception();
		}
	}

	~Sem() {
		sem_destroy(&sem_);
	}

	// 信号量-1，为0时阻塞
	bool Wait() {
		return sem_wait(&sem_) == 0;
	}

	// 信号量+1
	bool Post() {
		return sem_post(&sem_) == 0;
	}

private:
	sem_t sem_;
};


#endif // !LOCKER_h
