#ifdef _WIN32
// 防止 windows.h 包含 winsock.h（通过预定义 _WINSOCKAPI_）
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include <windows.h>
#endif

#include "sc_thread.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <atomic>

bool sc_thread_create(sc_thread &thread, sc_thread_fn fn, const char *name, void *userdata)
{
	assert(strlen(name) <= 15);
	std::promise<bool> p;
	thread.result = p.get_future();
	try {
		thread.thread = std::make_unique<std::thread>([fn, userdata, p = std::move(p)]() mutable {
			try {
				p.set_value(fn(userdata));
			} catch (...) {
				p.set_exception(std::current_exception());
			}
		});
	} catch (const std::system_error &e) {
		e;
		return false;
	}

#ifdef __linux__
	pthread_setname_np(thread->thread->native_handle(), name);
#elif defined(_WIN32)
	// Windows 10+  SetThreadDescription need wchar_t
#endif

	return true;
}

bool sc_cond_init(sc_cond &cond)
{
	cond.cond = std::make_unique<std::condition_variable>();
	return true;
}

void sc_cond_signal(sc_cond &cond)
{
	assert(cond.cond.get()); // 确保指针有效
	cond.cond->notify_one();
}

void sc_cond_wait(sc_cond &cond, sc_mutex &mutex)
{
	assert(cond.cond);
#ifndef NDEBUG
	assert(mutex.held()); // 调用前必须已加锁
#endif

	// 采用“已锁”的 mutex
	std::unique_lock<std::mutex> lock(mutex.native(), std::adopt_lock);

	cond.cond->wait(lock);

	lock.release();

#ifndef NDEBUG
	mutex.locker_.store(std::this_thread::get_id(), std::memory_order_relaxed);
#endif
}

bool sc_cond_timedwait(sc_cond &cond, sc_mutex &mutex, sc_tick deadline)
{
	sc_tick now = sc_tick_now();
	if (deadline <= now) {
		return false; // timeout
	}

	// 转为绝对时间点
	auto timeout = std::chrono::steady_clock::now() +
		       std::chrono::milliseconds(SC_TICK_TO_MS(deadline - now + SC_TICK_FROM_MS(1) - 1));

#ifndef NDEBUG
	assert(mutex.held()); // 必须已经上锁
#endif

	// ⚠️ adopt_lock：不再重复加锁
	std::unique_lock<std::mutex> lock(mutex.native(), std::adopt_lock);

	bool signaled = cond.cond->wait_until(lock, timeout) == std::cv_status::no_timeout;

	// ⚠️ mutex 仍由 sc_mutex 管理
	lock.release();

#ifndef NDEBUG
	// wait 返回时 mutex 一定重新被持有
	mutex.locker_.store(std::this_thread::get_id(), std::memory_order_relaxed);
#endif

	return signaled;
}

void sc_thread_join(sc_thread &thread, int *status)
{

	if (thread.thread && thread.thread->joinable()) {
		thread.thread->join();
	}

	if (status) {
		*status = thread.result.get();
	}
}

sc_mutex::sc_mutex() : mutex_(std::make_unique<std::mutex>()) {}

sc_mutex::~sc_mutex()
{
#ifndef NDEBUG
	assert(!held());
#endif
}

void sc_mutex::lock()
{
	assert(mutex_);
#ifndef NDEBUG
	assert(!held()); //forbid recursive locking
#endif
	mutex_->lock();
#ifndef NDEBUG
	locker_.store(std::this_thread::get_id(), std::memory_order_relaxed);
#endif
}

void sc_mutex::unlock()
{
#ifndef NDEBUG
	assert(held());
	locker_.store(sc_thread_id{}, std::memory_order_relaxed);
#endif
	mutex_->unlock();
}

std::mutex &sc_mutex::native()
{
	return *mutex_;
}

#ifndef NDEBUG
bool sc_mutex::held() const
{
	return locker_.load(std::memory_order_relaxed) == std::this_thread::get_id();
}
#endif

