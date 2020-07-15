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

#include "TestUtils.h"

#include <llamalog/llamalog.h>
#include <m3c/exception.h>
#include <m3c/string_encode.h>

#include <systools/DirectoryScanner.h>

#include <windows.h>

#include <cassert>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <ostream>
#include <random>
#include <string>
#include <type_traits>

#ifdef __clang_analyzer__
// Avoid collisions with Windows API defines
#undef GetSystemDirectory
#endif

namespace systools {

void PrintTo(const Filename& filename, std::ostream* const os) {
	*os << m3c::EncodeUtf8(filename.c_str(), filename.size());
}

void PrintTo(const Path& path, std::ostream* const os) {
	*os << m3c::EncodeUtf8(path.c_str(), path.size());
}

void PrintTo(const ScannedFile& scannedFile, std::ostream* const os) {
	*os << "[" << m3c::EncodeUtf8(scannedFile.GetName().c_str(), scannedFile.GetName().size())
		<< ", " << scannedFile.GetSize()
		<< ", " << scannedFile.GetCreationTime()
		<< ", " << scannedFile.GetLastWriteTime()
		<< ", " << std::hex << std::setw(8) << scannedFile.GetAttributes()
		<< ", " << std::setw(2);
	for (int i = 0; i < sizeof(FILE_ID_128::Identifier); ++i) {
		*os << scannedFile.GetFileId().Identifier[i];
	}
	*os << "]";
}

}  // namespace systools

namespace systools::test {

void PrintTo(const LatencyMode mode, _In_ std::ostream* const os) {
	switch (mode) {
	case LatencyMode::kSingle:
	case LatencyMode::kRepeat:
		// underlying type of 8 bits would be interpreted as a char...
		*os << static_cast<std::common_type_t<std::uint32_t, std::underlying_type_t<LatencyMode>>>(mode) << "x";
		break;
	case LatencyMode::kLatency:
		*os << static_cast<std::common_type_t<std::uint32_t, std::underlying_type_t<LatencyMode>>>(LatencyMode::kSingle) << "x, with latency";
		break;
	}
}

WithLatency::WithLatency(const LatencyMode mode)
	: m_mode(mode)
	, m_random(1234U)
	, m_normal(0, 3) {
	// empty
}

MaxRunsType WithLatency::GetMaxRuns() const noexcept {
	return static_cast<MaxRunsType>(m_mode == LatencyMode::kLatency ? LatencyMode::kSingle : m_mode);
}

void WithLatency::AddLatency(const std::chrono::milliseconds min, const std::chrono::milliseconds max) {
	if (m_mode != LatencyMode::kLatency) {
		return;
	}

	assert(max > min);
	while (true) {
		const float val = m_normal(m_random);
		if (val >= -3 && val <= 3) {
			const DWORD sleep = static_cast<DWORD>(std::round(std::fma(max.count() - min.count(), val / 6, std::midpoint(max.count(), min.count()))));
			assert(sleep >= min.count() && sleep <= max.count());
			Sleep(sleep);
			return;
		}
	}
}


Path TestUtils::GetSystemDirectory() {
	std::wstring systemDirectory;
	const UINT len = ::GetSystemDirectoryW(systemDirectory.data(), 0);
	if (!len) {
		THROW(m3c::windows_exception(GetLastError()), "GetSystemDirectory");
	}
	systemDirectory.resize(len);
	const UINT result = ::GetSystemDirectoryW(systemDirectory.data(), len);
	if (!result || result >= len) {
		THROW(m3c::windows_exception(GetLastError()), "GetSystemDirectory");
	}
	systemDirectory.resize(result);
	return Path(systemDirectory);
}

Path TestUtils::GetTempDirectory() {
	std::wstring tempDirectory;
	const DWORD len = ::GetTempPathW(0, tempDirectory.data());
	if (!len) {
		THROW(m3c::windows_exception(GetLastError()), "GetTempPath");
	}
	tempDirectory.resize(len);
	const DWORD result = ::GetTempPathW(len, tempDirectory.data());
	if (!result || result >= len) {
		THROW(m3c::windows_exception(GetLastError()), "GetTempPath");
	}
	tempDirectory.resize(result);
	return Path(tempDirectory);
}

}  // namespace systools::test
