#include "systools/BackupStrategy.h"

#include "TestUtils.h"
#include "systools/DirectoryScanner.h"
#include "systools/FileComparer.h"
#include "systools/Path.h"

#include <m3c/exception.h>
#include <m3c/handle.h>
#include <m4t/m4t.h>

#include <aclapi.h>
#include <detours_gmock.h>
#include <shobjidl.h>
#include <strsafe.h>
#include <windows.h>

#include <atomic>

namespace systools::test {

namespace t = testing;
namespace dtgm = detours_gmock;

namespace {

#pragma warning(suppress : 4100)
ACTION_P(Assert, msg) {
	throw std::exception(msg);
}

#pragma warning(suppress : 4100)
MATCHER_P(PathIs, path, "") {
	static_assert(std::is_convertible_v<arg_type, const Path&>);

	if constexpr (std::is_convertible_v<path_type, const std::wstring&> || std::is_convertible_v<path_type, const wchar_t*>) {
		return arg == Path(path);
	} else if constexpr (std::is_convertible_v<path_type, const Path&>) {
		return arg == path;
	} else {
		static_assert(false);
	}
}

enum class Strategy {
	kShell,
	kWin
};

void PrintTo(const Strategy strategy, std::ostream* const os) {
	switch (strategy) {
	case Strategy::kWin:
		*os << "Win";
		break;
	case Strategy::kShell:
		*os << "Shell";
		break;
	}
}

enum class FileMode {
	kRegular,
	kReadOnly
};

void PrintTo(const FileMode fileMode, std::ostream* const os) {
	switch (fileMode) {
	case FileMode::kRegular:
		*os << "Regular";
		break;
	case FileMode::kReadOnly:
		*os << "ReadOnly";
		break;
	}
}

}  // namespace


#define WIN32_FUNCTIONS(fn_)                                                                                                                                                                       \
	fn_(7, HANDLE, WINAPI, CreateFileW,                                                                                                                                                            \
		(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile), \
		(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile),                                                              \
		nullptr);                                                                                                                                                                                  \
	fn_(1, BOOL, WINAPI, CloseHandle,                                                                                                                                                              \
		(HANDLE hObject),                                                                                                                                                                          \
		(hObject),                                                                                                                                                                                 \
		nullptr);                                                                                                                                                                                  \
	fn_(2, BOOL, WINAPI, CreateDirectoryW,                                                                                                                                                         \
		(LPCWSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes),                                                                                                                          \
		(lpPathName, lpSecurityAttributes),                                                                                                                                                        \
		dtgm::SetLastErrorAndReturn(ERROR_FILE_INVALID, FALSE));                                                                                                                                   \
	fn_(3, BOOL, WINAPI, CreateDirectoryExW,                                                                                                                                                       \
		(LPCWSTR lpTemplateDirectory, LPCWSTR lpNewDirectory, LPSECURITY_ATTRIBUTES lpSecurityAttributes),                                                                                         \
		(lpTemplateDirectory, lpNewDirectory, lpSecurityAttributes),                                                                                                                               \
		dtgm::SetLastErrorAndReturn(ERROR_FILE_INVALID, FALSE));                                                                                                                                   \
	fn_(3, BOOL, WINAPI, CreateHardLinkW,                                                                                                                                                          \
		(LPCWSTR lpFileName, LPCWSTR lpExistingFileName, LPSECURITY_ATTRIBUTES lpSecurityAttributes),                                                                                              \
		(lpFileName, lpExistingFileName, lpSecurityAttributes),                                                                                                                                    \
		dtgm::SetLastErrorAndReturn(ERROR_FILE_INVALID, FALSE));                                                                                                                                   \
	fn_(3, BOOL, WINAPI, MoveFileExW,                                                                                                                                                              \
		(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, DWORD dwFlags),                                                                                                                        \
		(lpExistingFileName, lpNewFileName, dwFlags),                                                                                                                                              \
		dtgm::SetLastErrorAndReturn(ERROR_FILE_INVALID, FALSE));                                                                                                                                   \
	fn_(4, BOOL, WINAPI, GetFileInformationByHandleEx,                                                                                                                                             \
		(HANDLE hFile, FILE_INFO_BY_HANDLE_CLASS FileInformationClass, LPVOID lpFileInformation, DWORD dwBufferSize),                                                                              \
		(hFile, FileInformationClass, lpFileInformation, dwBufferSize),                                                                                                                            \
		dtgm::SetLastErrorAndReturn(ERROR_FILE_INVALID, FALSE));                                                                                                                                   \
	fn_(4, BOOL, WINAPI, SetFileInformationByHandle,                                                                                                                                               \
		(HANDLE hFile, FILE_INFO_BY_HANDLE_CLASS FileInformationClass, LPVOID lpFileInformation, DWORD dwBufferSize),                                                                              \
		(hFile, FileInformationClass, lpFileInformation, dwBufferSize),                                                                                                                            \
		dtgm::SetLastErrorAndReturn(ERROR_FILE_INVALID, FALSE));                                                                                                                                   \
	fn_(6, BOOL, WINAPI, CopyFileExW,                                                                                                                                                              \
		(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, LPPROGRESS_ROUTINE lpProgressRoutine, LPVOID lpData, LPBOOL pbCancel, DWORD dwCopyFlags),                                              \
		(lpExistingFileName, lpNewFileName, lpProgressRoutine, lpData, pbCancel, dwCopyFlags),                                                                                                     \
		dtgm::SetLastErrorAndReturn(ERROR_FILE_INVALID, FALSE));                                                                                                                                   \
	fn_(7, DWORD, WINAPI, SetNamedSecurityInfoW,                                                                                                                                                   \
		(LPWSTR pObjectName, SE_OBJECT_TYPE ObjectType, SECURITY_INFORMATION SecurityInfo, PSID psidOwner, PSID psidGroup, PACL pDacl, PACL pSacl),                                                \
		(pObjectName, ObjectType, SecurityInfo, psidOwner, psidGroup, pDacl, pSacl),                                                                                                               \
		t::Return(ERROR_FILE_INVALID));                                                                                                                                                            \
	fn_(4, HRESULT, STDAPICALLTYPE, SHCreateItemFromParsingName,                                                                                                                                   \
		(PCWSTR pszPath, IBindCtx * pbc, REFIID riid, void** ppv),                                                                                                                                 \
		(pszPath, pbc, riid, ppv),                                                                                                                                                                 \
		t::Return(HRESULT_FROM_WIN32(ERROR_FILE_INVALID)));

#define WIN32_MOVEFILEEX(fn_)                                               \
	fn_(3, BOOL, WINAPI, MoveFileExW,                                       \
		(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, DWORD dwFlags), \
		(lpExistingFileName, lpNewFileName, dwFlags),                       \
		nullptr);                                                           \
	fn_(4, HRESULT, STDAPICALLTYPE, SHCreateItemFromParsingName,            \
		(PCWSTR pszPath, IBindCtx * pbc, REFIID riid, void** ppv),          \
		(pszPath, pbc, riid, ppv),                                          \
		nullptr);

#define PATH_FUNCTIONS(fn_)           \
	fn_(Path, 0, bool, Exists,        \
		(),                           \
		(),                           \
		Assert("Path::Exists"));      \
	fn_(Path, 0, bool, IsDirectory,   \
		(),                           \
		(),                           \
		Assert("Path::IsDirectory")); \
	fn_(Path, 0, Path, GetParent,     \
		(),                           \
		(),                           \
		Assert("Path::GetParent"));   \
	fn_(Path, 0, void, ForceDelete,   \
		(),                           \
		(),                           \
		Assert("Path::ForceDelete"));

#define FILE_COMPARER_FUNCTIONS(fn_)        \
	fn_(FileComparer, 2, bool, Compare,     \
		(const Path& src, const Path& cpy), \
		(src, cpy),                         \
		Assert("FileComparer::Compare"));

#define DIRECTORY_SCANNER_FUNCTIONS(fn_)                                                                                                                   \
	fn_(                                                                                                                                                   \
		DirectoryScanner, 5, void, Scan,                                                                                                                   \
		(Path path, DirectoryScanner::Result & directories, DirectoryScanner::Result & files, DirectoryScanner::Flags flags, const ScannerFilter& filter), \
		(path, directories, files, flags, filter),                                                                                                         \
		Assert("DirectoryScanner::Scan"));                                                                                                                 \
	fn_(DirectoryScanner, 0, void, Wait,                                                                                                                   \
		(),                                                                                                                                                \
		(),                                                                                                                                                \
		t::Return());

DTGM_DECLARE_API_MOCK(Win32, WIN32_FUNCTIONS);
DTGM_DECLARE_API_MOCK(Win32MoveFileEx, WIN32_MOVEFILEEX);
DTGM_DECLARE_CLASS_MOCK(Path, PATH_FUNCTIONS);
DTGM_DECLARE_CLASS_MOCK(DirectoryScanner, DIRECTORY_SCANNER_FUNCTIONS);
DTGM_DECLARE_CLASS_MOCK(FileComparer, FILE_COMPARER_FUNCTIONS);


template <typename T>
class BackupStrategy_Test : public t::Test {
protected:
	void SetUp() override {
		const m3c::handle hMutex = CreateMutexW(nullptr, FALSE, nullptr);
		if (!hMutex) {
			THROW(m3c::windows_exception(GetLastError()), "CreateMutex");
		}

		HANDLE handle;
		if (!DuplicateHandle(GetCurrentProcess(), hMutex, GetCurrentProcess(), &handle, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
			THROW(m3c::windows_exception(GetLastError()), "DuplicateHandle");
		}
		m_hFile = handle;

		ON_CALL(m_win32, CreateFileW(t::Eq(m_name), DTGM_ARG6))
			.WillByDefault(t::WithoutArgs([this]() noexcept {
				++m_openHandles;
				return m_hFile.get();
			}));

		ON_CALL(m_win32, CloseHandle(m_hFile.get()))
			.WillByDefault(t::WithoutArgs([this]() noexcept {
				--m_openHandles;
				return TRUE;
			}));

		ON_CALL(m_win32, GetFileInformationByHandleEx(m_hFile.get(), FileBasicInfo, DTGM_ARG2))
			.WillByDefault([](t::Unused, t::Unused, LPVOID lpFileInformation, DWORD dwBufferSize) noexcept {
				if (dwBufferSize < sizeof(FILE_BASIC_INFO)) {
					assert(false);
					SetLastError(ERROR_INVALID_PARAMETER);
					return FALSE;
				}
				FILE_BASIC_INFO* pFileBasicInfo = static_cast<FILE_BASIC_INFO*>(lpFileInformation);
				pFileBasicInfo->ChangeTime.QuadPart = kChangeTime;
				pFileBasicInfo->CreationTime.QuadPart = kCreationTime;
				pFileBasicInfo->LastWriteTime.QuadPart = kLastWriteTime;
				pFileBasicInfo->LastAccessTime.QuadPart = kLastAccessTime;
				pFileBasicInfo->FileAttributes = kAttributes;
				return TRUE;
			});
	}

	void TearDown() override {
		EXPECT_EQ(0u, m_openHandles);
		t::Mock::VerifyAndClearExpectations(&m_fileComparer);
		t::Mock::VerifyAndClearExpectations(&m_directoryScanner);
		t::Mock::VerifyAndClearExpectations(&m_path);
		t::Mock::VerifyAndClearExpectations(&m_win32);
		DTGM_DETACH_CLASS_MOCK(FileComparer);
		DTGM_DETACH_CLASS_MOCK(DirectoryScanner);
		DTGM_DETACH_CLASS_MOCK(Path);
		DTGM_DETACH_API_MOCK(Win32);
	}

protected:
	static constexpr std::int64_t kChangeTime = 400;
	static constexpr std::int64_t kCreationTime = 100;
	static constexpr std::int64_t kLastWriteTime = 300;
	static constexpr std::int64_t kLastAccessTime = 500;
	static constexpr DWORD kAttributes = FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_ARCHIVE;

protected:
	DTGM_DEFINE_STRICT_API_MOCK(Win32, m_win32);
	DTGM_DEFINE_STRICT_CLASS_MOCK(Path, m_path);
	DTGM_DEFINE_STRICT_CLASS_MOCK(DirectoryScanner, m_directoryScanner);
	DTGM_DEFINE_STRICT_CLASS_MOCK(FileComparer, m_fileComparer);

	const std::wstring m_parent = LR"(\\?\Volume{23220209-1205-1000-8000-0000000001}\)";
	const std::wstring m_name = m_parent + LR"(src)";
	m3c::handle m_hFile;

private:
	std::atomic_uint32_t m_openHandles = 0;
};

class BackupStrategy_RenameTest : public t::TestWithParam<std::tuple<Strategy, FileMode>> {
protected:
	void SetUp() override {
		const m3c::handle hFile = CreateFileW(kTempFile.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, std::get<1>(GetParam()) == FileMode::kReadOnly ? FILE_ATTRIBUTE_READONLY : FILE_ATTRIBUTE_NORMAL, nullptr);
		ASSERT_TRUE(hFile);

		if (std::get<0>(GetParam()) == Strategy::kShell) {
			EXPECT_CALL(m_win32, MoveFileEx(t::StrEq(kTempFile.c_str()), t::StrEq(kTempFileRenamed.c_str()), t::_))
				.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_ACCESS_DENIED, FALSE));
			EXPECT_CALL(m_win32, SHCreateItemFromParsingName(t::StrEq(kTempFile.c_str()), t::_, __uuidof(IShellItem), t::_));
		} else {
			EXPECT_CALL(m_win32, MoveFileEx(t::StrEq(kTempFile.c_str()), t::StrEq(kTempFileRenamed.c_str()), t::_));
		}
	}

	void TearDown() override {
		if (kTempFile.Exists()) {
			kTempFile.ForceDelete();
		}
		if (kTempFileRenamed.Exists()) {
			kTempFileRenamed.ForceDelete();
		}
		t::Mock::VerifyAndClearExpectations(&m_win32);
		DTGM_DETACH_API_MOCK(Win32MoveFileEx);
	}

protected:
	DTGM_DEFINE_STRICT_API_MOCK(Win32MoveFileEx, m_win32);

	const Path kTempFile = TestUtils::GetTempDirectory() / L"23220209-1205-1000-8000-0000001001.0.test";
	const Path kTempFileRenamed = TestUtils::GetTempDirectory() / L"23220209-1205-1000-8000-0000001001.1.test";
};

namespace {

using StrategyTypes = t::Types<DryRunBackupStrategy, WritingBackupStrategy>;

}  // namespace

class StrategyTypeNames {
public:
	template <typename T>
	static std::string GetName(const int index) {
		// MUST start with a number, else google test adapter cannot find the source code
		if constexpr (std::is_same_v<T, DryRunBackupStrategy>) {
			return fmt::format("{}_DryRun", index);
		} else if constexpr (std::is_same_v<T, WritingBackupStrategy>) {
			return fmt::format("{}_Writing", index);
		}
	}
};

TYPED_TEST_SUITE(BackupStrategy_Test, StrategyTypes, StrategyTypeNames);


//
// Path Operations
//

TYPED_TEST(BackupStrategy_Test, Exists_IsTrue_ReturnTrue) {
	const Path path(this->m_name);

	EXPECT_CALL(this->m_path, Exists())
		.WillOnce(dtgm::WithAssert(&this->m_path, &path, t::Return(true)));

	TypeParam strategy;
	EXPECT_TRUE(strategy.Exists(path));
}

TYPED_TEST(BackupStrategy_Test, Exists_IsFalse_ReturnFalse) {
	const Path path(this->m_name);

	EXPECT_CALL(this->m_path, Exists())
		.WillOnce(dtgm::WithAssert(&this->m_path, &path, t::Return(false)));

	TypeParam strategy;
	EXPECT_FALSE(strategy.Exists(path));
}

TYPED_TEST(BackupStrategy_Test, Exists_Throws_ThrowException) {
	const Path path(this->m_name);

	EXPECT_CALL(this->m_path, Exists())
		.WillOnce(dtgm::WithAssert(&this->m_path, &path, t::Throw(std::logic_error("test"))));

	TypeParam strategy;
#pragma warning(suppress : 4834)
	EXPECT_THROW(strategy.Exists(path), std::logic_error);
}

TYPED_TEST(BackupStrategy_Test, IsDirectory_IsTrue_ReturnTrue) {
	const Path path(this->m_name);

	EXPECT_CALL(this->m_path, IsDirectory())
		.WillOnce(dtgm::WithAssert(&this->m_path, &path, t::Return(true)));

	TypeParam strategy;
	EXPECT_TRUE(strategy.IsDirectory(path));
}

TYPED_TEST(BackupStrategy_Test, IsDirectory_IsFalse_ReturnFalse) {
	const Path path(this->m_name);

	EXPECT_CALL(this->m_path, IsDirectory())
		.WillOnce(dtgm::WithAssert(&this->m_path, &path, t::Return(false)));

	TypeParam strategy;
	EXPECT_FALSE(strategy.IsDirectory(path));
}

TYPED_TEST(BackupStrategy_Test, IsDirectory_Throws_ThrowException) {
	const Path path(this->m_name);

	EXPECT_CALL(this->m_path, IsDirectory())
		.WillOnce(dtgm::WithAssert(&this->m_path, &path, t::Throw(std::logic_error("test"))));

	TypeParam strategy;
#pragma warning(suppress : 4834)
	EXPECT_THROW(strategy.IsDirectory(path), std::logic_error);
}


//
// File Operations
//

TYPED_TEST(BackupStrategy_Test, Compare_Equal_ReturnTrue) {
	const Path src(this->m_name);
	const Path dst(LR"(Q:\foo)");
	FileComparer fileComparer;

	EXPECT_CALL(this->m_fileComparer, Compare(PathIs(src), PathIs(dst)))
		.WillOnce(dtgm::WithAssert(&this->m_fileComparer, &fileComparer, t::Return(true)));

	TypeParam strategy;
	EXPECT_TRUE(strategy.Compare(src, dst, fileComparer));
}

TYPED_TEST(BackupStrategy_Test, Compare_Unequal_ReturnFalse) {
	const Path src(this->m_name);
	const Path dst(LR"(Q:\foo)");
	FileComparer fileComparer;

	EXPECT_CALL(this->m_fileComparer, Compare(PathIs(src), PathIs(dst)))
		.WillOnce(dtgm::WithAssert(&this->m_fileComparer, &fileComparer, t::Return(false)));

	TypeParam strategy;
	EXPECT_FALSE(strategy.Compare(src, dst, fileComparer));
}

TYPED_TEST(BackupStrategy_Test, Compare_Error_ThrowException) {
	const Path src(this->m_name);
	const Path dst(LR"(Q:\foo)");
	FileComparer fileComparer;

	EXPECT_CALL(this->m_fileComparer, Compare(PathIs(src), PathIs(dst)))
		.WillOnce(dtgm::WithAssert(&this->m_fileComparer, &fileComparer, t::Throw(std::logic_error("test"))));

	TypeParam strategy;
	EXPECT_THROW(strategy.Compare(src, dst, fileComparer), std::logic_error);
}

TYPED_TEST(BackupStrategy_Test, CreateDirectory_Create_Return) {
	const Path path(this->m_name);
	const Path templatePath(LR"(Q:\foo)");

	void* const ptr = new std::string("bar");

	if constexpr (!std::is_same_v<TypeParam, DryRunBackupStrategy>) {
		EXPECT_CALL(this->m_win32, CreateDirectoryExW(t::StrEq(templatePath.c_str()), t::StrEq(path.c_str()), t::Pointee(t::Field(&SECURITY_ATTRIBUTES::lpSecurityDescriptor, ptr))))
			.WillOnce(t::Return(TRUE));
	}

	TypeParam strategy;
	ScannedFile securitySource(Filename(L""), {}, {}, {}, 0, {}, {});
	securitySource.GetSecurity().pSecurityDescriptor.reset(ptr);
	strategy.CreateDirectory(path, templatePath, securitySource);
}

TYPED_TEST(BackupStrategy_Test, CreateDirectory_Error_ThrowException) {
	if constexpr (std::is_same_v<TypeParam, DryRunBackupStrategy>) {
		// no need to check for error in function which is never called
		return;
	} else {
		const Path path(this->m_name);
		const Path templatePath(LR"(Q:\foo)");

		EXPECT_CALL(this->m_win32, CreateDirectoryExW(t::StrEq(templatePath.c_str()), t::StrEq(path.c_str()), t::_))
			.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_ACCESS_DENIED, FALSE));

		TypeParam strategy;
		const ScannedFile securitySource(Filename(L""), {}, {}, {}, 0, {}, {});
		EXPECT_THROW(strategy.CreateDirectory(path, templatePath, securitySource), m3c::windows_exception);
	}
}

TYPED_TEST(BackupStrategy_Test, CreateDirectoryRecursive_GrandChildExists_Return) {
	const Path root(this->m_name);
	const Path child = root / L"child";
	const Path grandChild = child / L"grandchild";

	if constexpr (!std::is_same_v<TypeParam, DryRunBackupStrategy>) {
		EXPECT_CALL(this->m_path, Exists())
			.WillOnce(dtgm::WithAssert(&this->m_path, &grandChild, t::Return(true)));
	}

	TypeParam strategy;
	strategy.CreateDirectoryRecursive(grandChild);
}

TYPED_TEST(BackupStrategy_Test, CreateDirectoryRecursive_GrandChildExistsThrows_ThrowException) {
	if constexpr (std::is_same_v<TypeParam, DryRunBackupStrategy>) {
		// no need to check for error in function which is never called
		return;
	} else {
		const Path root(this->m_name);
		const Path child = root / L"child";
		const Path grandChild = child / L"grandchild";

		EXPECT_CALL(this->m_path, Exists())
			.WillOnce(dtgm::WithAssert(&this->m_path, &grandChild, t::Throw(std::logic_error("test"))));

		TypeParam strategy;
		EXPECT_THROW(strategy.CreateDirectoryRecursive(grandChild), std::logic_error);
	}
}

TYPED_TEST(BackupStrategy_Test, CreateDirectoryRecursive_OnlyChildExists_Create) {
	if constexpr (std::is_same_v<TypeParam, DryRunBackupStrategy>) {
		// no need to check for error in function which is never called
		return;
	} else {
		const Path root(this->m_name);
		const Path child = root / L"child";
		const Path grandChild = child / L"grandchild";

		EXPECT_CALL(this->m_path, Exists())
			.WillOnce(dtgm::WithAssert(&this->m_path, &grandChild, t::Return(false)))
			.WillOnce(t::Return(true));
		EXPECT_CALL(this->m_path, GetParent())
			.WillOnce(dtgm::WithAssert(&this->m_path, &grandChild, t::Return(child)));
		EXPECT_CALL(this->m_win32, CreateDirectoryW(t::StrEq(grandChild.c_str()), nullptr))
			.WillOnce(t::Return(TRUE));

		TypeParam strategy;
		strategy.CreateDirectoryRecursive(grandChild);
	}
}

TYPED_TEST(BackupStrategy_Test, CreateDirectoryRecursive_OnlyChildExistsGrandChildCreationFailed_ThrowsException) {
	if constexpr (std::is_same_v<TypeParam, DryRunBackupStrategy>) {
		// no need to check for error in function which is never called
		return;
	} else {
		const Path root(this->m_name);
		const Path child = root / L"child";
		const Path grandChild = child / L"grandchild";

		EXPECT_CALL(this->m_path, Exists())
			.WillOnce(dtgm::WithAssert(&this->m_path, &grandChild, t::Return(false)))
			.WillOnce(t::Return(true));
		EXPECT_CALL(this->m_path, GetParent())
			.WillOnce(dtgm::WithAssert(&this->m_path, &grandChild, t::Return(child)));
		EXPECT_CALL(this->m_win32, CreateDirectoryW(t::StrEq(grandChild.c_str()), nullptr))
			.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_ACCESS_DENIED, FALSE));

		TypeParam strategy;
		EXPECT_THROW(strategy.CreateDirectoryRecursive(grandChild), m3c::windows_exception);
	}
}

TYPED_TEST(BackupStrategy_Test, CreateDirectoryRecursive_OnlyRootExists_Create) {
	if constexpr (std::is_same_v<TypeParam, DryRunBackupStrategy>) {
		// no need to check for error in function which is never called
		return;
	} else {
		const Path root(this->m_name);
		const Path child = root / L"child";
		const Path grandChild = child / L"grandchild";

		EXPECT_CALL(this->m_path, Exists())
			.WillOnce(dtgm::WithAssert(&this->m_path, &grandChild, t::Return(false)))
			.WillOnce(t::Return(false))
			.WillOnce(t::Return(true));
		EXPECT_CALL(this->m_path, GetParent())
			.WillOnce(dtgm::WithAssert(&this->m_path, &grandChild, t::Return(child)))
			.WillOnce(t::Return(root));
		EXPECT_CALL(this->m_win32, CreateDirectoryW(t::StrEq(child.c_str()), nullptr))
			.WillOnce(t::Return(TRUE));
		EXPECT_CALL(this->m_win32, CreateDirectoryW(t::StrEq(grandChild.c_str()), nullptr))
			.WillOnce(t::Return(TRUE));

		TypeParam strategy;
		strategy.CreateDirectoryRecursive(grandChild);
	}
}

TYPED_TEST(BackupStrategy_Test, SetAttributes_SameAttributes_Return) {
	const Path path(this->m_name);
	const LARGE_INTEGER size{.QuadPart = 1234};
	const LARGE_INTEGER creationTime{.QuadPart = this->kCreationTime};
	const LARGE_INTEGER lastWriteTime{.QuadPart = this->kLastWriteTime};
	const DWORD attributes = this->kAttributes;
	FILE_ID_128 fileId = {};
	const ScannedFile file(Filename(L"foo"), size, creationTime, lastWriteTime, attributes, fileId, {});

	if constexpr (!std::is_same_v<TypeParam, DryRunBackupStrategy>) {
		EXPECT_CALL(this->m_win32, GetFileInformationByHandleEx(this->m_hFile.get(), FileBasicInfo, DTGM_ARG2));
	}

	TypeParam strategy;
	strategy.SetAttributes(path, file);
}

TYPED_TEST(BackupStrategy_Test, SetAttributes_DifferentCreationTime_UpdateAttributes) {
	const Path path(this->m_name);
	const LARGE_INTEGER size{.QuadPart = 1234};
	const LARGE_INTEGER creationTime{.QuadPart = this->kCreationTime + 1};
	const LARGE_INTEGER lastWriteTime{.QuadPart = this->kLastWriteTime};
	const DWORD attributes = this->kAttributes;
	FILE_ID_128 fileId = {};
	const ScannedFile file(Filename(L"foo"), size, creationTime, lastWriteTime, attributes, fileId, {});

	FILE_BASIC_INFO fileBasicInfo{};
	if constexpr (!std::is_same_v<TypeParam, DryRunBackupStrategy>) {
		EXPECT_CALL(this->m_win32, GetFileInformationByHandleEx(this->m_hFile.get(), FileBasicInfo, DTGM_ARG2));
		EXPECT_CALL(this->m_win32, SetFileInformationByHandle(this->m_hFile.get(), FileBasicInfo, t::_, t::_))
			.WillOnce([&fileBasicInfo](t::Unused, t::Unused, LPVOID lpFileInformation, DWORD dwBufferSize) {
				CopyMemory(&fileBasicInfo, lpFileInformation, std::min<std::size_t>(dwBufferSize, sizeof(FILE_BASIC_INFO)));
				return TRUE;
			});
	}

	TypeParam strategy;
	strategy.SetAttributes(path, file);

	if constexpr (!std::is_same_v<TypeParam, DryRunBackupStrategy>) {
		EXPECT_EQ(creationTime.QuadPart, fileBasicInfo.CreationTime.QuadPart);
		EXPECT_EQ(lastWriteTime.QuadPart, fileBasicInfo.LastWriteTime.QuadPart);
		EXPECT_EQ(attributes, fileBasicInfo.FileAttributes);

		EXPECT_EQ(this->kChangeTime, fileBasicInfo.ChangeTime.QuadPart);
		EXPECT_EQ(this->kLastAccessTime, fileBasicInfo.LastAccessTime.QuadPart);
	}
}

TYPED_TEST(BackupStrategy_Test, SetAttributes_DifferentLastWriteTime_UpdateAttributes) {
	const Path path(this->m_name);
	const LARGE_INTEGER size{.QuadPart = 1234};
	const LARGE_INTEGER creationTime{.QuadPart = this->kCreationTime};
	const LARGE_INTEGER lastWriteTime{.QuadPart = this->kLastWriteTime + 1};
	const DWORD attributes = this->kAttributes;
	FILE_ID_128 fileId = {};
	const ScannedFile file(Filename(L"foo"), size, creationTime, lastWriteTime, attributes, fileId, {});

	FILE_BASIC_INFO fileBasicInfo{};
	if constexpr (!std::is_same_v<TypeParam, DryRunBackupStrategy>) {
		EXPECT_CALL(this->m_win32, GetFileInformationByHandleEx(this->m_hFile.get(), FileBasicInfo, DTGM_ARG2));
		EXPECT_CALL(this->m_win32, SetFileInformationByHandle(this->m_hFile.get(), FileBasicInfo, t::_, t::_))
			.WillOnce([&fileBasicInfo](t::Unused, t::Unused, LPVOID lpFileInformation, DWORD dwBufferSize) {
				CopyMemory(&fileBasicInfo, lpFileInformation, std::min<std::size_t>(dwBufferSize, sizeof(FILE_BASIC_INFO)));
				return TRUE;
			});
	}

	TypeParam strategy;
	strategy.SetAttributes(path, file);

	if constexpr (!std::is_same_v<TypeParam, DryRunBackupStrategy>) {
		EXPECT_EQ(creationTime.QuadPart, fileBasicInfo.CreationTime.QuadPart);
		EXPECT_EQ(lastWriteTime.QuadPart, fileBasicInfo.LastWriteTime.QuadPart);
		EXPECT_EQ(attributes, fileBasicInfo.FileAttributes);

		EXPECT_EQ(this->kChangeTime, fileBasicInfo.ChangeTime.QuadPart);
		EXPECT_EQ(this->kLastAccessTime, fileBasicInfo.LastAccessTime.QuadPart);
	}
}

TYPED_TEST(BackupStrategy_Test, SetAttributes_AddAttributes_UpdateAttributes) {
	const Path path(this->m_name);
	const LARGE_INTEGER size{.QuadPart = 1234};
	const LARGE_INTEGER creationTime{.QuadPart = this->kCreationTime};
	const LARGE_INTEGER lastWriteTime{.QuadPart = this->kLastWriteTime};
	const DWORD attributes = this->kAttributes | FILE_ATTRIBUTE_HIDDEN;
	FILE_ID_128 fileId = {};
	const ScannedFile file(Filename(L"foo"), size, creationTime, lastWriteTime, attributes, fileId, {});

	FILE_BASIC_INFO fileBasicInfo{};
	if constexpr (!std::is_same_v<TypeParam, DryRunBackupStrategy>) {
		EXPECT_CALL(this->m_win32, GetFileInformationByHandleEx(this->m_hFile.get(), FileBasicInfo, DTGM_ARG2));
		EXPECT_CALL(this->m_win32, SetFileInformationByHandle(this->m_hFile.get(), FileBasicInfo, t::_, t::_))
			.WillOnce([&fileBasicInfo](t::Unused, t::Unused, LPVOID lpFileInformation, DWORD dwBufferSize) {
				CopyMemory(&fileBasicInfo, lpFileInformation, std::min<std::size_t>(dwBufferSize, sizeof(FILE_BASIC_INFO)));
				return TRUE;
			});
	}

	TypeParam strategy;
	strategy.SetAttributes(path, file);

	if constexpr (!std::is_same_v<TypeParam, DryRunBackupStrategy>) {
		EXPECT_EQ(creationTime.QuadPart, fileBasicInfo.CreationTime.QuadPart);
		EXPECT_EQ(lastWriteTime.QuadPart, fileBasicInfo.LastWriteTime.QuadPart);
		EXPECT_EQ(attributes, fileBasicInfo.FileAttributes);

		EXPECT_EQ(this->kChangeTime, fileBasicInfo.ChangeTime.QuadPart);
		EXPECT_EQ(this->kLastAccessTime, fileBasicInfo.LastAccessTime.QuadPart);
	}
}

TYPED_TEST(BackupStrategy_Test, SetAttributes_AddAndRemoveAttributes_UpdateAttributes) {
	const Path path(this->m_name);
	const LARGE_INTEGER size{.QuadPart = 1234};
	const LARGE_INTEGER creationTime{.QuadPart = this->kCreationTime};
	const LARGE_INTEGER lastWriteTime{.QuadPart = this->kLastWriteTime};
	const DWORD old = this->kAttributes;
	const DWORD attributes = (this->kAttributes & ~FILE_ATTRIBUTE_READONLY) | FILE_ATTRIBUTE_HIDDEN;
	FILE_ID_128 fileId = {};
	const ScannedFile file(Filename(L"foo"), size, creationTime, lastWriteTime, attributes, fileId, {});

	FILE_BASIC_INFO fileBasicInfo{};
	if constexpr (!std::is_same_v<TypeParam, DryRunBackupStrategy>) {
		EXPECT_CALL(this->m_win32, GetFileInformationByHandleEx(this->m_hFile.get(), FileBasicInfo, DTGM_ARG2));
		EXPECT_CALL(this->m_win32, SetFileInformationByHandle(this->m_hFile.get(), FileBasicInfo, t::_, t::_))
			.WillOnce([&fileBasicInfo](t::Unused, t::Unused, LPVOID lpFileInformation, DWORD dwBufferSize) {
				CopyMemory(&fileBasicInfo, lpFileInformation, std::min<std::size_t>(dwBufferSize, sizeof(FILE_BASIC_INFO)));
				return TRUE;
			});
	}

	TypeParam strategy;
	strategy.SetAttributes(path, file);

	if constexpr (!std::is_same_v<TypeParam, DryRunBackupStrategy>) {
		EXPECT_EQ(creationTime.QuadPart, fileBasicInfo.CreationTime.QuadPart);
		EXPECT_EQ(lastWriteTime.QuadPart, fileBasicInfo.LastWriteTime.QuadPart);
		EXPECT_EQ(attributes, fileBasicInfo.FileAttributes);

		EXPECT_EQ(this->kChangeTime, fileBasicInfo.ChangeTime.QuadPart);
		EXPECT_EQ(this->kLastAccessTime, fileBasicInfo.LastAccessTime.QuadPart);
	}
}

TYPED_TEST(BackupStrategy_Test, SetAttributes_DifferentIgnoredAttributes_Return) {
	const Path path(this->m_name);
	const LARGE_INTEGER size{.QuadPart = 1234};
	const LARGE_INTEGER creationTime{.QuadPart = this->kCreationTime};
	const LARGE_INTEGER lastWriteTime{.QuadPart = this->kLastWriteTime};
	const DWORD attributes = this->kAttributes | FILE_ATTRIBUTE_ENCRYPTED;
	FILE_ID_128 fileId = {};
	const ScannedFile file(Filename(L"foo"), size, creationTime, lastWriteTime, attributes, fileId, {});

	if constexpr (!std::is_same_v<TypeParam, DryRunBackupStrategy>) {
		EXPECT_CALL(this->m_win32, GetFileInformationByHandleEx(this->m_hFile.get(), FileBasicInfo, DTGM_ARG2));
	}

	TypeParam strategy;
	strategy.SetAttributes(path, file);
}

TYPED_TEST(BackupStrategy_Test, SetAttributes_ErrorSettingAttributes_ThrowException) {
	if constexpr (std::is_same_v<TypeParam, DryRunBackupStrategy>) {
		// no need to check for error in function which is never called
		return;
	} else {
		const Path path(this->m_name);
		const LARGE_INTEGER size{.QuadPart = 1234};
		const LARGE_INTEGER creationTime{.QuadPart = this->kCreationTime};
		const LARGE_INTEGER lastWriteTime{.QuadPart = this->kLastWriteTime + 1};
		const DWORD attributes = this->kAttributes;
		FILE_ID_128 fileId = {};
		const ScannedFile file(Filename(L"foo"), size, creationTime, lastWriteTime, attributes, fileId, {});

		EXPECT_CALL(this->m_win32, GetFileInformationByHandleEx(this->m_hFile.get(), FileBasicInfo, DTGM_ARG2));
		EXPECT_CALL(this->m_win32, SetFileInformationByHandle(this->m_hFile.get(), FileBasicInfo, t::_, t::_))
			.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_ACCESS_DENIED, FALSE));

		TypeParam strategy;
		EXPECT_THROW(strategy.SetAttributes(path, file), m3c::windows_exception);
	}
}

TYPED_TEST(BackupStrategy_Test, SetSecurity_Call_Return) {
	const Path path(this->m_name);
	const LARGE_INTEGER size{.QuadPart = 1234};
	const LARGE_INTEGER creationTime{.QuadPart = this->kCreationTime};
	const LARGE_INTEGER lastWriteTime{.QuadPart = this->kLastWriteTime};
	const DWORD attributes = this->kAttributes;
	FILE_ID_128 fileId = {};

	ScannedFile file(Filename(L"foo"), size, creationTime, lastWriteTime, attributes, fileId, {});

	const PSID pOwner = reinterpret_cast<PSID>(11);
	const PSID pGroup = reinterpret_cast<PSID>(13);
	const PACL pDacl = reinterpret_cast<PACL>(17);
	const PACL pSacl = reinterpret_cast<PACL>(19);
	ScannedFile::Security& security = file.GetSecurity();
	security.pOwner = pOwner;
	security.pGroup = pGroup;
	security.pDacl = pDacl;
	security.pSacl = pSacl;
	security.pSecurityDescriptor.reset(new std::string("bar"));

	if constexpr (!std::is_same_v<TypeParam, DryRunBackupStrategy>) {
		EXPECT_CALL(this->m_win32, SetNamedSecurityInfoW(t::StrEq(path.c_str()), SE_FILE_OBJECT, t::_, pOwner, pGroup, pDacl, pSacl))
			.WillOnce(t::Return(ERROR_SUCCESS));
	}

	TypeParam strategy;
	strategy.SetSecurity(path, file);
}

TYPED_TEST(BackupStrategy_Test, SetSecurity_Error_ThrowException) {
	if constexpr (std::is_same_v<TypeParam, DryRunBackupStrategy>) {
		// no need to check for error in function which is never called
		return;
	} else {
		const Path path(this->m_name);
		const LARGE_INTEGER size{.QuadPart = 1234};
		const LARGE_INTEGER creationTime{.QuadPart = this->kCreationTime};
		const LARGE_INTEGER lastWriteTime{.QuadPart = this->kLastWriteTime};
		const DWORD attributes = this->kAttributes;
		FILE_ID_128 fileId = {};

		ScannedFile file(Filename(L"foo"), size, creationTime, lastWriteTime, attributes, fileId, {});

		const PSID pOwner = reinterpret_cast<PSID>(11);
		const PSID pGroup = reinterpret_cast<PSID>(13);
		const PACL pDacl = reinterpret_cast<PACL>(17);
		const PACL pSacl = reinterpret_cast<PACL>(19);
		ScannedFile::Security& security = file.GetSecurity();
		security.pOwner = pOwner;
		security.pGroup = pGroup;
		security.pDacl = pDacl;
		security.pSacl = pSacl;
		security.pSecurityDescriptor.reset(new std::string("bar"));

		EXPECT_CALL(this->m_win32, SetNamedSecurityInfoW(t::StrEq(path.c_str()), SE_FILE_OBJECT, t::_, pOwner, pGroup, pDacl, pSacl))
			.WillOnce(t::Return(ERROR_ACCESS_DENIED));

		TypeParam strategy;
		EXPECT_THROW(strategy.SetSecurity(path, file), m3c::windows_exception);
	}
}

TYPED_TEST(BackupStrategy_Test, Rename_Rename_Return) {
	const Path path(this->m_name);
	const Path newName(this->m_parent + LR"(\foo)");

	if constexpr (!std::is_same_v<TypeParam, DryRunBackupStrategy>) {
		EXPECT_CALL(this->m_win32, MoveFileEx(t::StrEq(path.c_str()), t::StrEq(newName.c_str()), t::_))
			.WillOnce(t::Return(TRUE));
	}

	TypeParam strategy;
	strategy.Rename(path, newName);
}

TYPED_TEST(BackupStrategy_Test, Rename_ErrorRenaming_ThrowException) {
	if constexpr (std::is_same_v<TypeParam, DryRunBackupStrategy>) {
		// no need to check for error in function which is never called
		return;
	} else {
		const Path path(this->m_name);
		const Path newName(this->m_parent + LR"(\foo)");

		EXPECT_CALL(this->m_win32, MoveFileEx(t::StrEq(path.c_str()), t::StrEq(newName.c_str()), t::_))
			.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_FILE_INVALID, FALSE));

		TypeParam strategy;
		EXPECT_THROW(strategy.Rename(path, newName), m3c::windows_exception);
	}
}

TYPED_TEST(BackupStrategy_Test, Rename_ErrorRenaming_TryUsingShell) {
	if constexpr (std::is_same_v<TypeParam, DryRunBackupStrategy>) {
		// no need to check for error in function which is never called
		return;
	} else {
		const Path path(this->m_name);
		const Path newName(this->m_parent + LR"(\foo)");

		EXPECT_CALL(this->m_win32, MoveFileEx(t::StrEq(path.c_str()), t::StrEq(newName.c_str()), t::_))
			.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_ACCESS_DENIED, FALSE));
		EXPECT_CALL(this->m_win32, SHCreateItemFromParsingName(t::StrEq(path.c_str()), t::_, __uuidof(IShellItem), t::_))
			.WillOnce(t::Return(E_FILE_PLACEHOLDER_SERVER_TIMED_OUT));

		// Just ensures that Shell API is called, actual renaming is tested using BackupStrategyRenameTest
		TypeParam strategy;
		try {
			strategy.Rename(path, newName);
			assert(false);
			EXPECT_FALSE(true);
		} catch (const m3c::com_exception& e) {
			EXPECT_EQ(E_FILE_PLACEHOLDER_SERVER_TIMED_OUT, e.code().value());
		} catch (...) {
			EXPECT_FALSE(true);
		}
	}
}

TYPED_TEST(BackupStrategy_Test, Copy_Call_Return) {
	const Path src(this->m_name);
	const Path dst(this->m_parent + LR"(\foo)");

	if constexpr (!std::is_same_v<TypeParam, DryRunBackupStrategy>) {
		EXPECT_CALL(this->m_win32, CopyFileEx(t::StrEq(src.c_str()), t::StrEq(dst.c_str()), t::_, t::_, t::_, t::_))
			.WillOnce(t::Return(TRUE));
	}

	TypeParam strategy;
	strategy.Copy(src, dst);
}

TYPED_TEST(BackupStrategy_Test, Copy_ErrorCopying_ThrowException) {
	if constexpr (std::is_same_v<TypeParam, DryRunBackupStrategy>) {
		// no need to check for error in function which is never called
		return;
	} else {
		const Path src(this->m_name);
		const Path dst(this->m_parent + LR"(\foo)");

		EXPECT_CALL(this->m_win32, CopyFileEx(t::StrEq(src.c_str()), t::StrEq(dst.c_str()), t::_, t::_, t::_, t::_))
			.WillOnce(dtgm::SetLastErrorAndReturn(E_ACCESSDENIED, FALSE));

		TypeParam strategy;
		EXPECT_THROW(strategy.Copy(src, dst), m3c::windows_exception);
	}
}

TYPED_TEST(BackupStrategy_Test, CreateHardLink_Call_Return) {
	const Path existing(this->m_name);
	const Path link(this->m_parent + LR"(\foo)");

	if constexpr (!std::is_same_v<TypeParam, DryRunBackupStrategy>) {
		EXPECT_CALL(this->m_win32, CreateHardLinkW(t::StrEq(link.c_str()), t::StrEq(existing.c_str()), t::_))
			.WillOnce(t::Return(TRUE));
	}

	TypeParam strategy;
	strategy.CreateHardLink(link, existing);
}

TYPED_TEST(BackupStrategy_Test, CreateHardLink_ErrorCreatingHardLink_ThrowException) {
	if constexpr (std::is_same_v<TypeParam, DryRunBackupStrategy>) {
		// no need to check for error in function which is never called
		return;
	} else {
		const Path existing(this->m_name);
		const Path link(this->m_parent + LR"(\foo)");

		EXPECT_CALL(this->m_win32, CreateHardLinkW(t::StrEq(link.c_str()), t::StrEq(existing.c_str()), t::_))
			.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_ACCESS_DENIED, FALSE));

		TypeParam strategy;
		EXPECT_THROW(strategy.CreateHardLink(link, existing), m3c::windows_exception);
	}
}

TYPED_TEST(BackupStrategy_Test, Delete_Call_Return) {
	const Path path(this->m_name);

	if constexpr (!std::is_same_v<TypeParam, DryRunBackupStrategy>) {
		EXPECT_CALL(this->m_path, ForceDelete())
			.WillOnce(dtgm::WithAssert(&this->m_path, &path, t::Return()));
	}

	TypeParam strategy;
	strategy.Delete(path);
}

TYPED_TEST(BackupStrategy_Test, Delete_ErrorDeleting_ThrowException) {
	if constexpr (std::is_same_v<TypeParam, DryRunBackupStrategy>) {
		// no need to check for error in function which is never called
		return;
	} else {
		const Path path(this->m_name);

		EXPECT_CALL(this->m_path, ForceDelete())
			.WillOnce(dtgm::WithAssert(&this->m_path, &path, t::Throw(std::logic_error("test"))));

		TypeParam strategy;
		EXPECT_THROW(strategy.Delete(path), std::logic_error);
	}
}


//
// Scan Operations
//

TYPED_TEST(BackupStrategy_Test, Scan_Call_Return) {
	DirectoryScanner scanner;
	std::vector<ScannedFile> directories;
	std::vector<ScannedFile> files;
	const ScannerFilter& filter = kAcceptAllScannerFilter;

	EXPECT_CALL(this->m_directoryScanner, Scan(PathIs(this->m_name), t::Ref(directories), t::Ref(files), DirectoryScanner::Flags::kFolderSecurity, t::Ref(filter)))
		.WillOnce(dtgm::WithAssert(&this->m_directoryScanner, &scanner, t::Return()));

	TypeParam strategy;
	strategy.Scan(Path(this->m_name), scanner, directories, files, DirectoryScanner::Flags::kFolderSecurity, filter);
}

TYPED_TEST(BackupStrategy_Test, Scan_Throws_ThrowException) {
	DirectoryScanner scanner;
	std::vector<ScannedFile> directories;
	std::vector<ScannedFile> files;
	const ScannerFilter& filter = kAcceptAllScannerFilter;

	EXPECT_CALL(this->m_directoryScanner, Scan(PathIs(this->m_name), t::Ref(directories), t::Ref(files), DirectoryScanner::Flags::kFolderSecurity, t::Ref(filter)))
		.WillOnce(dtgm::WithAssert(&this->m_directoryScanner, &scanner, t::Throw(std::logic_error("test"))));

	TypeParam strategy;
	EXPECT_THROW(strategy.Scan(Path(this->m_name), scanner, directories, files, DirectoryScanner::Flags::kFolderSecurity, filter), std::logic_error);
}

TYPED_TEST(BackupStrategy_Test, WaitForScan_Call_Return) {
	DirectoryScanner scanner;

	EXPECT_CALL(this->m_directoryScanner, Wait())
		.WillOnce(dtgm::WithAssert(&this->m_directoryScanner, &scanner, t::Return()));

	TypeParam strategy;
	strategy.WaitForScan(scanner);
}

TYPED_TEST(BackupStrategy_Test, WaitForScan_Throws_ThrowException) {
	DirectoryScanner scanner;

	EXPECT_CALL(this->m_directoryScanner, Wait())
		.WillOnce(dtgm::WithAssert(&this->m_directoryScanner, &scanner, t::Throw(std::logic_error("test"))));

	TypeParam strategy;
	EXPECT_THROW(strategy.WaitForScan(scanner), std::logic_error);
}


TEST_P(BackupStrategy_RenameTest, Rename_Call_Rename) {
	ASSERT_TRUE(kTempFile.Exists());
	ASSERT_FALSE(kTempFileRenamed.Exists());

	WritingBackupStrategy strategy;
	strategy.Rename(kTempFile, kTempFileRenamed);

	EXPECT_FALSE(kTempFile.Exists());
	EXPECT_TRUE(kTempFileRenamed.Exists());
}

INSTANTIATE_TEST_SUITE_P(BackupStrategy_RenameTest, BackupStrategy_RenameTest, t::Combine(t::Values(Strategy::kWin, Strategy::kShell), t::Values(FileMode::kRegular, FileMode::kReadOnly)), [](const t::TestParamInfo<BackupStrategy_RenameTest::ParamType>& param) {
	return fmt::format("{}_{}{}", param.index, std::get<0>(param.param) == Strategy::kShell ? "Shell" : "Win", std::get<1>(param.param) == FileMode::kReadOnly ? "_ReadOnly" : "");
});

}  // namespace systools::test
