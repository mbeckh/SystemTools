#include "systools/FileComparer.h"

#include "systools/Path.h"
#include "systools/Volume.h"

#include <m3c/Handle.h>
#include <m4t/m4t.h>

#include <detours_gmock.h>
#include <winioctl.h>

#include <algorithm>


namespace systools::test {

namespace t = testing;

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

constexpr int kRepeatRuns = 5;

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

class FileComparerBaseTest : public t::Test {
public:
	FileComparerBaseTest() {
		Initialize();
	}

	~FileComparerBaseTest() {
		Deinitialize();
	}

	void Initialize() {
		const m3c::Handle hMutex = CreateMutexW(nullptr, FALSE, nullptr);
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
				.WillByDefault(t::WithoutArgs([ this, i ]() noexcept {
					++m_openHandles;
					SetLastError(0);
					return m_hFile[i].get();
				}));

			ON_CALL(m_win32, ReadFile(m_hFile[i].get(), DTGM_ARG4))
				.WillByDefault(t::InvokeWithoutArgs([]() noexcept {
					SetLastError(ERROR_FILE_INVALID);
					return FALSE;
				}));

			ON_CALL(m_win32, CloseHandle(m_hFile[i].get()))
				.WillByDefault(t::WithoutArgs([this]() noexcept {
					--m_openHandles;
					SetLastError(0);
					return TRUE;
				}));
		}

		EXPECT_CALL(m_volume, GetUnbufferedFileOffsetAlignment())
			.WillRepeatedly(t::ReturnRoundRobin({32u, 512u}));
		EXPECT_CALL(m_volume, GetUnbufferedMemoryAlignment())
			.WillRepeatedly(t::ReturnRoundRobin({static_cast<std::align_val_t>(128), static_cast<std::align_val_t>(4096)}));
	}

	void Deinitialize() {
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
	m3c::Handle m_hFile[sizeof(kTestFile) / sizeof(kTestFile[0])];

private:
	std::atomic_uint32_t m_openHandles = 0;
};

class FileComparerEqualDataTest : public FileComparerBaseTest
	, public t::WithParamInterface<std::tuple<std::uint32_t, std::uint32_t, bool>> {
};

class FileComparerUnequalDataAtStartTest : public FileComparerBaseTest
	, public t::WithParamInterface<std::tuple<std::uint32_t, std::uint32_t>> {
};

class FileComparerUnequalDataAtMiddleTest : public FileComparerBaseTest
	, public t::WithParamInterface<std::tuple<std::uint32_t, std::uint32_t>> {
};

class FileComparerUnequalDataAtEndTest : public FileComparerBaseTest
	, public t::WithParamInterface<std::tuple<std::uint32_t, std::uint32_t>> {
};

//
// Equal
//

TEST_P(FileComparerEqualDataTest, Compare_EqualData_ReturnResult) {
	const std::uint32_t srcSize = std::get<0>(GetParam());
	const std::uint32_t cpySize = std::get<1>(GetParam());
	const int maxRuns = std::get<2>(GetParam()) ? kRepeatRuns : 1;

	FileComparer comparer;
	for (int runs = 0; runs < maxRuns; ++runs) {
		auto& srcExpectation = EXPECT_CALL(m_win32, ReadFile(m_hFile[0].get(), DTGM_ARG4))
								   .Times(t::AnyNumber());
		auto& cpyExpectation = EXPECT_CALL(m_win32, ReadFile(m_hFile[1].get(), DTGM_ARG4))
								   .Times(t::AnyNumber());
		for (std::uint32_t i = 0; i < srcSize; i += 10) {
			srcExpectation.WillOnce(Read(0xDEADBEEF));
		}
		for (std::uint32_t i = 0; i < cpySize; i += 10) {
			cpyExpectation.WillOnce(Read(0xDEADBEEF));
		}
		if (srcSize % 10) {
			srcExpectation.WillOnce(Read(0xDEADBEEF, 0.5));
		}
		if (cpySize % 10) {
			cpyExpectation.WillOnce(Read(0xDEADBEEF, 0.5));
		}
		srcExpectation.WillOnce(Eof());
		cpyExpectation.WillOnce(Eof());

		EXPECT_EQ(srcSize == cpySize, comparer.Compare(Path(kTestFile[0]), Path(kTestFile[1])));
	}
}

TEST_P(FileComparerEqualDataTest, Compare_ReadError_ThrowException) {
	const std::uint32_t srcSize = std::get<0>(GetParam());
	const std::uint32_t cpySize = std::get<1>(GetParam());
	const int maxRuns = std::get<2>(GetParam()) ? kRepeatRuns : 1;

	FileComparer comparer;
	for (int runs = 0; runs < maxRuns; ++runs) {
		auto& srcExpectation = EXPECT_CALL(m_win32, ReadFile(m_hFile[0].get(), DTGM_ARG4))
								   .Times(t::AnyNumber());
		auto& cpyExpectation = EXPECT_CALL(m_win32, ReadFile(m_hFile[1].get(), DTGM_ARG4))
								   .Times(t::AnyNumber());
		for (std::uint32_t i = 0; i < srcSize; i += 10) {
			srcExpectation.WillOnce(Read(0xDEADBEEF));
		}
		for (std::uint32_t i = 0; i < cpySize; i += 10) {
			if (i + 10 >= std::min(srcSize, cpySize) / 2) {
				cpyExpectation.WillOnce(t::DoDefault());
			} else {
				cpyExpectation.WillOnce(Read(0xDEADBEEF));
			}
		}
		if (srcSize % 10) {
			srcExpectation.WillOnce(Read(0xDEADBEEF, 0.5));
		}
		if (cpySize % 10) {
			cpyExpectation.WillOnce(t::DoDefault());
		}
		srcExpectation.WillOnce(Eof());
		cpyExpectation.WillOnce(t::DoDefault());

		EXPECT_THROW(comparer.Compare(Path(kTestFile[0]), Path(kTestFile[1])), m3c::windows_exception);
	}
}

TEST_P(FileComparerUnequalDataAtStartTest, Compare_UnequalDataAtStart_ReturnFalse) {
	const std::uint32_t srcSize = std::get<0>(GetParam());
	const std::uint32_t cpySize = std::get<1>(GetParam());

	auto& srcExpectation = EXPECT_CALL(m_win32, ReadFile(m_hFile[0].get(), DTGM_ARG4))
							   .Times(t::AnyNumber());
	auto& cpyExpectation = EXPECT_CALL(m_win32, ReadFile(m_hFile[1].get(), DTGM_ARG4))
							   .Times(t::AnyNumber());
	for (std::uint32_t i = 0; i < srcSize; i += 10) {
		srcExpectation.WillOnce(Read(0xDEADBEEF));
	}
	for (std::uint32_t i = 0; i < cpySize; i += 10) {
		cpyExpectation.WillOnce(Read(i ? 0xDEADBEEF : 0xBEEFDEAD));
	}
	if (srcSize % 10) {
		srcExpectation.WillOnce(Read(0xDEADBEEF, 0.5));
	}
	if (cpySize % 10) {
		cpyExpectation.WillOnce(Read(cpySize < 10 ? 0xBEEFDEAD : 0xDEADBEEF, 0.5));
	}
	srcExpectation.WillOnce(Eof());
	cpyExpectation.WillOnce(Eof());

	FileComparer comparer;
	EXPECT_FALSE(comparer.Compare(Path(kTestFile[0]), Path(kTestFile[1])));
}

TEST_P(FileComparerUnequalDataAtMiddleTest, Compare_UnequalDataAtMiddle_ReturnFalse) {
	const std::uint32_t srcSize = std::get<0>(GetParam());
	const std::uint32_t cpySize = std::get<1>(GetParam());

	auto& srcExpectation = EXPECT_CALL(m_win32, ReadFile(m_hFile[0].get(), DTGM_ARG4))
							   .Times(t::AnyNumber());
	auto& cpyExpectation = EXPECT_CALL(m_win32, ReadFile(m_hFile[1].get(), DTGM_ARG4))
							   .Times(t::AnyNumber());
	for (std::uint32_t i = 0; i < srcSize; i += 10) {
		srcExpectation.WillOnce(Read(0xDEADBEEF));
	}
	for (std::uint32_t i = 0; i < cpySize; i += 10) {
		cpyExpectation.WillOnce(Read(i == (cpySize / 20) * 10 ? 0xBEEFDEAD : 0xDEADBEEF));
	}
	if (srcSize % 10) {
		srcExpectation.WillOnce(Read(0xDEADBEEF, 0.5));
	}
	if (cpySize % 10) {
		srcExpectation.WillOnce(Read(0xDEADBEEF, 0.5));
	}
	srcExpectation.WillOnce(Eof());
	cpyExpectation.WillOnce(Eof());

	FileComparer comparer;
	EXPECT_FALSE(comparer.Compare(Path(kTestFile[0]), Path(kTestFile[1])));
}

TEST_P(FileComparerUnequalDataAtEndTest, Compare_UnequalDataAtEnd_ReturnFalse) {
	const std::uint32_t srcSize = std::get<0>(GetParam());
	const std::uint32_t cpySize = std::get<1>(GetParam());

	auto& srcExpectation = EXPECT_CALL(m_win32, ReadFile(m_hFile[0].get(), DTGM_ARG4))
							   .Times(t::AnyNumber());
	auto& cpyExpectation = EXPECT_CALL(m_win32, ReadFile(m_hFile[1].get(), DTGM_ARG4))
							   .Times(t::AnyNumber());
	for (std::uint32_t i = 0; i < srcSize; i += 10) {
		srcExpectation.WillOnce(Read(0xDEADBEEF));
	}
	for (std::uint32_t i = 0; i < cpySize; i += 10) {
		cpyExpectation.WillOnce(Read(i + 10 == cpySize ? 0xBEEFDEAD : 0xDEADBEEF));
	}
	if (srcSize % 10) {
		srcExpectation.WillOnce(Read(0xDEADBEEF, 0.5));
	}
	if (cpySize % 10) {
		cpyExpectation.WillOnce(Read(0xBEEFDEAD, 0.5));
	}
	srcExpectation.WillOnce(Eof());
	cpyExpectation.WillOnce(Eof());

	FileComparer comparer;
	EXPECT_FALSE(comparer.Compare(Path(kTestFile[0]), Path(kTestFile[1])));
}

INSTANTIATE_TEST_SUITE_P(Size, FileComparerEqualDataTest, t::Combine(t::Values(0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 500, 505), t::Values(0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 500, 505), t::Bool()));
INSTANTIATE_TEST_SUITE_P(Size, FileComparerUnequalDataAtStartTest, t::Combine(t::Values(5, 10, 15, 20, 25, 30, 35, 40, 45, 50), t::Values(5, 10, 15, 20, 25, 30, 35, 40, 45, 50)));
INSTANTIATE_TEST_SUITE_P(Size, FileComparerUnequalDataAtMiddleTest, t::Combine(t::Values(15, 20, 25, 30, 35, 40, 45, 50), t::Values(15, 20, 25, 30, 35, 40, 45, 50)));
INSTANTIATE_TEST_SUITE_P(Size, FileComparerUnequalDataAtEndTest, t::Combine(t::Values(10, 15, 20, 25, 30, 35, 40, 45, 50), t::Values(10, 15, 20, 25, 30, 35, 40, 45, 50)));

}  // namespace systools::test
