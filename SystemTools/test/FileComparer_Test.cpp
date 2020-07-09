#include "systools/FileComparer.h"

#include "TestUtils.h"
#include "systools/Path.h"
#include "systools/Volume.h"

#include <m3c/handle.h>
#include <m4t/m4t.h>

#include <detours_gmock.h>

#include <algorithm>
#include <chrono>


namespace systools::test {

namespace t = testing;
namespace dtgm = detours_gmock;

#define WIN32_FUNCTIONS(fn_)                                                                                                                                                                       \
	fn_(7, HANDLE, WINAPI, CreateFileW,                                                                                                                                                            \
		(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile), \
		(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile),                                                              \
		nullptr);                                                                                                                                                                                  \
	fn_(1, BOOL, WINAPI, CloseHandle,                                                                                                                                                              \
		(HANDLE hObject),                                                                                                                                                                          \
		(hObject),                                                                                                                                                                                 \
		nullptr);                                                                                                                                                                                  \
	fn_(5, BOOL, WINAPI, ReadFile,                                                                                                                                                                 \
		(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped),                                                                       \
		(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped),                                                                                                                \
		nullptr);

#define VOLUME_FUNCTIONS(fn_)                                       \
	fn_(Volume, 0, std::uint32_t, GetUnbufferedFileOffsetAlignment, \
		(),                                                         \
		(),                                                         \
		nullptr);                                                   \
	fn_(Volume, 0, std::align_val_t, GetUnbufferedMemoryAlignment,  \
		(),                                                         \
		(),                                                         \
		nullptr);

DTGM_DECLARE_API_MOCK(Win32, WIN32_FUNCTIONS);
DTGM_DECLARE_CLASS_MOCK(Volume, VOLUME_FUNCTIONS);

namespace {

#pragma warning(suppress : 4100)
ACTION_P(Read, data) {
	ZeroMemory(arg1, arg2);
	CopyMemory(arg1, &data, std::min(arg2, static_cast<DWORD>(sizeof(data))));
	*arg3 = arg2;
	return TRUE;
}

#pragma warning(suppress : 4100)
ACTION_P2(Read, data, size) {
	if constexpr (std::is_floating_point_v<size_type>) {
		*arg3 = static_cast<DWORD>(arg2 * size);
	} else if (size < 0) {
		*arg3 = static_cast<DWORD>(std::max<std::int64_t>(arg2 + size, 0));
	} else {
		*arg3 = std::min<DWORD>(arg2, size);
	}
	ZeroMemory(arg1, *arg3);
	CopyMemory(arg1, &data, std::min(*arg3, static_cast<DWORD>(sizeof(data))));
	return TRUE;
}

#pragma warning(suppress : 4100)
ACTION(Eof) {
	*arg3 = 0;
	return TRUE;
}

}  // namespace

class FileComparer_BaseTest : public t::TestWithParam<std::tuple<std::uint32_t, std::uint32_t, LatencyMode>>
	, public WithLatency {
public:
	FileComparer_BaseTest()
		: WithLatency(std::get<2>(GetParam())) {
		// empty
	}

protected:
	void SetUp() override {
		using namespace std::literals::chrono_literals;

		const m3c::handle hMutex = CreateMutexW(nullptr, FALSE, nullptr);
		if (!hMutex) {
			THROW(m3c::windows_exception(GetLastError()), "CreateMutex");
		}

		for (auto i = 0; i < sizeof(m_hFile) / sizeof(m_hFile[0]); ++i) {
			HANDLE handle;
			if (!DuplicateHandle(GetCurrentProcess(), hMutex, GetCurrentProcess(), &handle, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
				THROW(m3c::windows_exception(GetLastError()), "DuplicateHandle");
			}
			m_hFile[i] = handle;

			ON_CALL(m_win32, CreateFileW(t::Eq(kTestFile[i]), DTGM_ARG6))
				.WillByDefault(WITH_LATENCY(20ms, 60ms, t::WithoutArgs([this, i]() noexcept {
												++m_openHandles;
												return m_hFile[i].get();
											})));

			ON_CALL(m_win32, ReadFile(m_hFile[i].get(), DTGM_ARG4))
				.WillByDefault(t::InvokeWithoutArgs([]() noexcept {
					SetLastError(ERROR_FILE_INVALID);
					return FALSE;
				}));

			ON_CALL(m_win32, CloseHandle(m_hFile[i].get()))
				.WillByDefault(WITH_LATENCY(4ms, 10ms, t ::WithoutArgs([this]() noexcept {
												--m_openHandles;
												return TRUE;
											})));
		}

		ON_CALL(m_volume, GetUnbufferedFileOffsetAlignment())
			.WillByDefault(WITH_LATENCY(4ms, 12ms, [this] {
				const Volume::string_type& name = m_volume.self().GetName();
				if (name.sv().compare(0, name.size(), kTestFile[0]) == 0) {
					return 32u;
				}
				if (name.sv().compare(0, name.size(), kTestFile[1]) == 0) {
					return 512u;
				}
				assert(false);
				_assume(false);
			}));
		ON_CALL(m_volume, GetUnbufferedMemoryAlignment())
			.WillByDefault(WITH_LATENCY(4ms, 12ms, [this] {
				const Volume::string_type& name = m_volume.self().GetName();
				if (name.sv().compare(0, name.size(), kTestFile[0]) == 0) {
					return static_cast<std::align_val_t>(128);
				}
				if (name.sv().compare(0, name.size(), kTestFile[1]) == 0) {
					return static_cast<std::align_val_t>(4096);
				}
				assert(false);
				_assume(false);
			}));
	}

	void TearDown() override {
		EXPECT_EQ(0u, m_openHandles);
		t::Mock::VerifyAndClearExpectations(&m_volume);
		t::Mock::VerifyAndClearExpectations(&m_win32);
		DTGM_DETACH_CLASS_MOCK(Volume);
		DTGM_DETACH_API_MOCK(Win32);
	}

protected:
	inline static const std::wstring kTestFile[2] = {
		LR"(\\?\Volume{23220209-1205-1000-8000-0000000001}\23220209-1205-1000-8000-0000001001.dat)",
		LR"(\\?\Volume{23220209-1205-1000-8000-0000000002}\23220209-1205-1000-8000-0000001002.dat)"};

protected:
	DTGM_DEFINE_API_MOCK(Win32, m_win32);
	DTGM_DEFINE_CLASS_MOCK(Volume, m_volume);
	m3c::handle m_hFile[sizeof(kTestFile) / sizeof(kTestFile[0])];

private:
	std::atomic_uint32_t m_openHandles = 0;
};

using FileComparer_EqualDataTest = FileComparer_BaseTest;
using FileComparer_UnequalDataAtStartTest = FileComparer_BaseTest;
using FileComparer_UnequalDataAtMiddleTest = FileComparer_BaseTest;
using FileComparer_UnequalDataAtEndTest = FileComparer_BaseTest;

//
// Equal
//

TEST_P(FileComparer_EqualDataTest, Compare_EqualData_ReturnResult) {
	using namespace std::literals::chrono_literals;

	const std::uint32_t srcSize = std::get<0>(GetParam());
	const std::uint32_t cpySize = std::get<1>(GetParam());

	FileComparer comparer;
	for (MaxRunsType runs = 0, maxRuns = GetMaxRuns(); runs < maxRuns; ++runs) {
		auto& srcExpectation = EXPECT_CALL(m_win32, ReadFile(m_hFile[0].get(), DTGM_ARG4))
								   .Times(t::AnyNumber());
		auto& cpyExpectation = EXPECT_CALL(m_win32, ReadFile(m_hFile[1].get(), DTGM_ARG4))
								   .Times(t::AnyNumber());
		for (std::uint32_t i = 0; i < srcSize; i += 10) {
			srcExpectation.WillOnce(WITH_LATENCY(10ms, 30ms, Read(0xDEADBEEF)));
		}
		for (std::uint32_t i = 0; i < cpySize; i += 10) {
			cpyExpectation.WillOnce(WITH_LATENCY(10ms, 30ms, Read(0xDEADBEEF)));
		}
		if (srcSize % 10) {
			srcExpectation.WillOnce(WITH_LATENCY(10ms, 30ms, Read(0xDEADBEEF, 0.5)));
		}
		if (cpySize % 10) {
			cpyExpectation.WillOnce(WITH_LATENCY(10ms, 30ms, Read(0xDEADBEEF, 0.5)));
		}
		srcExpectation.WillOnce(WITH_LATENCY(10ms, 30ms, Eof()));
		cpyExpectation.WillOnce(WITH_LATENCY(10ms, 30ms, Eof()));

		EXPECT_EQ(srcSize == cpySize, comparer.Compare(Path(kTestFile[0]), Path(kTestFile[1])));
	}
}

TEST_P(FileComparer_EqualDataTest, Compare_ErrorCreatingFile_ThrowException) {
	using namespace std::literals::chrono_literals;

	const std::uint32_t srcSize = std::get<0>(GetParam());
	const std::uint32_t cpySize = std::get<1>(GetParam());

	FileComparer comparer;
	for (MaxRunsType runs = 0, maxRuns = GetMaxRuns(); runs < maxRuns; ++runs) {
		EXPECT_CALL(m_win32, CreateFileW(t::Eq(kTestFile[0]), DTGM_ARG6))
			.WillOnce(WITH_LATENCY(20ms, 40ms, dtgm::SetLastErrorAndReturn(ERROR_ACCESS_DENIED, INVALID_HANDLE_VALUE)));
		auto& cpyExpectation = EXPECT_CALL(m_win32, ReadFile(m_hFile[1].get(), DTGM_ARG4))
								   .Times(t::AnyNumber());
		for (std::uint32_t i = 0; i < cpySize; i += 10) {
			cpyExpectation.WillOnce(WITH_LATENCY(10ms, 30ms, Read(0xDEADBEEF)));
		}
		if (cpySize % 10) {
			cpyExpectation.WillOnce(WITH_LATENCY(10ms, 30ms, Read(0xDEADBEEF, 0.5)));
		}
		cpyExpectation.WillOnce(WITH_LATENCY(10ms, 30ms, Eof()));

		EXPECT_THROW(comparer.Compare(Path(kTestFile[0]), Path(kTestFile[1])), m3c::windows_exception);
	}
}

TEST_P(FileComparer_EqualDataTest, Compare_ErrorReadingFile_ThrowException) {
	using namespace std::literals::chrono_literals;

	const std::uint32_t srcSize = std::get<0>(GetParam());
	const std::uint32_t cpySize = std::get<1>(GetParam());

	FileComparer comparer;
	for (MaxRunsType runs = 0, maxRuns = GetMaxRuns(); runs < maxRuns; ++runs) {
		auto& srcExpectation = EXPECT_CALL(m_win32, ReadFile(m_hFile[0].get(), DTGM_ARG4))
								   .Times(t::AnyNumber());
		auto& cpyExpectation = EXPECT_CALL(m_win32, ReadFile(m_hFile[1].get(), DTGM_ARG4))
								   .Times(t::AnyNumber());
		for (std::uint32_t i = 0; i < srcSize; i += 10) {
			srcExpectation.WillOnce(WITH_LATENCY(10ms, 30ms, Read(0xDEADBEEF)));
		}
		for (std::uint32_t i = 0; i < cpySize; i += 10) {
			if (i + 10 >= std::min(srcSize, cpySize) / 2) {
				cpyExpectation.WillOnce(t::DoDefault());
			} else {
				cpyExpectation.WillOnce(WITH_LATENCY(10ms, 30ms, Read(0xDEADBEEF)));
			}
		}
		if (srcSize % 10) {
			srcExpectation.WillOnce(WITH_LATENCY(10ms, 30ms, Read(0xDEADBEEF, 0.5)));
		}
		cpyExpectation.WillOnce(t::DoDefault());
		srcExpectation.WillOnce(WITH_LATENCY(10ms, 30ms, Eof()));

		EXPECT_THROW(comparer.Compare(Path(kTestFile[0]), Path(kTestFile[1])), m3c::windows_exception);
	}
}

TEST_P(FileComparer_UnequalDataAtStartTest, Compare_UnequalDataAtStart_ReturnFalse) {
	using namespace std::literals::chrono_literals;

	const std::uint32_t srcSize = std::get<0>(GetParam());
	const std::uint32_t cpySize = std::get<1>(GetParam());

	auto& srcExpectation = EXPECT_CALL(m_win32, ReadFile(m_hFile[0].get(), DTGM_ARG4))
							   .Times(t::AnyNumber());
	auto& cpyExpectation = EXPECT_CALL(m_win32, ReadFile(m_hFile[1].get(), DTGM_ARG4))
							   .Times(t::AnyNumber());
	for (std::uint32_t i = 0; i < srcSize; i += 10) {
		srcExpectation.WillOnce(WITH_LATENCY(10ms, 30ms, Read(0xDEADBEEF)));
	}
	for (std::uint32_t i = 0; i < cpySize; i += 10) {
		cpyExpectation.WillOnce(WITH_LATENCY(10ms, 30ms, Read(i ? 0xDEADBEEF : 0xBEEFDEAD)));
	}
	if (srcSize % 10) {
		srcExpectation.WillOnce(WITH_LATENCY(10ms, 30ms, Read(0xDEADBEEF, 0.5)));
	}
	if (cpySize % 10) {
		cpyExpectation.WillOnce(WITH_LATENCY(10ms, 30ms, Read(cpySize < 10 ? 0xBEEFDEAD : 0xDEADBEEF, 0.5)));
	}
	srcExpectation.WillOnce(WITH_LATENCY(10ms, 30ms, Eof()));
	cpyExpectation.WillOnce(WITH_LATENCY(10ms, 30ms, Eof()));

	FileComparer comparer;
	EXPECT_FALSE(comparer.Compare(Path(kTestFile[0]), Path(kTestFile[1])));
}

TEST_P(FileComparer_UnequalDataAtMiddleTest, Compare_UnequalDataAtMiddle_ReturnFalse) {
	using namespace std::literals::chrono_literals;

	const std::uint32_t srcSize = std::get<0>(GetParam());
	const std::uint32_t cpySize = std::get<1>(GetParam());

	auto& srcExpectation = EXPECT_CALL(m_win32, ReadFile(m_hFile[0].get(), DTGM_ARG4))
							   .Times(t::AnyNumber());
	auto& cpyExpectation = EXPECT_CALL(m_win32, ReadFile(m_hFile[1].get(), DTGM_ARG4))
							   .Times(t::AnyNumber());
	for (std::uint32_t i = 0; i < srcSize; i += 10) {
		srcExpectation.WillOnce(WITH_LATENCY(10ms, 30ms, Read(0xDEADBEEF)));
	}
	for (std::uint32_t i = 0; i < cpySize; i += 10) {
		cpyExpectation.WillOnce(WITH_LATENCY(10ms, 30ms, Read(i == (cpySize / 20) * 10 ? 0xBEEFDEAD : 0xDEADBEEF)));
	}
	if (srcSize % 10) {
		srcExpectation.WillOnce(WITH_LATENCY(10ms, 30ms, Read(0xDEADBEEF, 0.5)));
	}
	if (cpySize % 10) {
		srcExpectation.WillOnce(WITH_LATENCY(10ms, 30ms, Read(0xDEADBEEF, 0.5)));
	}
	srcExpectation.WillOnce(WITH_LATENCY(10ms, 30ms, Eof()));
	cpyExpectation.WillOnce(WITH_LATENCY(10ms, 30ms, Eof()));

	FileComparer comparer;
	EXPECT_FALSE(comparer.Compare(Path(kTestFile[0]), Path(kTestFile[1])));
}

TEST_P(FileComparer_UnequalDataAtEndTest, Compare_UnequalDataAtEnd_ReturnFalse) {
	using namespace std::literals::chrono_literals;

	const std::uint32_t srcSize = std::get<0>(GetParam());
	const std::uint32_t cpySize = std::get<1>(GetParam());

	auto& srcExpectation = EXPECT_CALL(m_win32, ReadFile(m_hFile[0].get(), DTGM_ARG4))
							   .Times(t::AnyNumber());
	auto& cpyExpectation = EXPECT_CALL(m_win32, ReadFile(m_hFile[1].get(), DTGM_ARG4))
							   .Times(t::AnyNumber());
	for (std::uint32_t i = 0; i < srcSize; i += 10) {
		srcExpectation.WillOnce(WITH_LATENCY(10ms, 30ms, Read(0xDEADBEEF)));
	}
	for (std::uint32_t i = 0; i < cpySize; i += 10) {
		cpyExpectation.WillOnce(WITH_LATENCY(10ms, 30ms, Read(i + 10 == cpySize ? 0xBEEFDEAD : 0xDEADBEEF)));
	}
	if (srcSize % 10) {
		srcExpectation.WillOnce(WITH_LATENCY(10ms, 30ms, Read(0xDEADBEEF, 0.5)));
	}
	if (cpySize % 10) {
		cpyExpectation.WillOnce(WITH_LATENCY(10ms, 30ms, Read(0xBEEFDEAD, 0.5)));
	}
	srcExpectation.WillOnce(WITH_LATENCY(10ms, 30ms, Eof()));
	cpyExpectation.WillOnce(WITH_LATENCY(10ms, 30ms, Eof()));

	FileComparer comparer;
	EXPECT_FALSE(comparer.Compare(Path(kTestFile[0]), Path(kTestFile[1])));
}

namespace {
auto paramNameGenerator = [](const t::TestParamInfo<FileComparer_BaseTest::ParamType>& param) {
	return fmt::format("{:03}_{}{}_{}{}_{}",
					   param.index,
					   static_cast<std::uint32_t>(std::ceil(std::get<0>(param.param) / 10.0f)),
					   std::get<0>(param.param) && std::get<0>(param.param) % 10 ? "H" : "",
					   static_cast<std::uint32_t>(std::ceil(std::get<1>(param.param) / 10.0f)),
					   std::get<1>(param.param) && std::get<1>(param.param) % 10 ? "H" : "",
					   std::get<2>(param.param) == LatencyMode::kSingle ? "Single" : (std::get<2>(param.param) == LatencyMode::kLatency ? "Latency" : "Repeat"));
};

}

INSTANTIATE_TEST_SUITE_P(FileComparer_EqualDataTest, FileComparer_EqualDataTest, t::Combine(t::Values(0, 5, 10, 15, 20, 25, 50), t::Values(0, 5, 10, 15, 20, 25, 50), t::Values(LatencyMode::kSingle, LatencyMode::kLatency, LatencyMode::kRepeat)), paramNameGenerator);
INSTANTIATE_TEST_SUITE_P(FileComparer_UnequalDataAtStartTest, FileComparer_UnequalDataAtStartTest, t::Combine(t::Values(5, 10, 15, 20, 25, 50), t::Values(5, 10, 15, 20, 25, 50), t::Values(LatencyMode::kSingle, LatencyMode::kLatency)), paramNameGenerator);
INSTANTIATE_TEST_SUITE_P(FileComparer_UnequalDataAtMiddleTest, FileComparer_UnequalDataAtMiddleTest, t::Combine(t::Values(15, 30, 40), t::Values(15, 30, 40), t::Values(LatencyMode::kSingle, LatencyMode::kLatency)), paramNameGenerator);
INSTANTIATE_TEST_SUITE_P(FileComparer_UnequalDataAtEndTest, FileComparer_UnequalDataAtEndTest, t::Combine(t::Values(15, 20, 25, 40), t::Values(15, 20, 25, 40), t::Values(LatencyMode::kSingle, LatencyMode::kLatency)), paramNameGenerator);

}  // namespace systools::test
