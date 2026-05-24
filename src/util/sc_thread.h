#pragma once

#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <cstring>
#include "tick.h"
#include <future>

typedef std::atomic_uint sc_atomic_thread_id;
typedef int sc_thread_fn(void *);

typedef struct sc_thread {
	std::future<bool> result;
	std::unique_ptr<std::thread> thread;
} sc_thread;

enum sc_thread_priority {
	SC_THREAD_PRIORITY_LOW,
	SC_THREAD_PRIORITY_NORMAL,
	SC_THREAD_PRIORITY_HIGH,
	SC_THREAD_PRIORITY_TIME_CRITICAL,
};

using sc_thread_id = std::thread::id;

class sc_mutex {
public:
	sc_mutex();
	~sc_mutex();

	sc_mutex(const sc_mutex &) = delete;
	sc_mutex &operator=(const sc_mutex &) = delete;

	void lock();

	void unlock();

	std::mutex &native();

#ifndef NDEBUG
	bool held() const;
#endif

	std::unique_ptr<std::mutex> mutex_;

#ifndef NDEBUG
	std::atomic<sc_thread_id> locker_{};
#endif
};

// Guard against future changes that could break sc_mutex
static_assert(std::is_same_v<decltype(&sc_mutex::lock), void (sc_mutex::*)()>);

typedef struct sc_cond {
	/*You can directly make cond a member variable, 
      so there’s no need to initialize it in sc_cond_init.
      Using a smart pointer here is just a habit of 
      using smart pointers.*/
	std::unique_ptr<std::condition_variable> cond;
} sc_cond;

bool sc_thread_create(sc_thread &thread, sc_thread_fn fn, const char *name, void *userdata);

bool sc_cond_init(sc_cond &cond);

void sc_cond_signal(sc_cond &cond);

void sc_cond_wait(sc_cond &cond, sc_mutex &mutex);

bool sc_cond_timedwait(sc_cond &cond, sc_mutex &mutex, sc_tick deadline);

void sc_thread_join(sc_thread &thread, int *status);
