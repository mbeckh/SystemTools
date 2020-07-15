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

#pragma once

#include "systools/Path.h"

#include <sal.h>

#include <chrono>
#include <cstdint>
#include <iosfwd>
#include <random>

#ifdef __clang_analyzer__
// Avoid collisions with Windows API defines
#undef GetSystemDirectory
#endif

namespace systools {

class ScannedFile;

void PrintTo(const Filename& filename, std::ostream* const os);
void PrintTo(const Path& path, std::ostream* const os);
void PrintTo(const ScannedFile& scannedFile, std::ostream* const os);

}  // namespace systools

namespace systools::test {

using MaxRunsType = std::uint_fast8_t;

enum class LatencyMode : MaxRunsType {
	kSingle = 1,
	kLatency = 0,
	kRepeat = 3
};

void PrintTo(const LatencyMode mode, _In_ std::ostream* const os);

#define WITH_LATENCY(min_, max_, action_) \
	t::DoAll(t::InvokeWithoutArgs([this] { AddLatency(min_, max_); }), action_)

class WithLatency {
protected:
	explicit WithLatency(LatencyMode mode);

protected:
	MaxRunsType GetMaxRuns() const noexcept;
	void AddLatency(const std::chrono::milliseconds min, const std::chrono::milliseconds max);

private:
	const LatencyMode m_mode;
	std::mt19937 m_random;
	std::normal_distribution<float> m_normal;
};

class TestUtils {
private:
	TestUtils() noexcept = default;
	~TestUtils() noexcept = default;

public:
	static Path GetSystemDirectory();
	static Path GetTempDirectory();
};

}  // namespace systools::test
