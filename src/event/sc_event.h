#pragma once
#include <atomic>
#include <future>

struct ServerConnectSignal {
	std::promise<bool> promise;
	std::atomic<bool> completed{false};

	void reset()
	{
		promise = std::promise<bool>();
		completed.store(false, std::memory_order_release);
	}

	bool try_complete(bool connected)
	{
		bool expected = false;
		if (!completed.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
			return false;
		promise.set_value(connected);
		return true;
	}
};
