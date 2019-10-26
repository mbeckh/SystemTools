#pragma once

#include "systools/Path.h"

namespace systools::test {

class TestUtils {
private:
	TestUtils() noexcept = default;
	~TestUtils() noexcept = default;

public:
	static Path GetSystemDirectory();
	static Path GetTempDirectory();
};

}  // namespace systools::test
