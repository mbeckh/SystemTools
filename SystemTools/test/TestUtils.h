#pragma once

#include "systools/Path.h"

#include <chrono>
#include <iosfwd>
#include <random>

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
	WithLatency(LatencyMode mode);

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
