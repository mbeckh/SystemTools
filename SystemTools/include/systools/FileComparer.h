#pragma once

#include <m3c/mutex.h>

#include <thread>


namespace systools {

class Path;

class FileComparer {
private:
	enum class State : std::uint_fast8_t { kIdle = 1,
										   kRunning = 2,
										   kAbort = 4,
										   kShutdown = 8 };

	struct Context;

public:
	FileComparer();
	~FileComparer() noexcept;

public:
	bool Compare(const Path& src, const Path& cpy);

private:
	bool CompareFiles(Context& context);

	void Run(std::uint_fast8_t index) noexcept;
	void ReadFileContent(std::uint_fast8_t index) noexcept;

private:
	Context* m_pContext;

	m3c::mutex m_mutex;
	m3c::condition_variable m_clients;
	m3c::condition_variable m_master;
	std::atomic<State> m_state[2] = {State::kIdle, State::kIdle};
	std::thread m_thread[2];
};

}  // namespace systools
