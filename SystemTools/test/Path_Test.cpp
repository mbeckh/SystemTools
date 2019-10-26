#include "systools/Path.h"

#include "TestUtils.h"

#include <m3c/Handle.h>
#include <m3c/exception.h>

#include <detours_gmock.h>
#include <pathcch.h>


namespace systools::test {

namespace t = testing;
namespace dtgm = detours_gmock;

#define WIN32_FUNCTIONS(fn_)                                                                    \
	fn_(1, DWORD, WINAPI, GetFileAttributesW,                                                   \
		(LPCWSTR lpFileName),                                                                   \
		(lpFileName),                                                                           \
		nullptr);                                                                               \
	fn_(2, BOOL, WINAPI, SetFileAttributesW,                                                    \
		(LPCWSTR lpFileName, DWORD dwFileAttributes),                                           \
		(lpFileName, dwFileAttributes),                                                         \
		nullptr);                                                                               \
	fn_(1, BOOL, WINAPI, RemoveDirectoryW,                                                      \
		(LPCWSTR lpPathName),                                                                   \
		(lpPathName),                                                                           \
		nullptr);                                                                               \
	fn_(1, BOOL, WINAPI, DeleteFileW,                                                           \
		(LPCWSTR lpFileName),                                                                   \
		(lpFileName),                                                                           \
		nullptr);                                                                               \
	fn_(5, int, WINAPI, CompareStringOrdinal,                                                   \
		(LPCWCH lpString1, int cchCount1, LPCWCH lpString2, int cchCount2, BOOL bIgnoreCase),   \
		(lpString1, cchCount1, lpString2, cchCount2, bIgnoreCase),                              \
		nullptr);                                                                               \
	fn_(4, DWORD, WINAPI, GetFullPathNameW,                                                     \
		(LPCWSTR lpFileName, DWORD nBufferLength, LPWSTR lpBuffer, LPWSTR * lpFilePart),        \
		(lpFileName, nBufferLength, lpBuffer, lpFilePart),                                      \
		nullptr);                                                                               \
	fn_(4, HANDLE, WINAPI, FindFirstFileNameW,                                                  \
		(LPCWSTR lpFileName, DWORD dwFlags, LPDWORD StringLength, PWSTR LinkName),              \
		(lpFileName, dwFlags, StringLength, LinkName),                                          \
		nullptr);                                                                               \
	fn_(3, BOOL, WINAPI, FindNextFileNameW,                                                     \
		(HANDLE hFindStream, LPDWORD StringLength, PWSTR LinkName),                             \
		(hFindStream, StringLength, LinkName),                                                  \
		nullptr);                                                                               \
	fn_(4, HRESULT, APIENTRY, PathCchAppendEx,                                                  \
		(PWSTR pszPath, size_t cchPath, PCWSTR pszMore, ULONG dwFlags),                         \
		(pszPath, cchPath, pszMore, dwFlags),                                                   \
		nullptr);                                                                               \
	fn_(4, HRESULT, APIENTRY, PathCchRemoveBackslashEx,                                         \
		(PWSTR pszPath, size_t cchPath, PWSTR * ppszEnd, size_t * pcchRemaining),               \
		(pszPath, cchPath, ppszEnd, pcchRemaining),                                             \
		nullptr);                                                                               \
	fn_(2, HRESULT, APIENTRY, PathCchSkipRoot,                                                  \
		(PCWSTR pszPath, PCWSTR * ppszRootEnd),                                                 \
		(pszPath, ppszRootEnd),                                                                 \
		nullptr);                                                                               \
	fn_(5, HRESULT, APIENTRY, PathCchCombineEx,                                                 \
		(PWSTR pszPathOut, size_t cchPathOut, PCWSTR pszPathIn, PCWSTR pszMore, ULONG dwFlags), \
		(pszPathOut, cchPathOut, pszPathIn, pszMore, dwFlags),                                  \
		nullptr);                                                                               \
	fn_(2, HRESULT, APIENTRY, PathCchRemoveFileSpec,                                            \
		(PWSTR pszPath, size_t cchPath),                                                        \
		(pszPath, cchPath),                                                                     \
		nullptr);

DTGM_DECLARE_API_MOCK(Win32, WIN32_FUNCTIONS);


class FilenameTest : public t::Test {
protected:
	~FilenameTest() noexcept {
		t::Mock::VerifyAndClearExpectations(&m_win32);
		DTGM_DETACH_API_MOCK(Win32);
	}

protected:
	DTGM_DEFINE_API_MOCK(Win32, m_win32);
};

class PathTest : public t::Test {
protected:
	~PathTest() noexcept {
		t::Mock::VerifyAndClearExpectations(&m_win32);
		DTGM_DETACH_API_MOCK(Win32);
	}

protected:
	DTGM_DEFINE_API_MOCK(Win32, m_win32);
};

namespace {

const std::wstring kTestVolume = LR"(\\?\Volume{23220209-1205-1000-8000-0000000001})";

enum class Mode : std::uint_fast8_t { kDirectory,
									  kFile,
									  kHardlink };

enum class Attributes : DWORD { kReadOnly = FILE_ATTRIBUTE_READONLY,
								kHidden = FILE_ATTRIBUTE_HIDDEN,
								kSystem = FILE_ATTRIBUTE_SYSTEM,
								kNormal = FILE_ATTRIBUTE_NORMAL };

/// @see https://stackoverflow.com/questions/1448396/how-to-use-enums-as-flags-in-c
template <typename T, std::enable_if_t<std::conjunction_v<std::is_same<T, Attributes>, std::is_enum<T>>, int> = 0>
class auto_bool {
public:
	constexpr auto_bool(T value)
		: m_value(value) {
		// empty
	}
	constexpr operator T() const noexcept {
		return m_value;
	}
	constexpr operator std::underlying_type_t<T>() const noexcept {
		return static_cast<std::underlying_type_t<T>>(m_value);
	}
	constexpr explicit operator bool() const noexcept {
		return static_cast<std::underlying_type_t<T>>(m_value);
	}

private:
	const T m_value;
};

template <typename T, std::enable_if_t<std::conjunction_v<std::is_same<T, Attributes>, std::is_enum<T>>, int> = 0>
constexpr auto_bool<T> operator&(const T lhs, const T rhs) {
	return static_cast<T>(static_cast<std::underlying_type_t<T>>(lhs) & static_cast<std::underlying_type_t<T>>(rhs));
}

template <typename T, std::enable_if_t<std::conjunction_v<std::is_same<T, Attributes>, std::is_enum<T>>, int> = 0>
constexpr auto_bool<T> operator&(const auto_bool<T> lhs, const T rhs) {
	return static_cast<T>(static_cast<std::underlying_type_t<T>>(lhs) & static_cast<std::underlying_type_t<T>>(rhs));
}

template <typename T, std::enable_if_t<std::conjunction_v<std::is_same<T, Attributes>, std::is_enum<T>>, int> = 0>
constexpr auto_bool<T> operator&(const T lhs, const auto_bool<T> rhs) {
	return static_cast<T>(static_cast<std::underlying_type_t<T>>(lhs) & static_cast<std::underlying_type_t<T>>(rhs));
}

template <typename T, std::enable_if_t<std::conjunction_v<std::is_same<T, Attributes>, std::is_enum<T>>, int> = 0>
constexpr auto_bool<T> operator&(const auto_bool<T> lhs, const auto_bool<T> rhs) {
	return static_cast<T>(static_cast<std::underlying_type_t<T>>(lhs) & static_cast<std::underlying_type_t<T>>(rhs));
}

template <typename T, std::enable_if_t<std::conjunction_v<std::is_same<T, Attributes>, std::is_enum<T>>, int> = 0>
constexpr auto_bool<T> operator|(const T lhs, const T rhs) {
	return static_cast<T>(static_cast<std::underlying_type_t<T>>(lhs) | static_cast<std::underlying_type_t<T>>(rhs));
}

template <typename T, std::enable_if_t<std::conjunction_v<std::is_same<T, Attributes>, std::is_enum<T>>, int> = 0>
constexpr auto_bool<T> operator|(const auto_bool<T> lhs, const T rhs) {
	return static_cast<T>(static_cast<std::underlying_type_t<T>>(lhs) | static_cast<std::underlying_type_t<T>>(rhs));
}

template <typename T, std::enable_if_t<std::conjunction_v<std::is_same<T, Attributes>, std::is_enum<T>>, int> = 0>
constexpr auto_bool<T> operator|(const T lhs, const auto_bool<T> rhs) {
	return static_cast<T>(static_cast<std::underlying_type_t<T>>(lhs) | static_cast<std::underlying_type_t<T>>(rhs));
}

template <typename T, std::enable_if_t<std::conjunction_v<std::is_same<T, Attributes>, std::is_enum<T>>, int> = 0>
constexpr auto_bool<T> operator|(const auto_bool<T> lhs, const auto_bool<T> rhs) {
	return static_cast<T>(static_cast<std::underlying_type_t<T>>(lhs) | static_cast<std::underlying_type_t<T>>(rhs));
}

template <typename T, std::enable_if_t<std::conjunction_v<std::is_same<T, Attributes>, std::is_enum<T>>, int> = 0>
constexpr auto_bool<T> operator~(const T value) {
	return static_cast<T>(~static_cast<std::underlying_type_t<T>>(value));
}

void PrintTo(const Mode mode, _In_ std::ostream *const os) {
	switch (mode) {
	case Mode::kDirectory:
		*os << "Directory";
		break;
	case Mode::kFile:
		*os << "File";
		break;
	case Mode::kHardlink:
		*os << "Hardlink";
		break;
	}
}

void PrintTo(const Attributes attributes, _In_ std::ostream *const os) {
	bool first = true;
	if (attributes & Attributes::kNormal) {
		*os << "Normal";
		first = false;
	}
	if (attributes & Attributes::kReadOnly) {
		!first && *os << "+";
		*os << "ReadOnly";
		first = false;
	}
	if (attributes & Attributes::kHidden) {
		!first && *os << "+";
		*os << "Hidden";
		first = false;
	}
	if (attributes & Attributes::kSystem) {
		!first && *os << "+";
		*os << "System";
		first = false;
	}
}

}  // namespace

class PathDeleteTest : public t::Test
	, public t::WithParamInterface<std::tuple<Mode, Attributes>> {
protected:
	PathDeleteTest() {
		Initialize();
	}
	~PathDeleteTest() {
		t::Mock::VerifyAndClearExpectations(&m_win32);
		DTGM_DETACH_API_MOCK(Win32);

		if (std::get<0>(GetParam()) == Mode::kHardlink) {
			EXPECT_TRUE(kHardlinkPath.Exists());
			EXPECT_EQ(static_cast<std::underlying_type_t<Attributes>>(std::get<1>(GetParam()) & ~Attributes::kNormal & ~(m_requireReadOnly ? Attributes::kNormal : Attributes::kReadOnly)), GetFileAttributesW(kHardlinkPath.c_str()) & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));

			if (!SetFileAttributesW(kHardlinkPath.c_str(), FILE_ATTRIBUTE_NORMAL)) {
				const DWORD lastError = GetLastError();
				EXPECT_THAT(lastError, t::AnyOf<DWORD>(ERROR_FILE_NOT_FOUND, ERROR_PATH_NOT_FOUND));
			}
			if (DeleteFileW(kHardlinkPath.c_str())) {
				LOG_INFO("Removed stale file {}", kHardlinkPath);
			} else {
				const DWORD lastError = GetLastError();
				EXPECT_THAT(lastError, t::AnyOf<DWORD>(ERROR_FILE_NOT_FOUND, ERROR_PATH_NOT_FOUND));
			}
		}

		if (!SetFileAttributesW(kTempPath.c_str(), FILE_ATTRIBUTE_NORMAL)) {
			const DWORD lastError = GetLastError();
			EXPECT_THAT(lastError, t::AnyOf<DWORD>(ERROR_FILE_NOT_FOUND, ERROR_PATH_NOT_FOUND));
		}
		if (std::get<0>(GetParam()) == Mode::kDirectory) {
			if (RemoveDirectoryW(kTempPath.c_str())) {
				LOG_INFO("Removed stale directory {}", kTempPath);
			} else {
				const DWORD lastError = GetLastError();
				EXPECT_THAT(lastError, t::AnyOf<DWORD>(ERROR_FILE_NOT_FOUND, ERROR_PATH_NOT_FOUND));
			}
		}
		if (std::get<0>(GetParam()) == Mode::kFile || std::get<0>(GetParam()) == Mode::kHardlink) {
			if (DeleteFileW(kTempPath.c_str())) {
				LOG_INFO("Removed stale file {}", kTempPath);
			} else {
				const DWORD lastError = GetLastError();
				EXPECT_THAT(lastError, t::AnyOf<DWORD>(ERROR_FILE_NOT_FOUND, ERROR_PATH_NOT_FOUND));
			}
		}
	}

private:
	void Initialize() {
		if (std::get<0>(GetParam()) == Mode::kDirectory) {
			ASSERT_TRUE(CreateDirectoryW(kTempPath.c_str(), nullptr));
			ASSERT_TRUE(SetFileAttributesW(kTempPath.c_str(), static_cast<std::underlying_type_t<Attributes>>(std::get<1>(GetParam()))));
		}
		if (std::get<0>(GetParam()) == Mode::kFile || std::get<0>(GetParam()) == Mode::kHardlink) {
			const m3c::Handle hFile = CreateFileW(kTempPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, static_cast<std::underlying_type_t<Attributes>>(std::get<1>(GetParam())), nullptr);
			ASSERT_TRUE(hFile);
		}

		ASSERT_EQ(static_cast<std::underlying_type_t<Attributes>>(std::get<1>(GetParam()) & ~Attributes::kNormal), GetFileAttributesW(kTempPath.c_str()) & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
		ASSERT_TRUE(kTempPath.Exists());

		if (std::get<0>(GetParam()) == Mode::kHardlink) {
			ASSERT_TRUE(CreateHardLinkW(kHardlinkPath.c_str(), kTempPath.c_str(), nullptr));

			ASSERT_TRUE(kHardlinkPath.Exists());
			ASSERT_EQ(static_cast<std::underlying_type_t<Attributes>>(std::get<1>(GetParam()) & ~Attributes::kNormal), GetFileAttributesW(kHardlinkPath.c_str()) & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
		}
	}

protected:
	const Path kTempPath = TestUtils::GetTempDirectory() / L"23220209-1205-1000-8000-0000001001.0.test";
	const Path kHardlinkPath = TestUtils::GetTempDirectory() / L"23220209-1205-1000-8000-0000001001.1.test";

protected:
	bool m_requireReadOnly = true;

protected:
	DTGM_DEFINE_API_MOCK(Win32, m_win32);
};


//
// Filename
//

TEST_F(FilenameTest, ctor_FromString_MakeFilename) {
	const Filename filename(L"foo");

	EXPECT_STREQ(L"foo", filename.c_str());
}

TEST_F(FilenameTest, ctor_FromCharacters_MakeFilename) {
	const Filename filename(L"foo", 2);

	EXPECT_STREQ(L"fo", filename.c_str());
}

TEST_F(FilenameTest, opEquals_IsSame_ReturnTrue) {
	const Filename filename(L"foo");

	EXPECT_TRUE(filename == Filename(L"foo"));
	EXPECT_FALSE(filename != Filename(L"foo"));
}

TEST_F(FilenameTest, opEquals_IsIdentity_ReturnTrue) {
	const Filename filename(L"foo");

	EXPECT_TRUE(filename == filename);
	EXPECT_FALSE(filename != filename);
}

TEST_F(FilenameTest, opEquals_IsSameWithDifferentCase_ReturnTrue) {
	const Filename filename(L"foo");

	EXPECT_TRUE(filename == Filename(L"Foo"));
	EXPECT_FALSE(filename != Filename(L"Foo"));
}

TEST_F(FilenameTest, opEquals_IsSameWithUmlaut_ReturnTrue) {
	const Filename filename(L"foo\u00E4\u00DF");

	EXPECT_TRUE(filename == Filename(L"foo\u00E4\u00DF"));
	EXPECT_FALSE(filename != Filename(L"foo\u00E4\u00DF"));
}

TEST_F(FilenameTest, opEquals_IsSameWithUmlautAndAccentAndDifferentCase_ReturnTrue) {
	const Filename filename(L"foo\u00E4\u00E9");

	EXPECT_TRUE(filename == Filename(L"foo\u00C4\u00C9"));
	EXPECT_FALSE(filename != Filename(L"foo\u00C4\u00C9"));
}

TEST_F(FilenameTest, opEquals_IsDifferent_ReturnFalse) {
	const Filename filename(L"foo\u00E4\u00DF");

	EXPECT_FALSE(filename == Filename(L"bar"));
	EXPECT_TRUE(filename != Filename(L"bar"));
}

TEST_F(FilenameTest, opEquals_IsDifferentForDiaeresisOnly_ReturnFalse) {
	const Filename filename(L"foo");

	EXPECT_FALSE(filename == Filename(L"fo\u00E6"));
	EXPECT_TRUE(filename != Filename(L"fo\u00E6"));
}

TEST_F(FilenameTest, opEquals_IsDifferentForAccentOnly_ReturnFalse) {
	const Filename filename(L"foo");

	EXPECT_FALSE(filename == Filename(L"fo\u00F3"));
	EXPECT_TRUE(filename != Filename(L"fo\u00F3"));
}

TEST_F(FilenameTest, opEquals_ErrorComparing_ThrowException) {
	EXPECT_CALL(m_win32, CompareStringOrdinal(t::StrEq(L"foo"), t::_, t::StrEq(L"foo"), t::_, t::_))
		.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_NOT_SUPPORTED, 0))
		.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_NOT_SUPPORTED, 0));
	const Filename filename(L"foo");

	EXPECT_THROW(filename == Filename(L"foo"), m3c::windows_exception);
	EXPECT_THROW(filename != Filename(L"foo"), m3c::windows_exception);
}

TEST_F(FilenameTest, CompareTo_IsSame_Return0) {
	const Filename filename(L"foo");

	EXPECT_EQ(0, filename.CompareTo(Filename(L"foo")));
}

TEST_F(FilenameTest, CompareTo_IsIdentity_Return0) {
	const Filename filename(L"foo");

	EXPECT_EQ(0, filename.CompareTo(filename));
}

TEST_F(FilenameTest, CompareTo_IsSameWithDifferentCase_Return0) {
	const Filename filename(L"foo");

	EXPECT_EQ(0, filename.CompareTo(Filename(L"Foo")));
}

TEST_F(FilenameTest, CompareTo_IsSameWithUmlaut_Return0) {
	const Filename filename(L"foo\u00E4\u00DF");

	EXPECT_EQ(0, filename.CompareTo(Filename(L"foo\u00E4\u00DF")));
}

TEST_F(FilenameTest, CompareTo_IsSameWithUmlautAndAccentAndDifferentCase_Return0) {
	const Filename filename(L"foo\u00E4\u00E9");

	EXPECT_EQ(0, filename.CompareTo(Filename(L"foo\u00C4\u00C9")));
}

TEST_F(FilenameTest, CompareTo_IsLess_ReturnPositive) {
	const Filename filename(L"foo");

	EXPECT_LT(0, filename.CompareTo(Filename(L"bar")));
}

TEST_F(FilenameTest, CompareTo_IsLessAndSubstring_ReturnPositive) {
	const Filename filename(L"foo");

	EXPECT_LT(0, filename.CompareTo(Filename(L"fo")));
}

TEST_F(FilenameTest, CompareTo_IsGreater_ReturnNegative) {
	const Filename filename(L"foo");

	EXPECT_GT(0, filename.CompareTo(Filename(L"zar")));
}

TEST_F(FilenameTest, CompareTo_IsGreaterAndSubstring_ReturnNegative) {
	const Filename filename(L"foo");

	EXPECT_GT(0, filename.CompareTo(Filename(L"fooo")));
}

TEST_F(FilenameTest, CompareTo_IsDifferentForDiaeresisOnly_ReturnNot0) {
	const Filename filename(L"foo");

	EXPECT_NE(0, filename.CompareTo(Filename(L"fo\u00E6")));
}

TEST_F(FilenameTest, CompareTo_IsDifferentForAccentOnly_ReturnNot0) {
	const Filename filename(L"foo");

	EXPECT_NE(0, filename.CompareTo(Filename(L"fo\u00F3")));
}

TEST_F(FilenameTest, CompareTo_IsDifferentUmlauts_ReturnNot0) {
	const Filename filename(L"foo\u00E0");

	EXPECT_NE(0, filename.CompareTo(Filename(L"foo\u00E1")));
}

TEST_F(FilenameTest, CompareTo_ErrorComparing_ThrowException) {
	EXPECT_CALL(m_win32, CompareStringOrdinal(t::StrEq(L"foo"), t::_, t::StrEq(L"foo"), t::_, t::_))
		.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_NOT_SUPPORTED, 0));
	const Filename filename(L"foo");

	EXPECT_THROW(filename.CompareTo(Filename(L"foo")), m3c::windows_exception);
}

TEST_F(FilenameTest, IsSameStringAs_IsSame_ReturnTrue) {
	const Filename filename(L"foo");

	EXPECT_TRUE(filename.IsSameStringAs(Filename(L"foo")));
}

TEST_F(FilenameTest, IsSameStringAs_IsIdentity_ReturnTrue) {
	const Filename filename(L"foo");

	EXPECT_TRUE(filename.IsSameStringAs(filename));
}

TEST_F(FilenameTest, IsSameStringAs_IsSameWithDifferentCase_ReturnFalse) {
	const Filename filename(L"foo");

	EXPECT_FALSE(filename.IsSameStringAs(Filename(L"Foo")));
}

TEST_F(FilenameTest, IsSameStringAs_IsSameWithUmlaut_ReturnTrue) {
	const Filename filename(L"foo\u00E4\u00DF");

	EXPECT_TRUE(filename.IsSameStringAs(Filename(L"foo\u00E4\u00DF")));
}

TEST_F(FilenameTest, IsSameStringAs_IsSameWithUmlautAndAccentAndDifferentCase_ReturnFalse) {
	const Filename filename(L"foo\u00E4\u00E9");

	EXPECT_FALSE(filename.IsSameStringAs(Filename(L"foo\u00C4\u00C9")));
}

TEST_F(FilenameTest, IsSameStringAs_IsLess_ReturnFalse) {
	const Filename filename(L"foo");

	EXPECT_FALSE(filename.IsSameStringAs(Filename(L"bar")));
}

TEST_F(FilenameTest, IsSameStringAs_IsLessAndSubstring_ReturnFalse) {
	const Filename filename(L"foo");

	EXPECT_FALSE(filename.IsSameStringAs(Filename(L"fo")));
}

TEST_F(FilenameTest, IsSameStringAs_IsGreater_ReturnFalse) {
	const Filename filename(L"foo");

	EXPECT_FALSE(filename.IsSameStringAs(Filename(L"bar")));
}

TEST_F(FilenameTest, IsSameStringAs_IsGreaterAndSubstring_ReturnFalse) {
	const Filename filename(L"foo");

	EXPECT_FALSE(filename.IsSameStringAs(Filename(L"fooo")));
}

TEST_F(FilenameTest, IsSameStringAs_IsDifferentForDiaeresisOnly_ReturnFalse) {
	const Filename filename(L"foo");

	EXPECT_FALSE(filename.IsSameStringAs(Filename(L"fo\u00E6")));
}

TEST_F(FilenameTest, IsSameStringAs_IsDifferentForAccentOnly_ReturnFalse) {
	const Filename filename(L"foo");

	EXPECT_FALSE(filename.IsSameStringAs(Filename(L"fo\u00F3")));
}

TEST_F(FilenameTest, IsSameStringAs_IsDifferentUmlauts_ReturnFalse) {
	const Filename filename(L"foo\u00E0");

	EXPECT_FALSE(filename.IsSameStringAs(Filename(L"foo\u00E1")));
}


//
// Path
//

TEST_F(PathTest, ctor_IsRelative_MakeAbsolute) {
	const Path systemDirectory = TestUtils::GetSystemDirectory();
	ASSERT_EQ(TRUE, SetCurrentDirectoryW(systemDirectory.c_str()));

	const Path path(L"foo");

	// static_cast gives error in Intellisense
	EXPECT_EQ((systemDirectory.operator const std::wstring &()) + LR"(\foo)", path.c_str());
}

TEST_F(PathTest, ctor_IsDrive_MakeRoot) {
	const Path path(LR"(Q:)");

	EXPECT_STREQ(LR"(Q:\)", path.c_str());
}

TEST_F(PathTest, ctor_IsRoot_MakeRoot) {
	const Path path(LR"(Q:\)");

	EXPECT_STREQ(LR"(Q:\)", path.c_str());
}

TEST_F(PathTest, ctor_IsAbsolute_MakePath) {
	const Path path(LR"(Q:\foo)");

	EXPECT_STREQ(LR"(Q:\foo)", path.c_str());
}

TEST_F(PathTest, ctor_IsAbsoluteWithSub_MakePath) {
	const Path path(LR"(Q:\foo\bar)");

	EXPECT_STREQ(LR"(Q:\foo\bar)", path.c_str());
}

TEST_F(PathTest, ctor_IsAbsoluteWithSubAndBackslash_RemoveBackslash) {
	const Path path(LR"(Q:\foo\bar\)");

	EXPECT_STREQ(LR"(Q:\foo\bar)", path.c_str());
}

TEST_F(PathTest, ctor_IsAbsoluteWithDot_RemoveDot) {
	const Path path(LR"(Q:\foo\.\bar)");

	EXPECT_STREQ(LR"(Q:\foo\bar)", path.c_str());
}

TEST_F(PathTest, ctor_IsAbsoluteWithDotDot_RemoveDotDot) {
	const Path path(LR"(Q:\foo\..\bar)");

	EXPECT_STREQ(LR"(Q:\bar)", path.c_str());
}

TEST_F(PathTest, ctor_IsVolume_MakeRoot) {
	const Path path(kTestVolume);

	EXPECT_EQ(kTestVolume + LR"(\)", path.c_str());
}

TEST_F(PathTest, ctor_IsVolumeRoot_MakeRoot) {
	const Path path(kTestVolume + LR"(\)");

	EXPECT_EQ(kTestVolume + LR"(\)", path.c_str());
}

TEST_F(PathTest, ctor_IsVolumepPath_MakePath) {
	const Path path(kTestVolume + LR"(\foo)");

	EXPECT_EQ(kTestVolume + LR"(\foo)", path.c_str());
}

TEST_F(PathTest, ctor_IsVolumePathWithBackslash_RemoveBackslash) {
	const Path path(kTestVolume + LR"(\foo\)");

	EXPECT_EQ(kTestVolume + LR"(\foo)", path.c_str());
}

TEST_F(PathTest, ctor_IsForwardSlash_ConvertToBackslash) {
	const Path path(LR"(Q:/foo/bar)");

	EXPECT_STREQ(LR"(Q:\foo\bar)", path.c_str());
}

TEST_F(PathTest, ctor_IsLongAbsolute_MakeLongPath) {
	const std::wstring name = LR"(Q:\)" + std::wstring(MAX_PATH, L'x') + LR"(\foo)";
	const Path path(name);

	EXPECT_EQ(LR"(\\?\)" + name, path.c_str());
}

TEST_F(PathTest, ctor_IsLongRelative_MakeLongPath) {
	const Path systemDirectory = TestUtils::GetSystemDirectory();
	ASSERT_EQ(TRUE, SetCurrentDirectoryW(systemDirectory.c_str()));

	const std::wstring name = std::wstring(MAX_PATH, L'x') + LR"(\foo)";

	const Path path(name);

	// static_cast gives error in Intellisense
	EXPECT_EQ(LR"(\\?\)" + (systemDirectory.operator const std::wstring &()) + LR"(\)" + name, path.c_str());
}

TEST_F(PathTest, ctor_ErrorGettingPathBufferSize_ThrowError) {
	EXPECT_CALL(m_win32, GetFullPathNameW(t::StrEq(L"Q:\\foo"), 0, t::_, nullptr))
		.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_NOT_SUPPORTED, 0));

	EXPECT_THROW(Path(LR"(Q:\foo)"), m3c::windows_exception);
}

TEST_F(PathTest, ctor_ErrorGettingPathName_ThrowError) {
	EXPECT_CALL(m_win32, GetFullPathNameW(t::StrEq(L"Q:\\foo"), 0, t::_, nullptr));
	EXPECT_CALL(m_win32, GetFullPathNameW(t::StrEq(L"Q:\\foo"), t::Gt(0u), t::_, nullptr))
		.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_NOT_SUPPORTED, 0));

	EXPECT_THROW(Path(LR"(Q:\foo)"), m3c::windows_exception);
}

TEST_F(PathTest, ctor_ErrorNormalizing_ThrowError) {
	EXPECT_CALL(m_win32, GetFullPathNameW(t::StrEq(L"Q:\\foo"), 0, t::_, nullptr));
	EXPECT_CALL(m_win32, GetFullPathNameW(t::StrEq(L"Q:\\foo"), t::Gt(0u), t::_, nullptr));
	EXPECT_CALL(m_win32, PathCchAppendEx(t::StrEq(L"Q:\\foo"), t::_, t::StrEq(L"."), t::_))
		.WillOnce(t::Return(HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)));

	EXPECT_THROW(Path(LR"(Q:\foo)"), m3c::com_exception);
}

TEST_F(PathTest, ctor_ErrorRemovingBackslash_ThrowError) {
	EXPECT_CALL(m_win32, GetFullPathNameW(t::StrEq(L"Q:\\foo"), 0, t::_, nullptr));
	EXPECT_CALL(m_win32, GetFullPathNameW(t::StrEq(L"Q:\\foo"), t::Gt(0u), t::_, nullptr));
	EXPECT_CALL(m_win32, PathCchAppendEx(t::StrEq(L"Q:\\foo"), t::_, t::StrEq(L"."), t::_));
	EXPECT_CALL(m_win32, PathCchRemoveBackslashEx(t::StrEq(L"Q:\\foo\\"), t::_, t::_, t::_))
		.WillOnce(t::Return(HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)));

	EXPECT_THROW(Path(LR"(Q:\foo)"), m3c::com_exception);
}

TEST_F(PathTest, opEquals_IsSame_ReturnTrue) {
	const Path path(LR"(Q:\foo)");

	EXPECT_TRUE(path == Path(LR"(Q:\foo)"));
	EXPECT_FALSE(path != Path(LR"(Q:\foo)"));
}

TEST_F(PathTest, opEquals_IsIdentity_ReturnTrue) {
	const Path path(LR"(Q:\foo)");

	EXPECT_TRUE(path == path);
	EXPECT_FALSE(path != path);
}

TEST_F(PathTest, opEquals_IsSameWithDifferentCase_ReturnTrue) {
	const Path path(LR"(Q:\foo)");

	EXPECT_TRUE(path == Path(LR"(q:\Foo)"));
	EXPECT_FALSE(path != Path(LR"(q:\Foo)"));
}

TEST_F(PathTest, opEquals_IsSameWithUmlaut_ReturnTrue) {
	const Path path(L"Q:\\foo\u00E4\u00DF");

	EXPECT_TRUE(path == Path(L"Q:\\foo\u00E4\u00DF"));
	EXPECT_FALSE(path != Path(L"Q:\\foo\u00E4\u00DF"));
}

TEST_F(PathTest, opEquals_IsSameWithUmlautAndAccentAndDifferentCase_ReturnTrue) {
	const Path path(L"Q:\\foo\u00E4\u00E9");

	EXPECT_TRUE(path == Path(L"Q:\\foo\u00C4\u00C9"));
	EXPECT_FALSE(path != Path(L"Q:\\foo\u00C4\u00C9"));
}

TEST_F(PathTest, opEquals_IsDifferent_ReturnFalse) {
	const Path path(LR"(Q:\foo)");

	EXPECT_FALSE(path == Path(LR"(Q:\bar)"));
	EXPECT_TRUE(path != Path(LR"(Q:\bar)"));
}

TEST_F(PathTest, opEquals_IsDifferentForDiaeresisOnly_ReturnFalse) {
	const Path path(L"Q:\\foo");

	EXPECT_FALSE(path == Path(L"Q:\\fo\u00E6"));
	EXPECT_TRUE(path != Path(L"Q:\\fo\u00E6"));
}

TEST_F(PathTest, opEquals_IsDifferentForAccentOnly_ReturnFalse) {
	const Path path(L"Q:\\foo");

	EXPECT_FALSE(path == Path(L"Q:\\fo\u00F3"));
	EXPECT_TRUE(path != Path(L"Q:\\fo\u00F3"));
}

TEST_F(PathTest, opEquals_IsDifferentUmlauts_ReturnFalse) {
	const Path path(L"Q:\\foo\u00E0");

	EXPECT_FALSE(path == Path(L"Q:\\fo\u00E1"));
	EXPECT_TRUE(path != Path(L"Q:\\fo\u00E1"));
}

TEST_F(PathTest, opEquals_ErrorComparing_ThrowException) {
	EXPECT_CALL(m_win32, CompareStringOrdinal(t::StrEq(L"Q:\\foo"), t::_, t::StrEq(L"Q:\\foo"), t::_, t::_))
		.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_NOT_SUPPORTED, 0))
		.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_NOT_SUPPORTED, 0));
	const Path path(LR"(Q:\foo)");

	EXPECT_THROW(path == Path(LR"(Q:\foo)"), m3c::windows_exception);
	EXPECT_THROW(path != Path(LR"(Q:\foo)"), m3c::windows_exception);
}

TEST_F(PathTest, opAppend_Directory_AppendDirectory) {
	Path path(LR"(Q:\foo)");

	path /= L"bar";

	EXPECT_STREQ(LR"(Q:\foo\bar)", path.c_str());
}

TEST_F(PathTest, opAppend_DirectoryWithSeparator_AppendDirectory) {
	Path path(LR"(Q:\foo)");

	path /= LR"(\bar)";

	EXPECT_STREQ(LR"(Q:\foo\bar)", path.c_str());
}

TEST_F(PathTest, opAppend_Empty_DoNotChange) {
	Path path(LR"(Q:\foo)");

	path /= L"";

	EXPECT_STREQ(LR"(Q:\foo)", path.c_str());
}

TEST_F(PathTest, opAppend_RootPath_ChangeRoot) {
	Path path(LR"(Q:\foo)");

	path /= LR"(R:\bar\)";

	EXPECT_STREQ(LR"(R:\bar)", path.c_str());
}

TEST_F(PathTest, opAppend_LongPath_MakeLongPath) {
	Path path(LR"(Q:\foo)");
	std::wstring name(MAX_PATH, L'x');

	path /= name;

	EXPECT_EQ(LR"(\\?\Q:\foo\)" + name, path.c_str());
}

TEST_F(PathTest, opAppend_LongPathWithDotDot_MakeLongPathAndRemoveDotDot) {
	Path path(LR"(Q:\foo)");
	const std::wstring name(MAX_PATH, L'x');

	path /= (name + LR"(\bar\..\baz)");

	EXPECT_EQ(LR"(\\?\Q:\foo\)" + name + LR"(\baz)", path.c_str());
}

TEST_F(PathTest, opAppend_DotDotToLong_RemoveDotDot) {
	const std::wstring name = LR"(Q:\)" + std::wstring(MAX_PATH, L'x') + LR"(\foo)";
	Path path(name);

	path /= LR"(bar\..\baz)";

	EXPECT_EQ(LR"(\\?\)" + name + LR"(\baz)", path.c_str());
}

TEST_F(PathTest, opAppend_ErrorAppending_ThrowException) {
	EXPECT_CALL(m_win32, PathCchAppendEx(t::StrEq(L"Q:\\foo"), t::_, t::StrEq(L"bar"), t::_))
		.WillOnce(t::Return(HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)));
	Path path(LR"(Q:\foo)");

	EXPECT_THROW(path /= L"bar", m3c::com_exception);
	EXPECT_STREQ(LR"(Q:\foo)", path.c_str());
}

TEST_F(PathTest, opAppend_ErrorRemovingBackslash_ThrowException) {
	EXPECT_CALL(m_win32, PathCchAppendEx(t::StrEq(L"Q:\\foo"), t::_, t::StrEq(L"bar"), t::_));
	EXPECT_CALL(m_win32, PathCchRemoveBackslashEx(t::StrEq(L"Q:\\foo\\bar"), t::_, t::_, t::_))
		.WillOnce(t::Return(HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)));
	Path path(LR"(Q:\foo)");

	EXPECT_THROW(path /= L"bar", m3c::com_exception);
	EXPECT_STREQ(LR"(Q:\foo)", path.c_str());
}

TEST_F(PathTest, opConcat_Directory_ReturnWithDirectoryAppended) {
	const Path path(LR"(Q:\foo)");

	const Path result = path / L"bar";

	EXPECT_STREQ(LR"(Q:\foo\bar)", result.c_str());
}

TEST_F(PathTest, opConcat_DirectoryWithSeparator_ReturnWithDirectoryAppended) {
	const Path path(LR"(Q:\foo)");

	const Path result = path / LR"(\bar)";

	EXPECT_STREQ(LR"(Q:\foo\bar)", result.c_str());
}

TEST_F(PathTest, opConcat_Empty_ReturnUnchanged) {
	const Path path(LR"(Q:\foo)");

	const Path result = path / L"";

	EXPECT_STREQ(LR"(Q:\foo)", result.c_str());
}

TEST_F(PathTest, opConcat_RootPath_ReturnNewRoot) {
	const Path path(LR"(Q:\foo)");

	const Path result = path / LR"(R:\bar\)";

	EXPECT_STREQ(LR"(R:\bar)", result.c_str());
}

TEST_F(PathTest, opConcat_LongPath_ReturnLongPath) {
	const Path path(LR"(Q:\foo)");
	std::wstring name(MAX_PATH, L'x');

	const Path result = path / name;

	EXPECT_EQ(LR"(\\?\Q:\foo\)" + name, result.c_str());
}

TEST_F(PathTest, opConcat_LongPathWithDotDot_ReturnLongPathAndRemoveDotDot) {
	const Path path(LR"(Q:\foo)");
	const std::wstring name(MAX_PATH, L'x');

	const Path result = path / (name + LR"(\bar\..\baz)");

	EXPECT_EQ(LR"(\\?\Q:\foo\)" + name + LR"(\baz)", result.c_str());
}

TEST_F(PathTest, opConcat_DotDotToLong_ReturnWithDotDotRemoved) {
	const std::wstring name = LR"(Q:\)" + std::wstring(MAX_PATH, L'x') + LR"(\foo)";
	const Path path(name);

	const Path result = path / LR"(bar\..\baz)";

	EXPECT_EQ(LR"(\\?\)" + name + LR"(\baz)", result.c_str());
}

TEST_F(PathTest, opStdWstring_call_ReturnString) {
	const Path path(LR"(Q:\foo)");

	// static_cast gives error in Intellisense
	const std::wstring value = (path.operator const std::wstring &());
	EXPECT_EQ(LR"(Q:\foo)", value);
}

TEST_F(PathTest, Exists_IsSystemDirectory_ReturnTrue) {
	const Path path = TestUtils::GetSystemDirectory();

	EXPECT_TRUE(path.Exists());
}

TEST_F(PathTest, Exists_IsSystemRoot_ReturnTrue) {
	Path path = TestUtils::GetSystemDirectory();
	for (Path parent = path.GetParent(); path != parent; parent = path.GetParent()) {
		path = parent;
	}

	EXPECT_TRUE(path.Exists());
}

TEST_F(PathTest, Exists_DoesNotExist_ReturnFalse) {
	const Path path(LR"(Q:\file_does_not_exist.321)");

	EXPECT_FALSE(path.Exists());
}

TEST_F(PathTest, Exists_Error_ThrowException) {
	EXPECT_CALL(m_win32, GetFileAttributesW(t::StrEq(L"Q:\\foo.txt")))
		.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_NOT_SUPPORTED, INVALID_FILE_ATTRIBUTES));
	const Path path(LR"(Q:\foo.txt)");

	EXPECT_THROW(path.Exists(), m3c::windows_exception);
}

TEST_F(PathTest, IsDirectory_IsSystemDirectory_ReturnTrue) {
	const Path path = TestUtils::GetSystemDirectory();

	EXPECT_TRUE(path.IsDirectory());
}

TEST_F(PathTest, IsDirectory_IsFileInSystemDirectory_ReturnFalse) {
	Path path = TestUtils::GetSystemDirectory();
	path /= L"user32.dll";
	ASSERT_TRUE(path.Exists());

	EXPECT_FALSE(path.IsDirectory());
}

TEST_F(PathTest, IsDirectory_DoesNotExist_ThrowException) {
	const Path path(LR"(Q:\file_does_not_exist.321)");

	EXPECT_THROW(path.IsDirectory(), m3c::windows_exception);
}

TEST_F(PathTest, IsDirectory_Error_ThrowException) {
	EXPECT_CALL(m_win32, GetFileAttributesW(t::StrEq(L"Q:\\foo.txt")))
		.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_NOT_SUPPORTED, INVALID_FILE_ATTRIBUTES));
	const Path path(LR"(Q:\foo.txt)");

	EXPECT_THROW(path.IsDirectory(), m3c::windows_exception);
}


TEST_F(PathTest, GetParent_IsSubDirectory_ReturnRoot) {
	const Path path(LR"(Q:\foo)");

	const Path parent = path.GetParent();

	EXPECT_STREQ(LR"(Q:\)", parent.c_str());
}

TEST_F(PathTest, GetParent_IsSubDirectoryWithSubDirectory_ReturnSubDirectory) {
	const Path path(LR"(Q:\foo\bar)");

	const Path parent = path.GetParent();

	EXPECT_STREQ(LR"(Q:\foo)", parent.c_str());
}

TEST_F(PathTest, GetParent_IsSubDirectoryWithFile_ReturnSubDirectory) {
	const Path path(LR"(Q:\foo\bar.txt)");

	const Path parent = path.GetParent();

	EXPECT_STREQ(LR"(Q:\foo)", parent.c_str());
}

TEST_F(PathTest, GetParent_IsDriveRoot_ReturnRoot) {
	const Path path(LR"(Q:\)");

	const Path parent = path.GetParent();

	EXPECT_STREQ(LR"(Q:\)", parent.c_str());
}

TEST_F(PathTest, GetParent_IsVolumeRoot_ReturnRoot) {
	const Path path(kTestVolume);

	const Path parent = path.GetParent();

	EXPECT_EQ(kTestVolume + LR"(\)", parent.c_str());
}

TEST_F(PathTest, GetParent_ErrorGettingParent_ThrowException) {
	EXPECT_CALL(m_win32, PathCchRemoveFileSpec(t::StrEq(L"Q:\\foo\\bar.txt"), t::_))
		.WillOnce(t::Return(HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)));
	const Path path(LR"(Q:\foo\bar.txt)");

	EXPECT_THROW(path.GetParent(), m3c::com_exception);
}

TEST_F(PathTest, GetFilename_IsFileInRoot_ReturnFilename) {
	const Path path(LR"(Q:\foo.txt)");

	const Filename filename = path.GetFilename();

	EXPECT_STREQ(LR"(foo.txt)", filename.c_str());
}

TEST_F(PathTest, GetFilename_IsFileInVolumeRoot_ReturnFilename) {
	const Path path(kTestVolume + LR"(\foo.txt)");

	const Filename filename = path.GetFilename();

	EXPECT_STREQ(LR"(foo.txt)", filename.c_str());
}

TEST_F(PathTest, GetFilename_IsFileInSubDirectory_ReturnFilename) {
	const Path path(LR"(Q:\bar\foo.txt)");

	const Filename filename = path.GetFilename();

	EXPECT_STREQ(LR"(foo.txt)", filename.c_str());
}

TEST_F(PathTest, GetFilename_IsFileInVolumeSubDirectory_ReturnFilename) {
	const Path path(kTestVolume + LR"(\bar\foo.txt)");

	const Filename filename = path.GetFilename();

	EXPECT_STREQ(LR"(foo.txt)", filename.c_str());
}

TEST_F(PathTest, GetFilename_IsDirectoryInRoot_ReturnFilename) {
	const Path path(LR"(Q:\foo)");

	const Filename filename = path.GetFilename();

	EXPECT_STREQ(LR"(foo)", filename.c_str());
}

TEST_F(PathTest, GetFilename_IsDirectoryInVolumeRoot_ReturnFilename) {
	const Path path(kTestVolume + LR"(\foo)");

	const Filename filename = path.GetFilename();

	EXPECT_STREQ(LR"(foo)", filename.c_str());
}

TEST_F(PathTest, GetFilename_IsDirectoryInSubDirectory_ReturnFilename) {
	const Path path(LR"(Q:\bar\foo)");

	const Filename filename = path.GetFilename();

	EXPECT_STREQ(LR"(foo)", filename.c_str());
}

TEST_F(PathTest, GetFilename_IsDriveRoot_ReturnEmpty) {
	const Path path(LR"(Q:\)");

	const Filename filename = path.GetFilename();

	EXPECT_STREQ(LR"()", filename.c_str());
}

TEST_F(PathTest, GetFilename_IsVolumeRoot_ReturnEmpty) {
	const Path path(kTestVolume + LR"(\)");

	const Filename filename = path.GetFilename();

	EXPECT_STREQ(LR"()", filename.c_str());
}

TEST_F(PathTest, GetFilename_ErrorGettingPathBufferSize_ThrowError) {
	EXPECT_CALL(m_win32, GetFullPathNameW(t::StrEq(LR"(Q:\bar\foo.txt)"), 0, t::_, nullptr))
		.WillOnce(t::DoDefault())
		.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_NOT_SUPPORTED, 0));
	const Path path(LR"(Q:\bar\foo.txt)");

	EXPECT_THROW(path.GetFilename(), m3c::windows_exception);
}

TEST_F(PathTest, GetFilename_ErrorGettingPathName_ThrowError) {
	EXPECT_CALL(m_win32, GetFullPathNameW(t::StrEq(LR"(Q:\bar\foo.txt)"), t::Gt(0u), t::_, t::NotNull()))
		.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_NOT_SUPPORTED, 0));
	const Path path(LR"(Q:\bar\foo.txt)");

	EXPECT_THROW(path.GetFilename(), m3c::windows_exception);
}

TEST_F(PathTest, ForceDelete_NotExists_ThrowsException) {
	const Path path(kTestVolume + LR"(\foo)");
	ASSERT_FALSE(path.Exists());

	EXPECT_THROW(path.ForceDelete(), m3c::windows_exception);
}

TEST_P(PathDeleteTest, ForceDelete_Exists_Delete) {
	ASSERT_TRUE(kTempPath.Exists());
	if (std::get<0>(GetParam()) == Mode::kHardlink) {
		ASSERT_TRUE(kHardlinkPath.Exists());
	}

	kTempPath.ForceDelete();
	EXPECT_FALSE(kTempPath.Exists());

	if (std::get<0>(GetParam()) == Mode::kHardlink) {
		EXPECT_TRUE(kHardlinkPath.Exists());
		EXPECT_EQ(static_cast<std::underlying_type_t<Attributes>>(std::get<1>(GetParam()) & ~Attributes::kNormal), GetFileAttributesW(kHardlinkPath.c_str()) & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
	}
}

TEST_P(PathDeleteTest, ForceDelete_ErrorGettingAttributes_ThrowException) {
	t::InSequence s;
	t::MockFunction<void(bool)> check;

	EXPECT_CALL(check, Call(true));
	EXPECT_CALL(m_win32, GetFileAttributesW(t::StrEq(kTempPath.c_str())))
		.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_NOT_SUPPORTED, INVALID_FILE_ATTRIBUTES));
	EXPECT_CALL(check, Call(false));

	ASSERT_TRUE(kTempPath.Exists());
	if (std::get<0>(GetParam()) == Mode::kHardlink) {
		ASSERT_TRUE(kHardlinkPath.Exists());
	}

	check.Call(true);
	EXPECT_THROW(kTempPath.ForceDelete(), m3c::windows_exception);
	check.Call(false);
	EXPECT_TRUE(kTempPath.Exists());
	EXPECT_EQ(static_cast<std::underlying_type_t<Attributes>>(std::get<1>(GetParam()) & ~Attributes::kNormal), GetFileAttributesW(kTempPath.c_str()) & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));

	if (std::get<0>(GetParam()) == Mode::kHardlink) {
		EXPECT_TRUE(kHardlinkPath.Exists());
		EXPECT_EQ(static_cast<std::underlying_type_t<Attributes>>(std::get<1>(GetParam()) & ~Attributes::kNormal), GetFileAttributesW(kHardlinkPath.c_str()) & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
	}
}

TEST_P(PathDeleteTest, ForceDelete_ErrorRemovingReadOnlyAttribute_ThrowException) {
	if (!(std::get<1>(GetParam()) & Attributes::kReadOnly)) {
		SUCCEED();
		return;
	}

	t::InSequence s;
	t::MockFunction<void(bool)> check;
	EXPECT_CALL(check, Call(true));
	EXPECT_CALL(m_win32, SetFileAttributesW(t::StrEq(kTempPath.c_str()), t::_))
		.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_NOT_SUPPORTED, FALSE));
	EXPECT_CALL(check, Call(false));

	ASSERT_TRUE(kTempPath.Exists());
	if (std::get<0>(GetParam()) == Mode::kHardlink) {
		ASSERT_TRUE(kHardlinkPath.Exists());
	}

	check.Call(true);
	EXPECT_THROW(kTempPath.ForceDelete(), m3c::windows_exception);
	check.Call(false);
	EXPECT_TRUE(kTempPath.Exists());
	EXPECT_EQ(static_cast<std::underlying_type_t<Attributes>>(std::get<1>(GetParam()) & ~Attributes::kNormal), GetFileAttributesW(kTempPath.c_str()) & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));

	if (std::get<0>(GetParam()) == Mode::kHardlink) {
		EXPECT_TRUE(kHardlinkPath.Exists());
		EXPECT_EQ(static_cast<std::underlying_type_t<Attributes>>(std::get<1>(GetParam()) & ~Attributes::kNormal), GetFileAttributesW(kHardlinkPath.c_str()) & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
	}
}

TEST_P(PathDeleteTest, ForceDelete_ErrorDeleting_ThrowException) {
	t::InSequence s;
	t::MockFunction<void(bool)> check;
	EXPECT_CALL(check, Call(true));
	if (std::get<0>(GetParam()) == Mode::kDirectory) {
		EXPECT_CALL(m_win32, RemoveDirectoryW(t::StrEq(kTempPath.c_str())))
			.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_NOT_SUPPORTED, FALSE));

	} else {
		EXPECT_CALL(m_win32, DeleteFileW(t::StrEq(kTempPath.c_str())))
			.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_NOT_SUPPORTED, FALSE));
	}
	EXPECT_CALL(check, Call(false));

	ASSERT_TRUE(kTempPath.Exists());
	if (std::get<0>(GetParam()) == Mode::kHardlink) {
		ASSERT_TRUE(kHardlinkPath.Exists());
	}

	check.Call(true);
	EXPECT_THROW(kTempPath.ForceDelete(), m3c::windows_exception);
	check.Call(false);
	EXPECT_TRUE(kTempPath.Exists());
	EXPECT_EQ(static_cast<std::underlying_type_t<Attributes>>(std::get<1>(GetParam()) & ~Attributes::kNormal), GetFileAttributesW(kTempPath.c_str()) & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));

	if (std::get<0>(GetParam()) == Mode::kHardlink) {
		EXPECT_TRUE(kHardlinkPath.Exists());
		EXPECT_EQ(static_cast<std::underlying_type_t<Attributes>>(std::get<1>(GetParam()) & ~Attributes::kNormal), GetFileAttributesW(kHardlinkPath.c_str()) & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
	}
}

TEST_P(PathDeleteTest, ForceDelete_ErrorRestoringReadOnlyAttributeAfterDeleteError_ThrowException) {
	if (!(std::get<1>(GetParam()) & Attributes::kReadOnly)) {
		SUCCEED();
		return;
	}

	m_requireReadOnly = false;

	t::InSequence s;
	t::MockFunction<void(bool)> check;
	EXPECT_CALL(check, Call(true));
	EXPECT_CALL(m_win32, SetFileAttributesW(t::StrEq(kTempPath.c_str()), t::_));  // remove attribute
	if (std::get<0>(GetParam()) == Mode::kDirectory) {
		EXPECT_CALL(m_win32, RemoveDirectoryW(t::StrEq(kTempPath.c_str())))
			.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_ACCESS_DENIED, FALSE));

	} else {
		EXPECT_CALL(m_win32, DeleteFileW(t::StrEq(kTempPath.c_str())))
			.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_ACCESS_DENIED, FALSE));
	}
	EXPECT_CALL(m_win32, SetFileAttributesW(t::StrEq(kTempPath.c_str()), t::_))  // set attribute
		.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_NOT_SUPPORTED, FALSE));
	EXPECT_CALL(check, Call(false));

	ASSERT_TRUE(kTempPath.Exists());
	if (std::get<0>(GetParam()) == Mode::kHardlink) {
		ASSERT_TRUE(kHardlinkPath.Exists());
	}

	check.Call(true);
	EXPECT_THROW(kTempPath.ForceDelete(), m3c::windows_exception);
	check.Call(false);
	EXPECT_TRUE(kTempPath.Exists());
	EXPECT_EQ(static_cast<std::underlying_type_t<Attributes>>(std::get<1>(GetParam()) & ~Attributes::kNormal & ~Attributes::kReadOnly), GetFileAttributesW(kTempPath.c_str()) & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));

	if (std::get<0>(GetParam()) == Mode::kHardlink) {
		EXPECT_TRUE(kHardlinkPath.Exists());
		EXPECT_EQ(static_cast<std::underlying_type_t<Attributes>>(std::get<1>(GetParam()) & ~Attributes::kNormal & ~Attributes::kReadOnly), GetFileAttributesW(kHardlinkPath.c_str()) & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
	}
}

TEST_P(PathDeleteTest, ForceDelete_ErrorFindingFirstNameBufferSize_ThrowException) {
	if (!(std::get<1>(GetParam()) & Attributes::kReadOnly) || std::get<0>(GetParam()) == Mode::kDirectory) {
		SUCCEED();
		return;
	}

	t::InSequence s;
	t::MockFunction<void(bool)> check;
	EXPECT_CALL(check, Call(true));
	EXPECT_CALL(m_win32, FindFirstFileNameW(t::StrEq(kTempPath.c_str()), t::_, t::Pointee(0u), t::_))
		.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_NOT_SUPPORTED, INVALID_HANDLE_VALUE));
	EXPECT_CALL(check, Call(false));

	ASSERT_TRUE(kTempPath.Exists());
	if (std::get<0>(GetParam()) == Mode::kHardlink) {
		ASSERT_TRUE(kHardlinkPath.Exists());
	}

	check.Call(true);
	EXPECT_THROW(kTempPath.ForceDelete(), m3c::windows_exception);
	check.Call(false);
	EXPECT_TRUE(kTempPath.Exists());
	EXPECT_EQ(static_cast<std::underlying_type_t<Attributes>>(std::get<1>(GetParam()) & ~Attributes::kNormal), GetFileAttributesW(kTempPath.c_str()) & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));

	if (std::get<0>(GetParam()) == Mode::kHardlink) {
		EXPECT_TRUE(kHardlinkPath.Exists());
		EXPECT_EQ(static_cast<std::underlying_type_t<Attributes>>(std::get<1>(GetParam()) & ~Attributes::kNormal), GetFileAttributesW(kHardlinkPath.c_str()) & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
	}
}

TEST_P(PathDeleteTest, ForceDelete_ErrorFindingFirstName_ThrowException) {
	if (!(std::get<1>(GetParam()) & Attributes::kReadOnly) || std::get<0>(GetParam()) == Mode::kDirectory) {
		SUCCEED();
		return;
	}

	t::InSequence s;
	t::MockFunction<void(bool)> check;
	EXPECT_CALL(check, Call(true));
	EXPECT_CALL(m_win32, FindFirstFileNameW(t::StrEq(kTempPath.c_str()), t::_, t::Pointee(t::Gt(0u)), t::_))
		.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_NOT_SUPPORTED, INVALID_HANDLE_VALUE));
	EXPECT_CALL(check, Call(false));

	ASSERT_TRUE(kTempPath.Exists());
	if (std::get<0>(GetParam()) == Mode::kHardlink) {
		ASSERT_TRUE(kHardlinkPath.Exists());
	}

	check.Call(true);
	EXPECT_THROW(kTempPath.ForceDelete(), m3c::windows_exception);
	check.Call(false);
	EXPECT_TRUE(kTempPath.Exists());
	EXPECT_EQ(static_cast<std::underlying_type_t<Attributes>>(std::get<1>(GetParam()) & ~Attributes::kNormal), GetFileAttributesW(kTempPath.c_str()) & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));

	if (std::get<0>(GetParam()) == Mode::kHardlink) {
		EXPECT_TRUE(kHardlinkPath.Exists());
		EXPECT_EQ(static_cast<std::underlying_type_t<Attributes>>(std::get<1>(GetParam()) & ~Attributes::kNormal), GetFileAttributesW(kHardlinkPath.c_str()) & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
	}
}

TEST_P(PathDeleteTest, ForceDelete_ErrorFindingSecondNameBufferSize_ThrowException) {
	if (!(std::get<1>(GetParam()) & Attributes::kReadOnly) || std::get<0>(GetParam()) == Mode::kDirectory) {
		SUCCEED();
		return;
	}

	t::InSequence s;
	t::MockFunction<void(bool)> check;
	EXPECT_CALL(check, Call(true));
	EXPECT_CALL(m_win32, FindNextFileNameW(t::_, t::Pointee(0), t::_))
		.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_NOT_SUPPORTED, FALSE));
	EXPECT_CALL(check, Call(false));

	ASSERT_TRUE(kTempPath.Exists());
	if (std::get<0>(GetParam()) == Mode::kHardlink) {
		ASSERT_TRUE(kHardlinkPath.Exists());
	}

	check.Call(true);
	EXPECT_THROW(kTempPath.ForceDelete(), m3c::windows_exception);
	check.Call(false);
	EXPECT_TRUE(kTempPath.Exists());
	EXPECT_EQ(static_cast<std::underlying_type_t<Attributes>>(std::get<1>(GetParam()) & ~Attributes::kNormal), GetFileAttributesW(kTempPath.c_str()) & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));

	if (std::get<0>(GetParam()) == Mode::kHardlink) {
		EXPECT_TRUE(kHardlinkPath.Exists());
		EXPECT_EQ(static_cast<std::underlying_type_t<Attributes>>(std::get<1>(GetParam()) & ~Attributes::kNormal), GetFileAttributesW(kHardlinkPath.c_str()) & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
	}
}

TEST_P(PathDeleteTest, ForceDelete_ErrorFindingSecondName_ThrowException) {
	if (!(std::get<1>(GetParam()) & Attributes::kReadOnly) || std::get<0>(GetParam()) != Mode::kHardlink) {
		SUCCEED();
		return;
	}

	t::InSequence s;
	t::MockFunction<void(bool)> check;
	EXPECT_CALL(check, Call(true));
	EXPECT_CALL(m_win32, FindNextFileNameW(t::_, t::Pointee(t::Gt(0u)), t::_))
		.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_NOT_SUPPORTED, FALSE));
	EXPECT_CALL(check, Call(false));

	ASSERT_TRUE(kTempPath.Exists());
	if (std::get<0>(GetParam()) == Mode::kHardlink) {
		ASSERT_TRUE(kHardlinkPath.Exists());
	}

	check.Call(true);
	EXPECT_THROW(kTempPath.ForceDelete(), m3c::windows_exception);
	check.Call(false);
	EXPECT_TRUE(kTempPath.Exists());
	EXPECT_EQ(static_cast<std::underlying_type_t<Attributes>>(std::get<1>(GetParam()) & ~Attributes::kNormal), GetFileAttributesW(kTempPath.c_str()) & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));

	if (std::get<0>(GetParam()) == Mode::kHardlink) {
		EXPECT_TRUE(kHardlinkPath.Exists());
		EXPECT_EQ(static_cast<std::underlying_type_t<Attributes>>(std::get<1>(GetParam()) & ~Attributes::kNormal), GetFileAttributesW(kHardlinkPath.c_str()) & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
	}
}

TEST_P(PathDeleteTest, DISABLED_ForceDelete_ErrorGettingHardlinkRoot_ThrowException) {
	if (!(std::get<1>(GetParam()) & Attributes::kReadOnly) || std::get<0>(GetParam()) != Mode::kHardlink) {
		SUCCEED();
		return;
	}
	m_requireReadOnly = false;

	t::InSequence s;
	t::MockFunction<void(bool)> check;
	EXPECT_CALL(check, Call(true));
	// The following EXPECT does not work
	EXPECT_CALL(m_win32, PathCchSkipRoot(t::StrEq(kTempPath.c_str()), t::_))
		.WillOnce(t::Return(HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)));
	EXPECT_CALL(check, Call(false));

	ASSERT_TRUE(kTempPath.Exists());
	if (std::get<0>(GetParam()) == Mode::kHardlink) {
		ASSERT_TRUE(kHardlinkPath.Exists());
	}

	check.Call(true);
	EXPECT_THROW(kTempPath.ForceDelete(), m3c::com_exception);
	check.Call(false);
	EXPECT_FALSE(kTempPath.Exists());

	if (std::get<0>(GetParam()) == Mode::kHardlink) {
		EXPECT_TRUE(kHardlinkPath.Exists());
		EXPECT_EQ(static_cast<std::underlying_type_t<Attributes>>(std::get<1>(GetParam()) & ~Attributes::kNormal & ~Attributes::kReadOnly), GetFileAttributesW(kHardlinkPath.c_str()) & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
	}
}

TEST_P(PathDeleteTest, ForceDelete_ErrorBuildingHardlinkPathForSecondName_ThrowException) {
	if (!(std::get<1>(GetParam()) & Attributes::kReadOnly) || std::get<0>(GetParam()) != Mode::kHardlink) {
		SUCCEED();
		return;
	}

	m_requireReadOnly = false;

	t::InSequence s;
	t::MockFunction<void(bool)> check;
	EXPECT_CALL(check, Call(true));
	EXPECT_CALL(m_win32, SetFileAttributesW(t::StrEq(kHardlinkPath.c_str()), t::_))
		.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_ACCESS_DENIED, FALSE));
	EXPECT_CALL(m_win32, PathCchCombineEx(t::_, t::_, t::StrEq(kTempPath.c_str()), t::_, t::_))
		.WillOnce(t::Return(HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)));
	EXPECT_CALL(check, Call(false));

	ASSERT_TRUE(kTempPath.Exists());
	if (std::get<0>(GetParam()) == Mode::kHardlink) {
		ASSERT_TRUE(kHardlinkPath.Exists());
	}

	check.Call(true);
	EXPECT_THROW(kTempPath.ForceDelete(), m3c::com_exception);
	check.Call(false);
	EXPECT_FALSE(kTempPath.Exists());

	if (std::get<0>(GetParam()) == Mode::kHardlink) {
		EXPECT_TRUE(kHardlinkPath.Exists());
		EXPECT_EQ(static_cast<std::underlying_type_t<Attributes>>(std::get<1>(GetParam()) & ~Attributes::kNormal & ~Attributes::kReadOnly), GetFileAttributesW(kHardlinkPath.c_str()) & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
	}
}

TEST_P(PathDeleteTest, ForceDelete_ErrorRestoringHardlinkReadOnlyAttributeForSecondName_Return) {
	if (!(std::get<1>(GetParam()) & Attributes::kReadOnly) || std::get<0>(GetParam()) != Mode::kHardlink) {
		SUCCEED();
		return;
	}

	m_requireReadOnly = false;

	t::InSequence s;
	t::MockFunction<void(bool)> check;
	EXPECT_CALL(check, Call(true));
	EXPECT_CALL(m_win32, SetFileAttributesW(t::StrEq(kTempPath.c_str()), t::_));
	EXPECT_CALL(m_win32, SetFileAttributesW(t::StrEq(kHardlinkPath.c_str()), t::_))
		.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_NOT_SUPPORTED, FALSE));
	EXPECT_CALL(check, Call(false));

	ASSERT_TRUE(kTempPath.Exists());
	if (std::get<0>(GetParam()) == Mode::kHardlink) {
		ASSERT_TRUE(kHardlinkPath.Exists());
	}

	check.Call(true);
	EXPECT_THROW(kTempPath.ForceDelete(), m3c::windows_exception);
	check.Call(false);
	EXPECT_FALSE(kTempPath.Exists());

	if (std::get<0>(GetParam()) == Mode::kHardlink) {
		EXPECT_TRUE(kHardlinkPath.Exists());
		EXPECT_EQ(static_cast<std::underlying_type_t<Attributes>>(std::get<1>(GetParam()) & ~Attributes::kNormal & ~Attributes::kReadOnly), GetFileAttributesW(kHardlinkPath.c_str()) & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
	}
}

TEST_P(PathDeleteTest, ForceDelete_ErrorBuildingHardlinkPathForFirstName_ThrowException) {
	if (!(std::get<1>(GetParam()) & Attributes::kReadOnly) || std::get<0>(GetParam()) != Mode::kHardlink) {
		SUCCEED();
		return;
	}

	m_requireReadOnly = false;

	t::InSequence s;
	t::MockFunction<void(bool)> check;
	EXPECT_CALL(check, Call(true));
	EXPECT_CALL(m_win32, PathCchCombineEx(t::_, t::_, t::StrEq(kTempPath.c_str()), t::_, t::_))
		.WillOnce(t::Return(HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)));
	EXPECT_CALL(check, Call(false));

	ASSERT_TRUE(kTempPath.Exists());
	if (std::get<0>(GetParam()) == Mode::kHardlink) {
		ASSERT_TRUE(kHardlinkPath.Exists());
	}

	check.Call(true);
	EXPECT_THROW(kTempPath.ForceDelete(), m3c::com_exception);
	check.Call(false);
	EXPECT_FALSE(kTempPath.Exists());

	if (std::get<0>(GetParam()) == Mode::kHardlink) {
		EXPECT_TRUE(kHardlinkPath.Exists());
		EXPECT_EQ(static_cast<std::underlying_type_t<Attributes>>(std::get<1>(GetParam()) & ~Attributes::kNormal & ~Attributes::kReadOnly), GetFileAttributesW(kHardlinkPath.c_str()) & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
	}
}

TEST_P(PathDeleteTest, ForceDelete_ErrorRestoringHardlinkReadOnlyAttributeForFirstName_Return) {
	if (!(std::get<1>(GetParam()) & Attributes::kReadOnly) || std::get<0>(GetParam()) != Mode::kHardlink) {
		SUCCEED();
		return;
	}

	m_requireReadOnly = false;

	t::InSequence s;
	t::MockFunction<void(bool)> check;
	EXPECT_CALL(check, Call(true));
	EXPECT_CALL(m_win32, SetFileAttributesW(t::StrEq(kHardlinkPath.c_str()), t::_))
		.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_NOT_SUPPORTED, FALSE));
	EXPECT_CALL(m_win32, SetFileAttributesW(t::StrEq(kTempPath.c_str()), t::_))
		.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_NOT_SUPPORTED, FALSE));
	EXPECT_CALL(check, Call(false));

	ASSERT_TRUE(kTempPath.Exists());
	if (std::get<0>(GetParam()) == Mode::kHardlink) {
		ASSERT_TRUE(kHardlinkPath.Exists());
	}

	check.Call(true);
	EXPECT_THROW(kTempPath.ForceDelete(), m3c::windows_exception);
	check.Call(false);
	EXPECT_FALSE(kTempPath.Exists());

	if (std::get<0>(GetParam()) == Mode::kHardlink) {
		EXPECT_TRUE(kHardlinkPath.Exists());
		EXPECT_EQ(static_cast<std::underlying_type_t<Attributes>>(std::get<1>(GetParam()) & ~Attributes::kNormal & ~Attributes::kReadOnly), GetFileAttributesW(kHardlinkPath.c_str()) & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
	}
}


INSTANTIATE_TEST_SUITE_P(Attributes, PathDeleteTest, t::Combine(t::Values(Mode::kDirectory, Mode::kFile, Mode::kHardlink), t::Values(Attributes::kNormal, Attributes::kReadOnly, Attributes::kHidden, Attributes::kSystem, Attributes::kHidden | Attributes::kSystem, Attributes::kReadOnly | Attributes::kHidden | Attributes::kSystem)));

}  // namespace systools::test
