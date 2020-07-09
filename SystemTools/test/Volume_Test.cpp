#include "systools/Volume.h"

#include "TestUtils.h"
#include "systools/Path.h"

#include <m3c/handle.h>
#include <m4t/m4t.h>

#include <detours_gmock.h>
#include <strsafe.h>
#include <winioctl.h>

#include <atomic>
#include <regex>


namespace systools::test {

namespace t = testing;
namespace dtgm = detours_gmock;

#define WIN32_FUNCTIONS(fn_)                                                                                                                                                                       \
	fn_(3, BOOL, WINAPI, GetVolumePathNameW,                                                                                                                                                       \
		(LPCWSTR lpszFileName, LPWSTR lpszVolumePathName, DWORD cchBufferLength),                                                                                                                  \
		(lpszFileName, lpszVolumePathName, cchBufferLength),                                                                                                                                       \
		nullptr);                                                                                                                                                                                  \
	fn_(3, BOOL, WINAPI, GetVolumeNameForVolumeMountPointW,                                                                                                                                        \
		(LPCWSTR lpszVolumeMountPoint, LPWSTR lpszVolumeName, DWORD cchBufferLength),                                                                                                              \
		(lpszVolumeMountPoint, lpszVolumeName, cchBufferLength),                                                                                                                                   \
		nullptr);                                                                                                                                                                                  \
	fn_(7, HANDLE, WINAPI, CreateFileW,                                                                                                                                                            \
		(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile), \
		(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile),                                                              \
		nullptr);                                                                                                                                                                                  \
	fn_(1, BOOL, WINAPI, CloseHandle,                                                                                                                                                              \
		(HANDLE hObject),                                                                                                                                                                          \
		(hObject),                                                                                                                                                                                 \
		nullptr);                                                                                                                                                                                  \
	fn_(8, BOOL, WINAPI, DeviceIoControl,                                                                                                                                                          \
		(HANDLE hDevice, DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize, LPVOID lpOutBuffer, DWORD nOutBufferSize, LPDWORD lpBytesReturned, LPOVERLAPPED lpOverlapped),             \
		(hDevice, dwIoControlCode, lpInBuffer, nInBufferSize, lpOutBuffer, nOutBufferSize, lpBytesReturned, lpOverlapped),                                                                         \
		nullptr);

DTGM_DECLARE_API_MOCK(Win32, WIN32_FUNCTIONS);

namespace {

const std::wstring kTestVolume(LR"(\\?\Volume{23220209-1205-1000-8000-0000000001})");

const std::wstring kVolumePattern = LR"((\\\\\?\\Volume\{23220209-1205-1000-8000-000000000[12]\}))";
constexpr std::wregex::flag_type kRegexOptions = std::regex_constants::ECMAScript | std::regex_constants::icase | std::regex_constants::optimize;

const std::wregex kVolumeRegex(kVolumePattern, kRegexOptions | std::regex_constants::nosubs);
const std::wregex kVolumePathRegex(kVolumePattern + LR"(\\.*)", kRegexOptions);
const std::wregex kVolumeMointPointRegex(kVolumePattern + LR"(\\)", kRegexOptions | std::regex_constants::nosubs);

}  // namespace

class Volume_Test : public t::Test {
protected:
	void SetUp() override {
		ON_CALL(m_win32, GetVolumePathNameW(m4t::MatchesRegex(kVolumePathRegex), DTGM_ARG2))
			.WillByDefault([](const wchar_t* const filename, wchar_t* const volumePathName, const DWORD bufferLength) noexcept {
				std::wcmatch results;
				if (!std::regex_match(filename, results, kVolumePathRegex)) {
					SetLastError(ERROR_FILE_INVALID);
					return FALSE;
				}
				HRESULT hr = StringCchCopyW(volumePathName, bufferLength, results.str(1).c_str());
				if (FAILED(hr)) {
					SetLastError(hr);
					return FALSE;
				}
				hr = StringCchCatW(volumePathName, bufferLength, L"\\");
				if (FAILED(hr)) {
					SetLastError(hr);
					return FALSE;
				}
				return TRUE;
			});

		ON_CALL(m_win32, GetVolumeNameForVolumeMountPointW(m4t::MatchesRegex(kVolumeMointPointRegex), DTGM_ARG2))
			.WillByDefault([](const wchar_t* const volumeMountPoint, wchar_t* const volumeName, const DWORD bufferLength) noexcept {
				const HRESULT hr = StringCchCopyW(volumeName, bufferLength, volumeMountPoint);
				SetLastError(hr);
				return static_cast<BOOL>(SUCCEEDED(hr));
			});

		const m3c::handle hMutex = CreateMutexW(nullptr, FALSE, nullptr);
		if (!hMutex) {
			THROW(m3c::windows_exception(GetLastError()), "CreateMutex");
		}

		HANDLE handle;
		if (!DuplicateHandle(GetCurrentProcess(), hMutex, GetCurrentProcess(), &handle, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
			THROW(m3c::windows_exception(GetLastError()), "DuplicateHandle");
		}
		m_hVolume = handle;

		ON_CALL(m_win32, CreateFileW(t::Eq(kTestVolume), DTGM_ARG6))
			.WillByDefault(t::WithoutArgs([this]() noexcept {
				++m_openHandles;
				return m_hVolume.get();
			}));

		ON_CALL(m_win32, DeviceIoControl(m_hVolume.get(), IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, DTGM_ARG6))
			.WillByDefault([this](t::Unused, t::Unused, t::Unused, t::Unused, void* const outBuffer, const DWORD outBufferSize, DWORD* const pBytesReturned, t::Unused) noexcept {
				if (outBufferSize < sizeof(VOLUME_DISK_EXTENTS)) {
					SetLastError(ERROR_MORE_DATA);
					return FALSE;
				}
				VOLUME_DISK_EXTENTS* pExtents = reinterpret_cast<VOLUME_DISK_EXTENTS*>(outBuffer);
				ZeroMemory(pExtents, sizeof(VOLUME_DISK_EXTENTS));
				pExtents->NumberOfDiskExtents = 1;
				pExtents->Extents[0].DiskNumber = 0x1000;
				*pBytesReturned = sizeof(VOLUME_DISK_EXTENTS);
				return TRUE;
			});

		if (!DuplicateHandle(GetCurrentProcess(), hMutex, GetCurrentProcess(), &handle, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
			THROW(m3c::windows_exception(GetLastError()), "DuplicateHandle");
		}
		m_hDevice = handle;

		ON_CALL(m_win32, CreateFileW(t::Eq(fmt::format(LR"(\\.\PhysicalDrive{})", 0x1000)), DTGM_ARG6))
			.WillByDefault(t::WithoutArgs([this]() noexcept {
				++m_openHandles;
				return m_hDevice.get();
			}));

		ON_CALL(m_win32, DeviceIoControl(m_hDevice.get(), IOCTL_STORAGE_QUERY_PROPERTY, DTGM_ARG6))
			.WillByDefault([this](t::Unused, t::Unused, t::Unused, t::Unused, void* const outBuffer, const DWORD outBufferSize, DWORD* const pBytesReturned, t::Unused) noexcept {
				if (outBufferSize < sizeof(STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR)) {
					SetLastError(ERROR_MORE_DATA);
					return FALSE;
				}

				STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR* pAlignment = reinterpret_cast<STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR*>(outBuffer);
				ZeroMemory(pAlignment, sizeof(STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR));
				pAlignment->Version = sizeof(STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR);
				pAlignment->Size = sizeof(STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR);
				pAlignment->BytesPerCacheLine = 15;
				pAlignment->BytesOffsetForCacheAlignment = 3;
				pAlignment->BytesPerLogicalSector = 31;
				pAlignment->BytesPerPhysicalSector = 127;
				pAlignment->BytesOffsetForSectorAlignment = 7;
				*pBytesReturned = sizeof(STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR);
				return TRUE;
			});

		ON_CALL(m_win32, CloseHandle(t::AnyOf(m_hVolume.get(), m_hDevice.get())))
			.WillByDefault(t::WithoutArgs([this]() noexcept {
				--m_openHandles;
				return TRUE;
			}));
	}

	void TearDown() {
		EXPECT_EQ(0u, m_openHandles);
		t::Mock::VerifyAndClearExpectations(&m_win32);
		DTGM_DETACH_API_MOCK(Win32);
	}

protected:
	DTGM_DEFINE_API_MOCK(Win32, m_win32);
	m3c::handle m_hVolume;
	m3c::handle m_hDevice;

private:
	std::atomic_uint32_t m_openHandles = 0;
};

TEST_F(Volume_Test, GetUnbufferedFileOffsetAlignment_SystemVolume_GetResult) {
	Volume volume(TestUtils::GetSystemDirectory());

	const std::uint32_t result = volume.GetUnbufferedFileOffsetAlignment();

	// expect some power-of-2-value
	EXPECT_EQ(1, PopulationCount64(result));
}

TEST_F(Volume_Test, GetUnbufferedFileOffsetAlignment_TestVolume_GetResult) {
	Volume volume(Path(kTestVolume + LR"(\foo.bar)"));

	const std::uint32_t result = volume.GetUnbufferedFileOffsetAlignment();

	EXPECT_EQ(31u, result);
}
TEST_F(Volume_Test, GetUnbufferedMemoryAlignment_SystemVolume_GetResult) {
	Volume volume(TestUtils::GetSystemDirectory());

	const std::align_val_t result = volume.GetUnbufferedMemoryAlignment();

	// expect some power-of-2-value
	EXPECT_EQ(1, PopulationCount64(static_cast<DWORD64>(result)));
}

TEST_F(Volume_Test, GetUnbufferedMemoryAlignment_TestVolume_GetResult) {
	Volume volume(Path(kTestVolume + LR"(\foo.bar)"));

	const std::align_val_t result = volume.GetUnbufferedMemoryAlignment();

	EXPECT_EQ(static_cast<std::align_val_t>(127), result);
}

}  // namespace systools::test
