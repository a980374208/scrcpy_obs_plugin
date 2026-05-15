#pragma once
#include <future>

struct ServerConnectSignal {
	std::promise<bool> promise;
};
