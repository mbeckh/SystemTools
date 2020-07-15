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

#pragma once

#include <m3c/mutex.h>

#include <atomic>
#include <cstdint>
#include <thread>


namespace systools {

class Path;

class FileComparer {
private:
	enum class State : std::uint_fast8_t;

	struct Context;

public:
	FileComparer();
	FileComparer(const FileComparer&) = delete;
	FileComparer(FileComparer&&) = delete;
	~FileComparer() noexcept;

public:
	FileComparer& operator=(const FileComparer&) = delete;
	FileComparer& operator=(FileComparer&&) = delete;

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
	std::atomic<State> m_state[2];  // NOLINT(modernize-use-default-member-init): Keep code out of the header.
	std::thread m_thread[2];        // NOLINT(modernize-use-default-member-init): Keep code out of the header.
};

}  // namespace systools
