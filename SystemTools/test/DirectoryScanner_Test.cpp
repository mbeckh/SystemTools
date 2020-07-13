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

#include "systools/DirectoryScanner.h"

#include "TestUtils.h"

#include <m3c/Exception.h>
#include <m3c/Handle.h>
#include <m4t/m4t.h>

#include <systools/Path.h>

#include <aclapi.h>
#include <detours_gmock.h>
#include <sddl.h>
#include <strsafe.h>

#include <chrono>
#include <tuple>

namespace systools::test {

namespace t = testing;
namespace dtgm = detours_gmock;

#define WIN32_FUNCTIONS(fn_)                                                                                                                                                                                  \
	fn_(7, HANDLE, WINAPI, CreateFileW,                                                                                                                                                                       \
		(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile),            \
		(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile),                                                                         \
		nullptr);                                                                                                                                                                                             \
	fn_(4, BOOL, WINAPI, GetFileInformationByHandleEx,                                                                                                                                                        \
		(HANDLE hFile, FILE_INFO_BY_HANDLE_CLASS FileInformationClass, LPVOID lpFileInformation, DWORD dwBufferSize),                                                                                         \
		(hFile, FileInformationClass, lpFileInformation, dwBufferSize),                                                                                                                                       \
		nullptr);                                                                                                                                                                                             \
	fn_(4, HANDLE, WINAPI, FindFirstStreamW,                                                                                                                                                                  \
		(LPCWSTR lpFileName, STREAM_INFO_LEVELS InfoLevel, LPVOID lpFindStreamData, DWORD dwFlags),                                                                                                           \
		(lpFileName, InfoLevel, lpFindStreamData, dwFlags),                                                                                                                                                   \
		nullptr);                                                                                                                                                                                             \
	fn_(2, BOOL, WINAPI, FindNextStreamW,                                                                                                                                                                     \
		(HANDLE hFindStream, LPVOID lpFindStreamData),                                                                                                                                                        \
		(hFindStream, lpFindStreamData),                                                                                                                                                                      \
		nullptr);                                                                                                                                                                                             \
	fn_(1, DWORD, WINAPI, GetFileAttributesW,                                                                                                                                                                 \
		(LPCWSTR lpFileName),                                                                                                                                                                                 \
		(lpFileName),                                                                                                                                                                                         \
		nullptr);                                                                                                                                                                                             \
	fn_(8, DWORD, WINAPI, GetNamedSecurityInfoW,                                                                                                                                                              \
		(LPCWSTR pObjectName, SE_OBJECT_TYPE ObjectType, SECURITY_INFORMATION SecurityInfo, PSID * ppsidOwner, PSID * ppsidGroup, PACL * ppDacl, PACL * ppSacl, PSECURITY_DESCRIPTOR * ppSecurityDescriptor), \
		(pObjectName, ObjectType, SecurityInfo, ppsidOwner, ppsidGroup, ppDacl, ppSacl, ppSecurityDescriptor),                                                                                                \
		nullptr);                                                                                                                                                                                             \
	fn_(1, BOOL, WINAPI, CloseHandle,                                                                                                                                                                         \
		(HANDLE hObject),                                                                                                                                                                                     \
		(hObject),                                                                                                                                                                                            \
		nullptr);                                                                                                                                                                                             \
	fn_(1, BOOL, WINAPI, FindClose,                                                                                                                                                                           \
		(HANDLE hFindFile),                                                                                                                                                                                   \
		(hFindFile),                                                                                                                                                                                          \
		nullptr);

DTGM_DECLARE_API_MOCK(Win32, WIN32_FUNCTIONS);

namespace {

#pragma warning(suppress : 4100)
MATCHER(ScannedFileHasName, "") {
	static_assert(std::is_convertible_v<arg_type, std::tuple<const ScannedFile &, const std::wstring &>>);

	return std::get<0>(arg).GetName().c_str() == std::get<1>(arg);
}

#pragma warning(suppress : 4100)
MATCHER_P3(ScannedFileIdMatches, first, lower, upper, "") {
	static_assert(std::is_convertible_v<arg_type, const ScannedFile &>);
	static_assert(std::is_convertible_v<first_type, std::uint8_t>);
	static_assert(std::is_convertible_v<lower_type, std::uint16_t>);
	static_assert(std::is_convertible_v<upper_type, std::uint16_t>);

	if (arg.GetFileId().Identifier[0] != first) {
		return false;
	}
	for (std::uint_fast8_t i = 1; i < 14; ++i) {
		if (arg.GetFileId().Identifier[i]) {
			return false;
		}
	}
	const std::uint16_t id = arg.GetFileId().Identifier[14] | (arg.GetFileId().Identifier[15] << 8);
	return id >= lower && id <= upper;
}

#pragma warning(suppress : 4100)
MATCHER_P(ScannedFileHasAttribute, attribute, "") {
	static_assert(std::is_convertible_v<arg_type, const ScannedFile &>);
	static_assert(std::is_convertible_v<attribute_type, DWORD>);

	return arg.GetAttributes() & attribute;
}

#pragma warning(suppress : 4100)
MATCHER_P(ScannedFileHasAtLeastStreams, count, "") {
	static_assert(std::is_convertible_v<arg_type, const ScannedFile &>);

	return arg.GetStreams().size() >= count;
}

#pragma warning(suppress : 4100)
MATCHER(ScannedFileHasSecurity, "") {
	static_assert(std::is_convertible_v<arg_type, const ScannedFile &>);

	return !!arg.GetSecurity().pSecurityDescriptor;
}

enum class Flags {
	kNone,
	kFolderStreams,
	kFolderSecurity,
	kFileSecurity
};

const auto LocalFreeDelete = [](void *ptr) noexcept {
	if (LocalFree(ptr)) {
		SLOG_ERROR("LocalFree: {}", lg::LastError());
	}
};

void FillSecurity(ScannedFile::Security &security, const char *const securityString) {
	PSECURITY_DESCRIPTOR pSecurityDescriptor;
	ASSERT_TRUE(ConvertStringSecurityDescriptorToSecurityDescriptorA(securityString, SDDL_REVISION_1, &pSecurityDescriptor, nullptr));
	security.pSecurityDescriptor.reset(pSecurityDescriptor, LocalFreeDelete);

	BOOL defaulted;
	ASSERT_TRUE(GetSecurityDescriptorOwner(pSecurityDescriptor, &security.pOwner, &defaulted));
	ASSERT_TRUE(GetSecurityDescriptorGroup(pSecurityDescriptor, &security.pGroup, &defaulted));

	BOOL present;
	ASSERT_TRUE(GetSecurityDescriptorDacl(pSecurityDescriptor, &present, &security.pDacl, &defaulted));
	ASSERT_TRUE(GetSecurityDescriptorSacl(pSecurityDescriptor, &present, &security.pSacl, &defaulted));
}

ScannedFile::Security CreateSecurity(const char *const securityString) {
	ScannedFile::Security security;
	FillSecurity(security, securityString);
	return security;
}

}  // namespace

class DirectoryScanner_Test : public t::TestWithParam<std::tuple<std::uint16_t, std::uint16_t, LatencyMode, Flags>>
	, public WithLatency {
public:
	DirectoryScanner_Test()
		: WithLatency(std::get<2>(GetParam())) {
		// empty
	}

protected:
	void SetUp() override {
		using namespace std::literals::chrono_literals;

		const m3c::Handle hMutex = CreateMutexW(nullptr, FALSE, nullptr);
		if (!hMutex) {
			THROW(m3c::windows_exception(GetLastError()), "CreateMutex");
		}

		HANDLE handle;
		if (!DuplicateHandle(GetCurrentProcess(), hMutex, GetCurrentProcess(), &handle, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
			THROW(m3c::windows_exception(GetLastError()), "DuplicateHandle");
		}
		m_hDirectory = handle;

		if (!DuplicateHandle(GetCurrentProcess(), hMutex, GetCurrentProcess(), &handle, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
			THROW(m3c::windows_exception(GetLastError()), "DuplicateHandle");
		}
		m_hFind = handle;

		ON_CALL(m_win32, CreateFileW(t::Eq(m_path), DTGM_ARG6))
			.WillByDefault(WITH_LATENCY(5ms, 15ms, t::WithoutArgs([this]() noexcept {
											++m_openHandles;
											return m_hDirectory.get();
										})));

		ON_CALL(m_win32, GetFileInformationByHandleEx(m_hDirectory.get(), FileIdExtdDirectoryInfo, DTGM_ARG2))
			.WillByDefault(t::InvokeWithoutArgs([]() noexcept {
				SetLastError(ERROR_FILE_INVALID);
				return FALSE;
			}));

		ON_CALL(m_win32, FindFirstStreamW(t::StartsWith(m_path), DTGM_ARG3))
			.WillByDefault(t::InvokeWithoutArgs([]() noexcept {
				SetLastError(ERROR_FILE_INVALID);
				return INVALID_HANDLE_VALUE;
			}));

		ON_CALL(m_win32, FindNextStreamW(m_hFind.get(), t::_))
			.WillByDefault(t::InvokeWithoutArgs([]() noexcept {
				SetLastError(ERROR_FILE_INVALID);
				return FALSE;
			}));

		ON_CALL(m_win32, GetFileAttributesW(t::StartsWith(m_path)))
			.WillByDefault(t::InvokeWithoutArgs([]() noexcept {
				SetLastError(ERROR_FILE_INVALID);
				return INVALID_FILE_ATTRIBUTES;
			}));

		ON_CALL(m_win32, GetNamedSecurityInfoW(t::StartsWith(m_path), SE_FILE_OBJECT, DTGM_ARG6))
			.WillByDefault(t::InvokeWithoutArgs([]() noexcept {
				return ERROR_FILE_INVALID;
			}));

		ON_CALL(m_win32, CloseHandle(m_hDirectory.get()))
			.WillByDefault(WITH_LATENCY(1ms, 5ms, t::WithoutArgs([this]() noexcept {
											--m_openHandles;
											return TRUE;
										})));

		ON_CALL(m_win32, FindClose(m_hFind.get()))
			.WillByDefault(t::WithoutArgs([this]() noexcept {
				--m_openHandles;
				return TRUE;
			}));
	}

	void TearDown() override {
		EXPECT_EQ(0u, m_openHandles);
		t::Mock::VerifyAndClearExpectations(&m_win32);
		DTGM_DETACH_API_MOCK(Win32);
	}

protected:
	DTGM_DEFINE_API_MOCK(Win32, m_win32);
	const std::wstring m_path = LR"(\\?\Volume{23220209-1205-1000-8000-0000000001}\test)";
	m3c::Handle m_hDirectory;
	m3c::Handle m_hFind;
	std::atomic_uint32_t m_openHandles = 0;
};

using DirectoryScanner_ErrorTest = DirectoryScanner_Test;


//
// ScannedFile::Stream
//

TEST(ScannedFile_Stream_Test, opCompare_Identity_Equal) {
	const ScannedFile::Stream stream(ScannedFile::Stream::name_type(L":foo:$DATA"), {.QuadPart = 9}, FILE_ATTRIBUTE_NORMAL);

	EXPECT_TRUE(stream == stream);
	EXPECT_FALSE(stream != stream);
}

TEST(ScannedFile_Stream_Test, opCompare_Equal_Equal) {
	const ScannedFile::Stream stream(ScannedFile::Stream::name_type(L":foo:$DATA"), {.QuadPart = 9}, FILE_ATTRIBUTE_NORMAL);
	const ScannedFile::Stream oth(ScannedFile::Stream::name_type(L":foo:$DATA"), {.QuadPart = 9}, FILE_ATTRIBUTE_NORMAL);

	EXPECT_TRUE(stream == oth);
	EXPECT_FALSE(stream != oth);
}

TEST(ScannedFile_Stream_Test, opCompare_Name_NotEqual) {
	const ScannedFile::Stream stream(ScannedFile::Stream::name_type(L":foo:$DATA"), {.QuadPart = 9}, FILE_ATTRIBUTE_NORMAL);
	const ScannedFile::Stream oth(ScannedFile::Stream::name_type(L":bar:$DATA"), {.QuadPart = 9}, FILE_ATTRIBUTE_NORMAL);

	EXPECT_FALSE(stream == oth);
	EXPECT_TRUE(stream != oth);
}

TEST(ScannedFile_Stream_Test, opCompare_Size_NotEqual) {
	const ScannedFile::Stream stream(ScannedFile::Stream::name_type(L":foo:$DATA"), {.QuadPart = 9}, FILE_ATTRIBUTE_NORMAL);
	const ScannedFile::Stream oth(ScannedFile::Stream::name_type(L":foo:$DATA"), {.QuadPart = 10}, FILE_ATTRIBUTE_NORMAL);

	EXPECT_FALSE(stream == oth);
	EXPECT_TRUE(stream != oth);
}

TEST(ScannedFile_Stream_Test, opCompare_Attributes_NotEqual) {
	const ScannedFile::Stream stream(ScannedFile::Stream::name_type(L":foo:$DATA"), {.QuadPart = 9}, FILE_ATTRIBUTE_NORMAL);
	const ScannedFile::Stream oth(ScannedFile::Stream::name_type(L":foo:$DATA"), {.QuadPart = 9}, FILE_ATTRIBUTE_READONLY);

	EXPECT_FALSE(stream == oth);
	EXPECT_TRUE(stream != oth);
}


//
// ScannedFile::Security
//

TEST(ScannedFile_Security_Test, opCompare_Identity_Equal) {
	const ScannedFile::Security security = CreateSecurity("O:BAG:BAD:(A;;FR;;;CO)");

	EXPECT_TRUE(security == security);
	EXPECT_FALSE(security != security);
}

TEST(ScannedFile_Security_Test, opCompare_Equal_Equal) {
	const ScannedFile::Security security = CreateSecurity("O:BAG:BAD:(A;;FR;;;CO)S:(OU;SAFA;GACCFA;;;AU)");
	const ScannedFile::Security oth = CreateSecurity("O:BAG:BAD:(A;;FR;;;CO)S:(OU;SAFA;GACCFA;;;AU)");

	EXPECT_TRUE(security == oth);
	EXPECT_FALSE(security != oth);
}

TEST(ScannedFile_Security_Test, opCompare_Owner_NotEqual) {
	const ScannedFile::Security security = CreateSecurity("O:BAG:BAD:(A;;FR;;;CO)");
	const ScannedFile::Security oth = CreateSecurity("O:BUG:BAD:(A;;FR;;;CO)");

	EXPECT_FALSE(security == oth);
	EXPECT_TRUE(security != oth);
}

TEST(ScannedFile_Security_Test, opCompare_OwnerEmpty_NotEqual) {
	const ScannedFile::Security security = CreateSecurity("O:BAG:BAD:(A;;FR;;;CO)");
	const ScannedFile::Security oth = CreateSecurity("G:BAD:(A;;FR;;;CO)");

	EXPECT_FALSE(security == oth);
	EXPECT_TRUE(security != oth);
}

TEST(ScannedFile_Security_Test, opCompare_OwnerEmptyBoth_Equal) {
	const ScannedFile::Security security = CreateSecurity("G:BAD:(A;;FR;;;CO)");
	const ScannedFile::Security oth = CreateSecurity("G:BAD:(A;;FR;;;CO)");

	EXPECT_TRUE(security == oth);
	EXPECT_FALSE(security != oth);
}

TEST(ScannedFile_Security_Test, opCompare_Group_NotEqual) {
	const ScannedFile::Security security = CreateSecurity("O:BAG:BAD:(A;;FR;;;CO)");
	const ScannedFile::Security oth = CreateSecurity("O:BAG:BUD:(A;;FR;;;CO)");

	EXPECT_FALSE(security == oth);
	EXPECT_TRUE(security != oth);
}

TEST(ScannedFile_Security_Test, opCompare_GroupEmpty_NotEqual) {
	const ScannedFile::Security security = CreateSecurity("O:BAG:BAD:(A;;FR;;;CO)");
	const ScannedFile::Security oth = CreateSecurity("O:BAD:(A;;FR;;;CO)");

	EXPECT_FALSE(security == oth);
	EXPECT_TRUE(security != oth);
}

TEST(ScannedFile_Security_Test, opCompare_GroupEmptyBoth_Equal) {
	const ScannedFile::Security security = CreateSecurity("O:BAD:(A;;FR;;;CO)");
	const ScannedFile::Security oth = CreateSecurity("O:BAD:(A;;FR;;;CO)");

	EXPECT_TRUE(security == oth);
	EXPECT_FALSE(security != oth);
}

TEST(ScannedFile_Security_Test, opCompare_DaclType_NotEqual) {
	const ScannedFile::Security security = CreateSecurity("O:BAG:BAD:(A;;FR;;;CO)");
	const ScannedFile::Security oth = CreateSecurity("O:BAG:BAD:(D;;FR;;;CO)");

	EXPECT_FALSE(security == oth);
	EXPECT_TRUE(security != oth);
}

TEST(ScannedFile_Security_Test, opCompare_DaclFlags_NotEqual) {
	const ScannedFile::Security security = CreateSecurity("O:BAG:BAD:(A;;FR;;;CO)");
	const ScannedFile::Security oth = CreateSecurity("O:BAG:BAD:(A;NP;FR;;;CO)");

	EXPECT_FALSE(security == oth);
	EXPECT_TRUE(security != oth);
}

TEST(ScannedFile_Security_Test, opCompare_DaclRights_NotEqual) {
	const ScannedFile::Security security = CreateSecurity("O:BAG:BAD:(A;;FR;;;CO)");
	const ScannedFile::Security oth = CreateSecurity("O:BAG:BAD:(A;;FRFW;;;CO)");

	EXPECT_FALSE(security == oth);
	EXPECT_TRUE(security != oth);
}

TEST(ScannedFile_Security_Test, opCompare_DaclRightsAdditional_NotEqual) {
	const ScannedFile::Security security = CreateSecurity("O:BAG:BAD:(A;;FRFW;;;CO)");
	const ScannedFile::Security oth = CreateSecurity("O:BAG:BAD:(A;;FR;;;CO)(A;;FW;;;CO)");

	EXPECT_FALSE(security == oth);
	EXPECT_TRUE(security != oth);
}

TEST(ScannedFile_Security_Test, opCompare_DaclSid_NotEqual) {
	const ScannedFile::Security security = CreateSecurity("O:BAG:BAD:(A;;FR;;;CO)");
	const ScannedFile::Security oth = CreateSecurity("O:BAG:BAD:(A;;FR;;;BU)");

	EXPECT_FALSE(security == oth);
	EXPECT_TRUE(security != oth);
}

TEST(ScannedFile_Security_Test, opCompare_DaclEmpty_NotEqual) {
	const ScannedFile::Security security = CreateSecurity("O:BAG:BAD:(A;;FR;;;CO)");
	const ScannedFile::Security oth = CreateSecurity("O:BAG:BA");

	EXPECT_FALSE(security == oth);
	EXPECT_TRUE(security != oth);
}

// TEST(ScannedFile_Security_Test, opCompare_SaclType_NotEqual) {
// changes in type seem to require changes in other data too
TEST(ScannedFile_Security_Test, opCompare_SaclFlags_NotEqual) {
	const ScannedFile::Security security = CreateSecurity("O:BAG:BAS:(OU;SAFA;GACCFA;;;AU)");
	const ScannedFile::Security oth = CreateSecurity("O:BAG:BAS:(OU;FA;GACCFA;;;AU)");

	EXPECT_FALSE(security == oth);
	EXPECT_TRUE(security != oth);
}

TEST(ScannedFile_Security_Test, opCompare_SaclRights_NotEqual) {
	const ScannedFile::Security security = CreateSecurity("O:BAG:BAS:(OU;SAFA;GACCFA;;;AU)");
	const ScannedFile::Security oth = CreateSecurity("O:BAG:BAS:(OU;SAFA;GACC;;;AU)");

	EXPECT_FALSE(security == oth);
	EXPECT_TRUE(security != oth);
}

TEST(ScannedFile_Security_Test, opCompare_SaclSid_NotEqual) {
	const ScannedFile::Security security = CreateSecurity("O:BAG:BAS:(OU;SAFA;GACCFA;;;AU)");
	const ScannedFile::Security oth = CreateSecurity("O:BAG:BAS:(OU;SAFA;GACCFA;;;WD)");

	EXPECT_FALSE(security == oth);
	EXPECT_TRUE(security != oth);
}

TEST(ScannedFile_Security_Test, opCompare_SaclEmpty_NotEqual) {
	const ScannedFile::Security security = CreateSecurity("O:BAG:BAS:(OU;SAFA;GACCFA;;;AU)");
	const ScannedFile::Security oth = CreateSecurity("O:BAG:BA");

	EXPECT_FALSE(security == oth);
	EXPECT_TRUE(security != oth);
}

TEST(ScannedFile_Security_Test, opCompare_DaclSaclEmptyBoth_Equal) {
	const ScannedFile::Security security = CreateSecurity("O:BAG:BA");
	const ScannedFile::Security oth = CreateSecurity("O:BAG:BA");

	EXPECT_TRUE(security == oth);
	EXPECT_FALSE(security != oth);
}

//
// DirectoryScanner
//
TEST(DirectoryScanner_RealTest, Scan_SystemFolder_ReturnResult) {
	DirectoryScanner scanner;

	DirectoryScanner::Result directories;
	DirectoryScanner::Result files;
	scanner.Scan(TestUtils::GetSystemDirectoryW(), directories, files, DirectoryScanner::Flags::kDefault, kAcceptAllScannerFilter);
	scanner.Wait();

	EXPECT_THAT(directories, t::SizeIs(t::Gt(0)));
	EXPECT_THAT(files, t::SizeIs(t::Gt(0)));

	EXPECT_THAT(directories, t::Each(t::Property(&ScannedFile::GetSize, 0)));
	EXPECT_THAT(directories, t::IsSupersetOf({t::Property(&ScannedFile::GetName, Filename(L"drivers")), t::Property(&ScannedFile::GetName, Filename(L"wbem"))}));
	EXPECT_THAT(files, t::IsSupersetOf({t::Property(&ScannedFile::GetName, Filename(L"kernel32.dll")), t::Property(&ScannedFile::GetName, Filename(L"ntdll.dll"))}));
}

TEST(DirectoryScanner_RealTest, Scan_SystemFolderFiltered_ReturnResult) {
	DirectoryScanner scanner;

	DirectoryScanner::Result directories;
	DirectoryScanner::Result files;
	const LambdaScannerFilter filter([](const Filename &name) {
		return name != L"drivers" && name != L"kernel32.dll";
	});
	scanner.Scan(TestUtils::GetSystemDirectoryW(), directories, files, DirectoryScanner::Flags::kDefault, filter);
	scanner.Wait();

	EXPECT_THAT(directories, t::SizeIs(t::Gt(0)));
	EXPECT_THAT(files, t::SizeIs(t::Gt(0)));

	EXPECT_THAT(directories, t::Contains(t::Property(&ScannedFile::GetName, Filename(L"wbem"))));
	EXPECT_THAT(files, t::Contains(t::Property(&ScannedFile::GetName, Filename(L"ntdll.dll"))));

	EXPECT_THAT(directories, t::Not(t::Contains(t::Property(&ScannedFile::GetName, Filename(L"drivers")))));
	EXPECT_THAT(files, t::Not(t::Contains(t::Property(&ScannedFile::GetName, Filename(L"kernel32.dll")))));
}

TEST_P(DirectoryScanner_Test, Scan_DirectoriesFiles_ReturnResult) {
	using namespace std::literals::chrono_literals;

	constexpr std::uint16_t kSkipIndex = 4;
	const std::uint16_t dirCount = std::get<0>(GetParam());
	const std::uint16_t fileCount = std::get<1>(GetParam());
	const bool folderStreams = std::get<3>(GetParam()) == Flags::kFolderStreams;
	const bool folderSecurity = std::get<3>(GetParam()) == Flags::kFolderSecurity;
	const bool fileSecurity = std::get<3>(GetParam()) == Flags::kFileSecurity;

	struct entry : FILE_ID_EXTD_DIR_INFO {
		wchar_t paddingForName[0x1000];
	};

	const Path rootPath(m_path);
	for (MaxRunsType run = 0, maxRuns = GetMaxRuns(); run < maxRuns; ++run) {
		const std::size_t size = 2Ui64 + dirCount + fileCount;
		std::unique_ptr<entry[]> dirInfo = std::make_unique<entry[]>(size);
		std::memset(dirInfo.get(), 0xCC, size * sizeof(entry));

		std::uint16_t dirRemaining = dirCount;
		std::uint16_t fileRemaining = fileCount;
		std::unordered_map<Path, std::pair<std::size_t, std::vector<std::pair<std::wstring, std::uint64_t>>>> streams;
		Path currentFindPath(L"Q:\\");

		for (std::size_t i = 0; i < size; ++i) {
			const bool isDirectory = i < 2 || ((i % 2) && dirRemaining > 0) || fileRemaining == 0;
			if (isDirectory) {
				if (i >= 2) {
					--dirRemaining;
				}
			} else {
				--fileRemaining;
			}
			ZeroMemory(&dirInfo[i], sizeof(entry));
			dirInfo[i].CreationTime.QuadPart = ((isDirectory ? 1000i64 : 2000i64) << 32) | (70i64 + (isDirectory ? dirRemaining : fileRemaining));
			dirInfo[i].LastAccessTime.QuadPart = ((isDirectory ? 1000i64 : 2000i64) << 32) | (71i64 + (isDirectory ? dirRemaining : fileRemaining));
			dirInfo[i].LastWriteTime.QuadPart = ((isDirectory ? 1000i64 : 2000i64) << 32) | (72i64 + (isDirectory ? dirRemaining : fileRemaining));
			dirInfo[i].ChangeTime.QuadPart = ((isDirectory ? 1000i64 : 2000i64) << 32) | (73i64 + (isDirectory ? dirRemaining : fileRemaining));
			dirInfo[i].EndOfFile.QuadPart = isDirectory ? 0 : (1000i64 + fileRemaining);
			dirInfo[i].AllocationSize.QuadPart = dirInfo[i].EndOfFile.QuadPart ? ((dirInfo[i].EndOfFile.QuadPart - 1) / 4096 + 1) * 4096 : 0;
			dirInfo[i].FileAttributes = (isDirectory ? FILE_ATTRIBUTE_DIRECTORY : 0) | ((isDirectory ? dirRemaining : fileRemaining) % 2 ? FILE_ATTRIBUTE_READONLY : 0);

			static_assert(sizeof(FILE_ID_128::Identifier) == 16);
			dirInfo[i].FileId.Identifier[0] = isDirectory ? 1 : 2;
			std::uint16_t id = 5 + (isDirectory ? dirRemaining : fileRemaining);
			std::memcpy(&dirInfo[i].FileId.Identifier[16 - sizeof(id)], &id, sizeof(id));

			const std::wstring name = fmt::format(i == 0 ? L"." : (i == 1 ? L".." : (isDirectory ? L"dir_{}" : L"file_{}.ext")), isDirectory ? dirRemaining : fileRemaining);
			COM_HR(StringCbCopyW(dirInfo[i].FileName, sizeof(entry::paddingForName) + sizeof(entry::FileName), name.c_str()), "StringCbCopyW {}", name);
			dirInfo[i].FileName[name.size()] = L'X';
			dirInfo[i].FileNameLength = static_cast<ULONG>(name.size() * sizeof(wchar_t));

			if (i < 2) {
				// no streams or security for . and ..
				continue;
			}
			const Path path(rootPath, name);

			std::pair<std::size_t, std::vector<std::pair<std::wstring, std::uint64_t>>> &findEntries = streams[path];
			if (!isDirectory) {
				findEntries.second.emplace_back(L"::$DATA", dirInfo[i].EndOfFile.QuadPart);
				if (fileRemaining % 3 == 1 && id != kSkipIndex + 5) {
					findEntries.second.emplace_back(L":stream0:$DATA", 13);
					EXPECT_CALL(m_win32, GetFileAttributesW(t::StrEq(path.c_str() + findEntries.second.back().first)))
						.WillOnce(t::Return(FILE_ATTRIBUTE_COMPRESSED));
				}
				if (fileRemaining % 4 == 1 && id != kSkipIndex + 5) {
					findEntries.second.emplace_back(L":stream1:$DATA", 14);
					EXPECT_CALL(m_win32, GetFileAttributesW(t::StrEq(path.c_str() + findEntries.second.back().first)))
						.WillOnce(t::Return(FILE_ATTRIBUTE_NORMAL));
				}
			} else if (dirRemaining % 3 == 1 && folderStreams && id != kSkipIndex + 5) {
				findEntries.second.emplace_back(L":dirstream:$DATA", 10);
				EXPECT_CALL(m_win32, GetFileAttributesW(t::StrEq(path.c_str() + findEntries.second.back().first)))
					.WillOnce(t::Return(FILE_ATTRIBUTE_NORMAL));
			}
			if (((isDirectory && folderSecurity) || (!isDirectory && fileSecurity)) && id != kSkipIndex + 5) {
				// security is read after filter
				EXPECT_CALL(m_win32, GetNamedSecurityInfoW(t::StrEq(path.c_str()), SE_FILE_OBJECT, DTGM_ARG6))
					.WillOnce(WITH_LATENCY(5ms, 10ms, ([](t::Unused, t::Unused, t::Unused, t::Unused, t::Unused, t::Unused, t::Unused, PSECURITY_DESCRIPTOR *ppSecurityDescriptor) -> DWORD {
											   if (ConvertStringSecurityDescriptorToSecurityDescriptorA("O:BAG:BAD:(A;;FR;;;CO)", SDDL_REVISION_1, ppSecurityDescriptor, nullptr)) {
												   return ERROR_SUCCESS;
											   }
											   return GetLastError();
										   })));
			}

			if (!isDirectory || folderStreams) {
				EXPECT_CALL(m_win32, FindFirstStreamW(t::StrEq(path.c_str()), FindStreamInfoStandard, t::_, 0))
					.WillRepeatedly(WITH_LATENCY(5ms, 10ms, ([this, &streams, &currentFindPath](LPCWSTR lpFileName, t::Unused, LPVOID lpFindStreamData, t::Unused) {
													 // store current path and reset index
													 currentFindPath = Path(lpFileName);
													 std::pair<std::size_t, std::vector<std::pair<std::wstring, std::uint64_t>>> &findEntries = streams.at(currentFindPath);
													 findEntries.first = 0;

													 if (findEntries.first >= findEntries.second.size()) {
														 SetLastError(ERROR_HANDLE_EOF);
														 return INVALID_HANDLE_VALUE;
													 }

													 WIN32_FIND_STREAM_DATA *const pStream = reinterpret_cast<WIN32_FIND_STREAM_DATA *>(lpFindStreamData);
													 pStream->StreamSize.QuadPart = findEntries.second[findEntries.first].second;
													 COM_HR(StringCbCopyW(pStream->cStreamName, sizeof(WIN32_FIND_STREAM_DATA::cStreamName), findEntries.second[findEntries.first].first.c_str()), "StringCbCopyW");
													 ++m_openHandles;
													 return m_hFind.get();
												 })));
			}
		}

		EXPECT_CALL(m_win32, FindNextStreamW(m_hFind.get(), t::_))
			.WillRepeatedly(WITH_LATENCY(5ms, 10ms, ([this, &streams, &currentFindPath](t::Unused, LPVOID lpFindStreamData) {
											 std::pair<std::size_t, std::vector<std::pair<std::wstring, std::uint64_t>>> &findEntries = streams.at(currentFindPath);
											 ++findEntries.first;
											 if (findEntries.first >= findEntries.second.size()) {
												 SetLastError(ERROR_HANDLE_EOF);
												 return FALSE;
											 }

											 WIN32_FIND_STREAM_DATA *const pStream = reinterpret_cast<WIN32_FIND_STREAM_DATA *>(lpFindStreamData);
											 pStream->StreamSize.QuadPart = findEntries.second[findEntries.first].second;
											 COM_HR(StringCbCopyW(pStream->cStreamName, sizeof(WIN32_FIND_STREAM_DATA::cStreamName), findEntries.second[findEntries.first].first.c_str()), "StringCbCopyW");
											 return TRUE;
										 })));

		std::size_t current = 0;
		EXPECT_CALL(m_win32, GetFileInformationByHandleEx(m_hDirectory.get(), FileIdExtdDirectoryInfo, DTGM_ARG2))
			.WillRepeatedly(WITH_LATENCY(40ms, 120ms, ([this, &current, size, &dirInfo](t::Unused, t::Unused, LPVOID lpFileInformation, DWORD dwBufferSize) noexcept {
											 if (current >= size) {
												 SetLastError(ERROR_NO_MORE_FILES);
												 return FALSE;
											 }
											 const std::size_t count = std::min(dwBufferSize / sizeof(entry), size - current);
											 if (count == 0) {
												 SetLastError(ERROR_MORE_DATA);
												 return FALSE;
											 }
											 CopyMemory(lpFileInformation, &dirInfo[current], count * sizeof(entry));
											 for (std::uint16_t i = 0; i < count - 1; ++i) {
												 reinterpret_cast<entry *>(lpFileInformation)[i].NextEntryOffset = sizeof(entry);
											 }
											 current += count;
											 return TRUE;
										 })));

		DirectoryScanner scanner;

		DirectoryScanner::Result directories;
		DirectoryScanner::Result files;
		const LambdaScannerFilter filter([](const Filename &name) {
			return name.sv() != fmt::format(L"dir_{}", kSkipIndex) && name.sv() != fmt::format(L"file_{}.ext", kSkipIndex);
		});
		DirectoryScanner::Flags flags = DirectoryScanner::Flags::kDefault;
		if (folderStreams) {
			flags |= DirectoryScanner::Flags::kFolderStreams;
		}
		if (folderSecurity) {
			flags |= DirectoryScanner::Flags::kFolderSecurity;
		}
		if (fileSecurity) {
			flags |= DirectoryScanner::Flags::kFileSecurity;
		}
		scanner.Scan(Path(m_path), directories, files, flags, filter);
		scanner.Wait();

		EXPECT_THAT(directories, t::SizeIs(dirCount - (kSkipIndex < dirCount ? 1 : 0)));
		EXPECT_THAT(files, t::SizeIs(fileCount - (kSkipIndex < fileCount ? 1 : 0)));

		std::vector<std::wstring> expectDirs;
		for (std::uint16_t i = 0; i < dirCount; ++i) {
			if (i == kSkipIndex) {
				continue;
			}
			expectDirs.push_back(fmt::format(L"dir_{}", i));
		}
		std::vector<std::wstring> expectFiles;
		for (std::uint16_t i = 0; i < fileCount; ++i) {
			if (i == kSkipIndex) {
				continue;
			}
			expectFiles.push_back(fmt::format(L"file_{}.ext", i));
		}
		EXPECT_THAT(directories, t::UnorderedPointwise(ScannedFileHasName(), expectDirs));
		EXPECT_THAT(files, t::UnorderedPointwise(ScannedFileHasName(), expectFiles));

		EXPECT_THAT(directories, t::Each(t::Property(&ScannedFile::GetSize, 0)));
		EXPECT_THAT(files, t::Each(t::Property(&ScannedFile::GetSize, t::AllOf(t::Ge(1000Ui64), t::Lt(1000Ui64 + fileCount)))));

		EXPECT_THAT(directories, t::Each(t::Property(&ScannedFile::GetCreationTime, t::AllOf(t::Ge((1000i64 << 32) | (70i64)), t::Lt((1000i64 << 32) | (70i64 + dirCount))))));
		EXPECT_THAT(files, t::Each(t::Property(&ScannedFile::GetCreationTime, t::AllOf(t::Ge((2000i64 << 32) | (70i64)), t::Lt((2000i64 << 32) | (70i64 + fileCount))))));

		EXPECT_THAT(directories, t::Each(t::Property(&ScannedFile::GetLastWriteTime, t::AllOf(t::Ge((1000i64 << 32) | (72i64)), t::Lt((1000i64 << 32) | (72i64 + dirCount))))));
		EXPECT_THAT(files, t::Each(t::Property(&ScannedFile::GetLastWriteTime, t::AllOf(t::Ge((2000i64 << 32) | (72i64)), t::Lt((2000i64 << 32) | (72i64 + fileCount))))));

		if (dirCount > 1) {
			EXPECT_THAT(directories, t::Contains(ScannedFileHasAttribute(FILE_ATTRIBUTE_DIRECTORY)));
			EXPECT_THAT(directories, t::Contains(ScannedFileHasAttribute(FILE_ATTRIBUTE_READONLY)));
			if (folderStreams) {
				EXPECT_THAT(directories, t::Contains(ScannedFileHasAtLeastStreams(1)));
			} else {
				EXPECT_THAT(directories, t::Not(t::Contains(ScannedFileHasAtLeastStreams(1))));
			}
		} else {
			EXPECT_THAT(directories, t::Not(t::Contains(ScannedFileHasAtLeastStreams(1))));
		}
		if (fileCount > 1) {
			EXPECT_THAT(files, t::Not(t::Contains(ScannedFileHasAttribute(FILE_ATTRIBUTE_DIRECTORY))));
			EXPECT_THAT(files, t::Contains(ScannedFileHasAttribute(FILE_ATTRIBUTE_READONLY)));
			if (fileCount > 11) {
				EXPECT_THAT(files, t::Contains(ScannedFileHasAtLeastStreams(2)));
			} else if (fileCount > 2) {
				EXPECT_THAT(files, t::Contains(ScannedFileHasAtLeastStreams(1)));
			} else {
				EXPECT_THAT(files, t::Not(t::Contains(ScannedFileHasAtLeastStreams(1))));
			}
		}

		EXPECT_THAT(directories, t::Each(ScannedFileIdMatches(1, 5, 4 + dirCount)));
		EXPECT_THAT(files, t::Each(ScannedFileIdMatches(2, 5, 4 + fileCount)));

		if (folderSecurity) {
			EXPECT_THAT(directories, t::Each(ScannedFileHasSecurity()));
		} else {
			EXPECT_THAT(directories, t::Not(t::Contains(ScannedFileHasSecurity())));
		}
		if (fileSecurity) {
			EXPECT_THAT(files, t::Each(ScannedFileHasSecurity()));
		} else {
			EXPECT_THAT(files, t::Not(t::Contains(ScannedFileHasSecurity())));
		}
	}
}

TEST_P(DirectoryScanner_ErrorTest, Scan_ErrorOpeningDirectory_ThrowException) {
	for (MaxRunsType run = 0, maxRuns = GetMaxRuns(); run < maxRuns; ++run) {
		EXPECT_CALL(m_win32, CreateFileW(t::Eq(m_path), DTGM_ARG6))
			.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_ACCESS_DENIED, INVALID_HANDLE_VALUE));

		DirectoryScanner scanner;

		DirectoryScanner::Result directories;
		DirectoryScanner::Result files;
		scanner.Scan(Path(m_path), directories, files, DirectoryScanner::Flags::kDefault, kAcceptAllScannerFilter);
		EXPECT_THROW(scanner.Wait(), m3c::windows_exception);
	}
}

TEST_P(DirectoryScanner_ErrorTest, Scan_ErrorReadingDirectory_ThrowException) {
	for (MaxRunsType run = 0, maxRuns = GetMaxRuns(); run < maxRuns; ++run) {
		EXPECT_CALL(m_win32, GetFileInformationByHandleEx(m_hDirectory.get(), FileIdExtdDirectoryInfo, DTGM_ARG2));

		DirectoryScanner scanner;

		DirectoryScanner::Result directories;
		DirectoryScanner::Result files;
		scanner.Scan(Path(m_path), directories, files, DirectoryScanner::Flags::kDefault, kAcceptAllScannerFilter);
		EXPECT_THROW(scanner.Wait(), m3c::windows_exception);
	}
}

INSTANTIATE_TEST_SUITE_P(DirectoryScanner_Test, DirectoryScanner_Test, t::Combine(t::Values(0, 1, 10, 50), t::Values(0, 1, 10, 50), t::Values(LatencyMode::kSingle, LatencyMode::kLatency, LatencyMode::kRepeat), t::Values(Flags::kNone)), [](const t::TestParamInfo<DirectoryScanner_Test::ParamType> &param) {
	return fmt::format("{:02}_{}_Directories_{}_Files_{}",
					   param.index,
					   std::get<0>(param.param),
					   std::get<1>(param.param),
					   std::get<2>(param.param) == LatencyMode::kSingle ? "Single" : (std::get<2>(param.param) == LatencyMode::kLatency ? "Latency" : "Repeat"));
});

INSTANTIATE_TEST_SUITE_P(DirectoryScanner_FlagsTest, DirectoryScanner_Test, t::Combine(t::Values(15), t::Values(15), t::Values(LatencyMode::kSingle, LatencyMode::kRepeat), t::Values(Flags::kFolderStreams, Flags::kFolderSecurity, Flags::kFileSecurity)), [](const t::TestParamInfo<DirectoryScanner_Test::ParamType> &param) {
	return fmt::format("{:02}_{}_{}",
					   param.index,
					   std::get<3>(param.param) == Flags::kFolderStreams ? "FolderStreams" : std::get<3>(param.param) == Flags::kFolderSecurity ? "FolderSecurity" : "FileSecurity",
					   std::get<2>(param.param) == LatencyMode::kSingle ? "Single" : (std::get<2>(param.param) == LatencyMode::kLatency ? "Latency" : "Repeat"));
});

INSTANTIATE_TEST_SUITE_P(DirectoryScanner_ErrorTest, DirectoryScanner_ErrorTest, t::Combine(t::Values(0), t::Values(0), t::Values(LatencyMode::kSingle, LatencyMode::kRepeat), t::Values(Flags::kNone)), [](const t::TestParamInfo<DirectoryScanner_ErrorTest::ParamType> &param) {
	return fmt::format("{}_{}", param.index, std::get<2>(param.param) == LatencyMode::kSingle ? "Single" : "Repeat");
});


}  // namespace systools::test
