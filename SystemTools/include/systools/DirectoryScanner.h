#pragma once

#include <m3c/Handle.h>
#include <m3c/mutex.h>

#include <systools/Path.h>

#include <iterator>
#include <thread>
#include <vector>

namespace systools {

class DirectoryScanner {
public:
	struct Entry {
		Entry() noexcept = default;
		Entry(Filename name, const LARGE_INTEGER size, const LARGE_INTEGER creationTime, const LARGE_INTEGER lastWriteTime, const ULONG attributes, const FILE_ID_128& fileId) noexcept
			: name(std::move(name))
			, size(size.QuadPart)
			, creationTime(creationTime.QuadPart)
			, lastWriteTime(lastWriteTime.QuadPart)
			, attributes(attributes)
			, fileId(fileId) {
		}
		Entry(const Entry&) = default;
		Entry(Entry&&) noexcept = default;
		~Entry() noexcept = default;

		Entry& operator=(const Entry&) = default;
		Entry& operator=(Entry&&) noexcept = default;

		Filename name;
		std::uint64_t size;
		std::int64_t creationTime;
		std::int64_t lastWriteTime;
		std::uint32_t attributes;
		FILE_ID_128 fileId;
	};

	using Result = std::vector<Entry>;

private:
	struct Context {
		const Path& path;
		Result& directories;
		Result& files;
		std::exception_ptr exceptionPtr;
	};
	enum class State : std::uint_fast8_t { kIdle = 1,
										   kRunning = 2,
										   kShutdown = 4 };

public:
	DirectoryScanner();
	~DirectoryScanner() noexcept;

public:
	void Scan(const Path& path, Result& directories, Result& files) noexcept;
	void Wait();

private:
	void Run() noexcept;

private:
	Context* m_pContext;

	m3c::mutex m_mutex;
	m3c::condition_variable m_stateChanged;
	std::atomic<State> m_state = State::kIdle;
	std::thread m_thread;
};  // namespace systools

}  // namespace systools
