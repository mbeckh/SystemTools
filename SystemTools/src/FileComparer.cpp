/*
Copyright 2020 Michael Beckh

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

/// @file

#include "systools/FileComparer.h"

#include <llamalog/llamalog.h>
#include <m3c/exception.h>
#include <m3c/handle.h>
#include <m3c/mutex.h>

#include <systools/Path.h>
#include <systools/Volume.h>

#include <windows.h>

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <exception>
#include <memory>
#include <new>
#include <numeric>
#include <type_traits>

namespace systools {

namespace {

constexpr std::uint32_t kTargetBufferSize = 0x10000;
constexpr std::uint32_t kThreadDone = 0xFFFFFFFF;

}  // namespace

enum class FileComparer::State : std::uint_fast8_t { kIdle = 1,
													 kRunning = 2,
													 kAbort = 4,
													 kShutdown = 8 };

struct FileComparer::Context {
	const std::uint_fast32_t bufferSize;
	const Path* path[2];
	std::byte* buffer[2][2];
	std::atomic_uint_fast32_t size[2][2];
	std::exception_ptr exceptionPtr[2];
};

FileComparer::FileComparer()
	: m_state{State::kIdle, State::kIdle}
	, m_thread{std::thread(
				   [](FileComparer* const pComparer) noexcept {
					   pComparer->Run(0);
				   }  // namespace systools
				   ,
				   this),
			   std::thread(
				   [](FileComparer* const pComparer) noexcept {
					   pComparer->Run(1);
				   }  // namespace systools
				   ,
				   this)} {
}

FileComparer::~FileComparer() noexcept {
	{
		LOG_TRACE("Sending shutdown");
		m3c::scoped_lock lock(m_mutex);
		m_state[0].store(State::kShutdown, std::memory_order_release);
		m_state[1].store(State::kShutdown, std::memory_order_release);
	}
	m_clients.notify_all();

	for (std::uint_fast8_t index = 0; index < 2; ++index) {
		LOG_TRACE("Waiting for thread {}", index);
		try {
			m_thread[index].join();
		} catch (const std::exception& e) {
			LOG_ERROR("thread.join: {}", e);
		}
	}
	LOG_TRACE("Shutdown complete");
}

bool FileComparer::Compare(const Path& src, const Path& cpy) {
	assert(m_state[0].load(std::memory_order_acquire) == State::kIdle);
	assert(m_state[1].load(std::memory_order_acquire) == State::kIdle);

	//
	// set up the buffer with property alignment

	Volume srcVolume(src);
	Volume cpyVolume(cpy);

	const std::align_val_t srcAlignment = srcVolume.GetUnbufferedMemoryAlignment();
	const std::align_val_t cpyAlignment = cpyVolume.GetUnbufferedMemoryAlignment();
	const std::uint_fast32_t chunkSize = std::lcm(std::lcm(std::lcm(srcVolume.GetUnbufferedFileOffsetAlignment(), cpyVolume.GetUnbufferedFileOffsetAlignment()), static_cast<std::uint32_t>(srcAlignment)), static_cast<std::uint32_t>(cpyAlignment));

	const std::uint_fast32_t bufferSize = static_cast<std::uint32_t>(kTargetBufferSize / chunkSize) * chunkSize;
	const std::size_t allocationSize = static_cast<std::size_t>(bufferSize) * 2;

	const auto srcDeleter = [allocationSize, srcAlignment](void* const p) noexcept {
		operator delete[](p, allocationSize, srcAlignment);
	};
	const auto cpyDeleter = [allocationSize, cpyAlignment](void* const p) noexcept {
		operator delete[](p, allocationSize, cpyAlignment);
	};
	const std::unique_ptr<std::byte[], decltype(srcDeleter)> srcBuffer(static_cast<std::byte*>(operator new[](allocationSize, srcAlignment)), srcDeleter);
	const std::unique_ptr<std::byte[], decltype(cpyDeleter)> cpyBuffer(static_cast<std::byte*>(operator new[](allocationSize, cpyAlignment)), cpyDeleter);

	LOG_TRACE("Comparing {} and {} with buffer size {}, file offset alignment {} and memory alignment {}/{}", src, cpy, bufferSize, chunkSize, srcAlignment, cpyAlignment);

	Context context = {
		bufferSize,
		{std::addressof(src), std::addressof(cpy)},
		{{&srcBuffer[0], &cpyBuffer[0]},
		 {&srcBuffer[bufferSize], &cpyBuffer[bufferSize]}}};
	m_pContext = &context;
	std::atomic_thread_fence(std::memory_order_release);

	{
		m3c::scoped_lock lock(m_mutex);
		m_state[0].store(State::kRunning, std::memory_order_release);
		m_state[1].store(State::kRunning, std::memory_order_release);
	}
	m_clients.notify_all();

	bool result;
	try {
		result = CompareFiles(context);
	} catch (...) {
		LOG_TRACE("Error comparing files {} and {}", cpy, src);
		{
			m3c::shared_lock lock(m_mutex);
			while (m_state[0].load(std::memory_order_acquire) != State::kIdle || m_state[1].load(std::memory_order_acquire) != State::kIdle) {
				LOG_TRACE("Waiting for threads");
				m_master.wait(lock);
			}
		}
		throw;
	}

	{
		m3c::shared_lock lock(m_mutex);
		while (m_state[0].load(std::memory_order_acquire) != State::kIdle || m_state[1].load(std::memory_order_acquire) != State::kIdle) {
			LOG_TRACE("Waiting for threads");
			m_master.wait(lock);
		}
	}

	LOG_TRACE("Files {} and {} are {}equal", cpy, src, result ? "" : "not ");
	return result;
}

bool FileComparer::CompareFiles(Context& context) {
	std::uint_fast8_t readIndex = 0;
	while (true) {
		// wait for data being available
		{
			m3c::shared_lock lock(m_mutex);
			while ((!context.size[readIndex][0].load(std::memory_order_acquire) || !context.size[readIndex][1].load(std::memory_order_acquire)) && (m_state[0].load(std::memory_order_acquire) == State::kRunning || m_state[1].load(std::memory_order_acquire) == State::kRunning)) {
				LOG_TRACE("Waiting for data");
				m_master.wait(lock);
			}
		}
		std::atomic_thread_fence(std::memory_order_acquire);

		// check for errors
		if (context.exceptionPtr[0] || context.exceptionPtr[1]) {
			const std::uint_fast8_t errorIndex = context.exceptionPtr[0] ? 0 : 1;
			LOG_TRACE("Error in thread {}", errorIndex);
			assert(context.exceptionPtr[errorIndex]);
			std::rethrow_exception(context.exceptionPtr[errorIndex]);
			__assume(false);
		}

		std::uint_fast32_t size;
		if ((size = context.size[readIndex][0].load(std::memory_order_relaxed)) != context.size[readIndex][1].load(std::memory_order_relaxed)) {
			LOG_TRACE("Files differ in size for buffer {}: {} / {}, aborting", readIndex, size, context.size[readIndex][1].load(std::memory_order_relaxed));
			{
				m3c::scoped_lock lock(m_mutex);
				if (m_state[0].load(std::memory_order_acquire) == State::kRunning) {
					m_state[0].store(State::kAbort, std::memory_order_release);
				}
				if (m_state[1].load(std::memory_order_acquire) == State::kRunning) {
					m_state[1].store(State::kAbort, std::memory_order_release);
				}
			}
			m_clients.notify_all();
			return false;
		}

		// done
		if (size == kThreadDone) {
			LOG_TRACE("Received EOF in buffer {}", readIndex);
			assert(context.size[readIndex][1].load(std::memory_order_relaxed) == kThreadDone);
			assert(!context.exceptionPtr[0]);
			assert(!context.exceptionPtr[1]);
			return true;
		}

		assert(size <= context.bufferSize);
		if (std::memcmp(context.buffer[readIndex][0], context.buffer[readIndex][1], size) != 0) {
			LOG_TRACE("Files differ in buffer {}", readIndex);
			{
				m3c::scoped_lock lock(m_mutex);
				if (m_state[0].load(std::memory_order_acquire) == State::kRunning) {
					m_state[0].store(State::kAbort, std::memory_order_release);
				}
				if (m_state[1].load(std::memory_order_acquire) == State::kRunning) {
					m_state[1].store(State::kAbort, std::memory_order_release);
				}
			}
			m_clients.notify_all();
			return false;
		}

		LOG_TRACE("Data in buffer {} is equal", readIndex);
		{
			m3c::scoped_lock lock(m_mutex);
			context.size[readIndex][0].store(0, std::memory_order_release);
			context.size[readIndex][1].store(0, std::memory_order_release);
		}
		m_clients.notify_all();

		readIndex ^= 1;
	}
}

void FileComparer::Run(const std::uint_fast8_t index) noexcept {
	LOG_TRACE("Thread {} started", index);
	while (true) {
		// wait for next call to Compare
		State state;
		{
			m3c::shared_lock lock(m_mutex);
			while ((state = m_state[index].load(std::memory_order_acquire)) == State::kIdle) {
				LOG_TRACE("Thread {} waiting for compare", index);
				m_clients.wait(lock);
			}
		}
		if (state == State::kShutdown) {
			break;
		}
		std::atomic_thread_fence(std::memory_order_acquire);

		LOG_TRACE("Thread {} running", index);
		ReadFileContent(index);

		LOG_TRACE("Thread {} completed compare", index);
		{
			m3c::scoped_lock lock(m_mutex);
			state = m_state[index].exchange(State::kIdle, std::memory_order_acq_rel);
		}
		if (state == State::kShutdown) {
			break;
		}
		m_master.notify_one();
	}
	LOG_TRACE("Thread {} stopped", index);
}

void FileComparer::ReadFileContent(const std::uint_fast8_t index) noexcept {
	std::uint_fast8_t writeIndex = 0;
	try {
		const m3c::handle hFile = CreateFileW(m_pContext->path[index]->c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING, nullptr);
		if (!hFile) {
			THROW(m3c::windows_exception(GetLastError()), "CreateFile {}", m_pContext->path[index]);
		}

		while (true) {
			{
				m3c::shared_lock lock(m_mutex);
				while (m_pContext->size[writeIndex][index].load(std::memory_order_acquire) && m_state[index].load(std::memory_order_acquire) == State::kRunning) {
					LOG_TRACE("Thread {} waiting for free buffer {}", index, writeIndex);
					m_clients.wait(lock);
				}
			}
			if (m_state[index].load(std::memory_order_relaxed) != State::kRunning) {
				LOG_TRACE("Thread {} received stop signal", index);
				return;
			}

			DWORD bytesRead;
			if (!ReadFile(hFile, m_pContext->buffer[writeIndex][index], m_pContext->bufferSize, &bytesRead, nullptr)) {
				THROW(m3c::windows_exception(GetLastError()), "ReadFile {}", m_pContext->path[index]);
			}

			if (!bytesRead) {
				LOG_TRACE("Thread {} at EOF for buffer {}", index, writeIndex);
				{
					m3c::scoped_lock lock(m_mutex);
					m_pContext->size[writeIndex][index].store(kThreadDone, std::memory_order_release);
				}
				return;
			}

			std::atomic_thread_fence(std::memory_order_release);
			{
				LOG_TRACE("Thread {} read buffer {}", index, writeIndex);
				m3c::scoped_lock lock(m_mutex);
				m_pContext->size[writeIndex][index].store(bytesRead, std::memory_order_release);
			}

			m_master.notify_one();
			writeIndex ^= 1;
		}
	} catch (...) {
		LOG_TRACE("Error in thread {} ", index);
		m_pContext->exceptionPtr[index] = std::current_exception();
		std::atomic_thread_fence(std::memory_order_release);
		{
			m3c::scoped_lock lock(m_mutex);
			if (m_state[index ^ 1].load(std::memory_order_acquire) == State::kRunning) {
				LOG_TRACE("Sending abort to thread {} ", index ^ 1);
				m_state[index ^ 1].store(State::kAbort, std::memory_order_release);
			}
		}
		m_clients.notify_one();
	}
}

}  // namespace systools
