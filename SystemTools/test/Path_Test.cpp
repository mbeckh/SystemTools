#include "systools/Path.h"

#include "TestUtils.h"

#include <m3c/exception.h>
#include <m3c/handle.h>

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
	fn_(4, HRESULT, APIENTRY, PathCchCanonicalizeEx,                                            \
		(PWSTR pszPathOut, size_t cchPathOut, PCWSTR pszPathIn, ULONG dwFlags),                 \
		(pszPathOut, cchPathOut, pszPathIn, dwFlags),                                           \
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


class Filename_Test : public t::Test {
protected:
	void TearDown() override {
		t::Mock::VerifyAndClearExpectations(&m_win32);
		DTGM_DETACH_API_MOCK(Win32);
	}

protected:
	DTGM_DEFINE_NICE_API_MOCK(Win32, m_win32);
};

class Path_Test : public t::Test {
protected:
	void TearDown() override {
		t::Mock::VerifyAndClearExpectations(&m_win32);
		DTGM_DETACH_API_MOCK(Win32);
	}

protected:
	DTGM_DEFINE_NICE_API_MOCK(Win32, m_win32);
};

namespace {

const std::wstring kTestVolume = LR"(\\?\Volume{00112233-4455-6677-8899-AABBCCDDEEFF})";

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
		!first &&*os << "+";
		*os << "ReadOnly";
		first = false;
	}
	if (attributes & Attributes::kHidden) {
		!first &&*os << "+";
		*os << "Hidden";
		first = false;
	}
	if (attributes & Attributes::kSystem) {
		!first &&*os << "+";
		*os << "System";
		first = false;
	}
}

}  // namespace

class Path_DeleteTest : public t::Test
	, public t::WithParamInterface<std::tuple<Mode, Attributes>> {
protected:
	void SetUp() override {
		if (std::get<0>(GetParam()) == Mode::kDirectory) {
			ASSERT_TRUE(CreateDirectoryW(kTempPath.c_str(), nullptr));
			ASSERT_TRUE(SetFileAttributesW(kTempPath.c_str(), static_cast<std::underlying_type_t<Attributes>>(std::get<1>(GetParam()))));
		}
		if (std::get<0>(GetParam()) == Mode::kFile || std::get<0>(GetParam()) == Mode::kHardlink) {
			const m3c::handle hFile = CreateFileW(kTempPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, static_cast<std::underlying_type_t<Attributes>>(std::get<1>(GetParam())), nullptr);
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

	void TearDown() override {
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

protected:
	const Path kTempPath = TestUtils::GetTempDirectory() / L"23220209-1205-1000-8000-000000001001.0.test";
	const Path kHardlinkPath = TestUtils::GetTempDirectory() / L"23220209-1205-1000-8000-000000001001.1.test";

protected:
	bool m_requireReadOnly = true;

protected:
	DTGM_DEFINE_API_MOCK(Win32, m_win32);
};


//
// Filename
//

TEST_F(Filename_Test, ctor_FromString_MakeFilename) {
	const Filename filename(L"foo");

	EXPECT_STREQ(L"foo", filename.c_str());
}

TEST_F(Filename_Test, ctor_FromCharacters_MakeFilename) {
	const Filename filename(L"foo", 2);

	EXPECT_STREQ(L"fo", filename.c_str());
}

TEST_F(Filename_Test, opCompare_IsSame_CompareEqual) {
	const Filename filename(L"foo");

	EXPECT_TRUE(filename == Filename(L"foo"));
	EXPECT_FALSE(filename != Filename(L"foo"));
	EXPECT_FALSE(filename < Filename(L"foo"));
	EXPECT_TRUE(filename <= Filename(L"foo"));
	EXPECT_FALSE(filename > Filename(L"foo"));
	EXPECT_TRUE(filename >= Filename(L"foo"));

	EXPECT_TRUE(filename.IsSameStringAs(Filename(L"foo")));
	EXPECT_EQ(filename.hash(), Filename(L"foo").hash());
}

TEST_F(Filename_Test, opCompare_IsIdentity_CompareEqual) {
	const Filename filename(L"foo");

	EXPECT_TRUE(filename == filename);
	EXPECT_FALSE(filename != filename);
	EXPECT_FALSE(filename < filename);
	EXPECT_TRUE(filename <= filename);
	EXPECT_FALSE(filename > filename);
	EXPECT_TRUE(filename >= filename);

	EXPECT_TRUE(filename.IsSameStringAs(filename));
	EXPECT_EQ(filename.hash(), filename.hash());
}

TEST_F(Filename_Test, opCompare_IsSameWithDifferentCase_CompareEqual) {
	const Filename filename(L"foo");

	EXPECT_TRUE(filename == Filename(L"Foo"));
	EXPECT_FALSE(filename != Filename(L"Foo"));
	EXPECT_FALSE(filename < Filename(L"Foo"));
	EXPECT_TRUE(filename <= Filename(L"Foo"));
	EXPECT_FALSE(filename > Filename(L"Foo"));
	EXPECT_TRUE(filename >= Filename(L"Foo"));

	EXPECT_FALSE(filename.IsSameStringAs(Filename(L"Foo")));
	EXPECT_EQ(filename.hash(), Filename(L"Foo").hash());
}

TEST_F(Filename_Test, opCompare_IsSameWithUmlaut_CompareEqual) {
	const Filename filename(L"foo\u00E4\u00DF");  // E4 == a umlaut, DF == sz

	EXPECT_TRUE(filename == Filename(L"foo\u00E4\u00DF"));
	EXPECT_FALSE(filename != Filename(L"foo\u00E4\u00DF"));
	EXPECT_FALSE(filename < Filename(L"foo\u00E4\u00DF"));
	EXPECT_TRUE(filename <= Filename(L"foo\u00E4\u00DF"));
	EXPECT_FALSE(filename > Filename(L"foo\u00E4\u00DF"));
	EXPECT_TRUE(filename >= Filename(L"foo\u00E4\u00DF"));

	EXPECT_TRUE(filename.IsSameStringAs(Filename(L"foo\u00E4\u00DF")));
	EXPECT_EQ(filename.hash(), Filename(L"foo\u00E4\u00DF").hash());
}

TEST_F(Filename_Test, opCompare_IsSameWithUmlautAndAccentAndDifferentCase_CompareEqual) {
	const Filename filename(L"foo\u00E4\u00E9");  // E4 == a umlaut, E9 == e accent ´

	EXPECT_TRUE(filename == Filename(L"foo\u00C4\u00C9"));  // C4 == A umlaut, C9 == E accent ´
	EXPECT_FALSE(filename != Filename(L"foo\u00C4\u00C9"));
	EXPECT_FALSE(filename < Filename(L"foo\u00C4\u00C9"));
	EXPECT_TRUE(filename <= Filename(L"foo\u00C4\u00C9"));
	EXPECT_FALSE(filename > Filename(L"foo\u00C4\u00C9"));
	EXPECT_TRUE(filename >= Filename(L"foo\u00C4\u00C9"));

	EXPECT_FALSE(filename.IsSameStringAs(Filename(L"foo\u00C4\u00C9")));
	EXPECT_EQ(filename.hash(), Filename(L"foo\u00C4\u00C9").hash());
}

TEST_F(Filename_Test, opCompare_IsLessThan_CompareLessThan) {
	const Filename filename(L"foo");

	EXPECT_FALSE(filename == Filename(L"zar"));  // E4 == a umlaut, DF == sz
	EXPECT_TRUE(filename != Filename(L"zar"));
	EXPECT_TRUE(filename < Filename(L"zar"));
	EXPECT_TRUE(filename <= Filename(L"zar"));
	EXPECT_FALSE(filename > Filename(L"zar"));
	EXPECT_FALSE(filename >= Filename(L"zar"));

	EXPECT_FALSE(filename.IsSameStringAs(Filename(L"zar")));
	EXPECT_NE(filename.hash(), Filename(L"zar").hash());
}

TEST_F(Filename_Test, opCompare_IsLessThanAndSubstring_CompareLessThan) {
	const Filename filename(L"foo");

	EXPECT_FALSE(filename == Filename(L"fooo"));
	EXPECT_TRUE(filename != Filename(L"fooo"));
	EXPECT_TRUE(filename < Filename(L"fooo"));
	EXPECT_TRUE(filename <= Filename(L"fooo"));
	EXPECT_FALSE(filename > Filename(L"fooo"));
	EXPECT_FALSE(filename >= Filename(L"fooo"));

	EXPECT_FALSE(filename.IsSameStringAs(Filename(L"fooo")));
	EXPECT_NE(filename.hash(), Filename(L"fooo").hash());
}

TEST_F(Filename_Test, opCompare_IsGreatherThan_CompareGreatherThan) {
	const Filename filename(L"foo");

	EXPECT_FALSE(filename == Filename(L"bar"));
	EXPECT_TRUE(filename != Filename(L"bar"));
	EXPECT_FALSE(filename < Filename(L"bar"));
	EXPECT_FALSE(filename <= Filename(L"bar"));
	EXPECT_TRUE(filename > Filename(L"bar"));
	EXPECT_TRUE(filename >= Filename(L"bar"));

	EXPECT_FALSE(filename.IsSameStringAs(Filename(L"bar")));
	EXPECT_NE(filename.hash(), Filename(L"bar").hash());
}

TEST_F(Filename_Test, opCompare_IsGreatherThanAndSubstring_CompareGreatherThan) {
	const Filename filename(L"foo");

	EXPECT_FALSE(filename == Filename(L"fo"));
	EXPECT_TRUE(filename != Filename(L"fo"));
	EXPECT_FALSE(filename < Filename(L"fo"));
	EXPECT_FALSE(filename <= Filename(L"fo"));
	EXPECT_TRUE(filename > Filename(L"fo"));
	EXPECT_TRUE(filename >= Filename(L"fo"));

	EXPECT_FALSE(filename.IsSameStringAs(Filename(L"fo")));
	EXPECT_NE(filename.hash(), Filename(L"fo").hash());
}

TEST_F(Filename_Test, opCompare_IsDifferentForDiaeresisOnly_CompareLessThan) {
	const Filename filename(L"foo");

	EXPECT_FALSE(filename == Filename(L"fo\u00F6"));  // F6 == o umlaut
	EXPECT_TRUE(filename != Filename(L"fo\u00F6"));
	EXPECT_TRUE(filename < Filename(L"fo\u00F6"));
	EXPECT_TRUE(filename <= Filename(L"fo\u00F6"));
	EXPECT_FALSE(filename > Filename(L"fo\u00F6"));
	EXPECT_FALSE(filename >= Filename(L"fo\u00F6"));

	EXPECT_FALSE(filename.IsSameStringAs(Filename(L"fo\u00F6")));
	EXPECT_NE(filename.hash(), Filename(L"fo\u00F6").hash());
}

TEST_F(Filename_Test, opCompare_IsDifferentForAccentOnly_CompareLessThan) {
	const Filename filename(L"foo");

	EXPECT_FALSE(filename == Filename(L"fo\u00F3"));  // F3 == o accent ´
	EXPECT_TRUE(filename != Filename(L"fo\u00F3"));
	EXPECT_TRUE(filename < Filename(L"fo\u00F3"));
	EXPECT_TRUE(filename <= Filename(L"fo\u00F3"));
	EXPECT_FALSE(filename > Filename(L"fo\u00F3"));
	EXPECT_FALSE(filename >= Filename(L"fo\u00F3"));

	EXPECT_FALSE(filename.IsSameStringAs(Filename(L"fo\u00F3")));
	EXPECT_NE(filename.hash(), Filename(L"fo\u00F3").hash());
}

TEST_F(Filename_Test, opCompare_IsDifferentAccents_CompareNotEqual) {
	const Filename filename(L"fo\u00E0");  // E0 == a accent `

	EXPECT_FALSE(filename == Filename(L"fo\u00E1"));  // E1 == a accent ´
	EXPECT_TRUE(filename != Filename(L"fo\u00E1"));
	EXPECT_TRUE(filename < Filename(L"fo\u00E1"));
	EXPECT_TRUE(filename <= Filename(L"fo\u00E1"));
	EXPECT_FALSE(filename > Filename(L"fo\u00E1"));
	EXPECT_FALSE(filename >= Filename(L"fo\u00E1"));

	EXPECT_FALSE(filename.IsSameStringAs(Filename(L"fo\u00E1")));
	EXPECT_NE(filename.hash(), Filename(L"fo\u00E1").hash());
}

TEST_F(Filename_Test, opCompare_ErrorComparing_ThrowException) {
	EXPECT_CALL(m_win32, CompareStringOrdinal(t::StrEq(L"foo"), t::_, t::StrEq(L"foo"), t::_, t::_))
		.Times(6)
		.WillRepeatedly(dtgm::SetLastErrorAndReturn(ERROR_NOT_SUPPORTED, 0));
	const Filename filename(L"foo");

#pragma warning(suppress : 4834)
	EXPECT_THROW(filename == Filename(L"foo"), m3c::windows_exception);
#pragma warning(suppress : 4552)
	EXPECT_THROW(filename != Filename(L"foo"), m3c::windows_exception);
#pragma warning(suppress : 4834)
	EXPECT_THROW(filename < Filename(L"foo"), m3c::windows_exception);
#pragma warning(suppress : 4834)
	EXPECT_THROW(filename <= Filename(L"foo"), m3c::windows_exception);
#pragma warning(suppress : 4834)
	EXPECT_THROW(filename > Filename(L"foo"), m3c::windows_exception);
#pragma warning(suppress : 4834)
	EXPECT_THROW(filename >= Filename(L"foo"), m3c::windows_exception);
}

TEST_F(Filename_Test, swap_ValueWithValue_ValueAndValue) {
	Filename filename(L"foo");
	Filename oth(L"bar");

	filename.swap(oth);

	EXPECT_STREQ(L"bar", filename.c_str());
	EXPECT_STREQ(L"foo", oth.c_str());
}

TEST_F(Filename_Test, stdSwap_ValueWithValue_ValueAndValue) {
	Filename filename(L"foo");
	Filename oth(L"bar");

	std::swap(filename, oth);

	EXPECT_STREQ(L"bar", filename.c_str());
	EXPECT_STREQ(L"foo", oth.c_str());
}

TEST_F(Filename_Test, stdHash_Value_ReturnHash) {
	const std::wstring str(L"foo");
	const Filename filename(str);
	const size_t h = std::hash<Filename>{}(filename);

	EXPECT_EQ(Filename(str).hash(), h);
}

TEST_F(Filename_Test, stdHash_DiffersInCase_HashEquals) {
	const std::wstring str(L"foo");
	const std::wstring oth(L"Foo");
	const Filename filename(str);
	const Filename othFilename(oth);
	const size_t h = std::hash<Filename>{}(filename);
	const size_t o = std::hash<Filename>{}(othFilename);

	ASSERT_TRUE(filename == othFilename);
	EXPECT_EQ(o, h);
}

TEST_F(Filename_Test, stdHash_DiffersInCaseWithUmlaut_HashEquals) {
	const std::wstring str(L"fo\u00F6");
	const std::wstring oth(L"Fo\u00D6");
	const Filename filename(str);
	const Filename othFilename(oth);
	const size_t h = std::hash<Filename>{}(filename);
	const size_t o = std::hash<Filename>{}(othFilename);

	ASSERT_TRUE(filename == othFilename);
	EXPECT_EQ(o, h);
}

//
// Path
//

TEST_F(Path_Test, ctor_IsRelative_MakeAbsolute) {
	const Path systemDirectory = TestUtils::GetSystemDirectory();
	ASSERT_EQ(TRUE, SetCurrentDirectoryW(systemDirectory.c_str()));

	const Path path(L"foo");

	// static_cast gives error in Intellisense
	EXPECT_EQ(systemDirectory.c_str() + std::wstring(LR"(\foo)"), path.c_str());
}

TEST_F(Path_Test, ctor_IsDrive_MakeRoot) {
	const Path path(LR"(Q:)");

	EXPECT_STREQ(LR"(Q:\)", path.c_str());
}

TEST_F(Path_Test, ctor_IsDriveRelative_MakeRoot) {
	const Path path(LR"(Q:sub\foo.txt)");

	EXPECT_STREQ(LR"(Q:\sub\foo.txt)", path.c_str());
}

TEST_F(Path_Test, ctor_IsRoot_MakeRoot) {
	const Path path(LR"(Q:\)");

	EXPECT_STREQ(LR"(Q:\)", path.c_str());
}

TEST_F(Path_Test, ctor_IsAbsolute_MakePath) {
	const Path path(LR"(Q:\foo)");

	EXPECT_STREQ(LR"(Q:\foo)", path.c_str());
}

TEST_F(Path_Test, ctor_IsAbsoluteWithSub_MakePath) {
	const Path path(LR"(Q:\foo\bar)");

	EXPECT_STREQ(LR"(Q:\foo\bar)", path.c_str());
}

TEST_F(Path_Test, ctor_IsAbsoluteWithSubAndBackslash_RemoveBackslash) {
	const Path path(LR"(Q:\foo\bar\)");

	EXPECT_STREQ(LR"(Q:\foo\bar)", path.c_str());
}

TEST_F(Path_Test, ctor_IsAbsoluteWithDot_RemoveDot) {
	const Path path(LR"(Q:\foo\.\bar)");

	EXPECT_STREQ(LR"(Q:\foo\bar)", path.c_str());
}

TEST_F(Path_Test, ctor_IsAbsoluteWithDotDot_RemoveDotDot) {
	const Path path(LR"(Q:\foo\..\bar)");

	EXPECT_STREQ(LR"(Q:\bar)", path.c_str());
}

TEST_F(Path_Test, ctor_IsVolumeRoot_MakeRoot) {
	const Path path(kTestVolume + LR"(\)");

	EXPECT_EQ(kTestVolume + LR"(\)", path.c_str());
}

TEST_F(Path_Test, ctor_IsVolumePath_MakePath) {
	const Path path(kTestVolume + LR"(\foo)");

	EXPECT_EQ(kTestVolume + LR"(\foo)", path.c_str());
}

TEST_F(Path_Test, ctor_IsVolumePathWithBackslash_RemoveBackslash) {
	const Path path(kTestVolume + LR"(\foo\)");

	EXPECT_EQ(kTestVolume + LR"(\foo)", path.c_str());
}

TEST_F(Path_Test, ctor_IsForwardSlash_ConvertToBackslash) {
	const Path path(LR"(Q:/foo/bar)");

	EXPECT_STREQ(LR"(Q:\foo\bar)", path.c_str());
}

TEST_F(Path_Test, ctor_IsLongAbsolute_MakeLongPath) {
	const std::wstring name = LR"(Q:\)" + std::wstring(MAX_PATH, L'x') + LR"(\foo)";
	const Path path(name);

	EXPECT_EQ(LR"(\\?\)" + name, path.c_str());
}

TEST_F(Path_Test, ctor_IsLongRelative_MakeLongPath) {
	const Path systemDirectory = TestUtils::GetSystemDirectory();
	ASSERT_EQ(TRUE, SetCurrentDirectoryW(systemDirectory.c_str()));

	const std::wstring name = std::wstring(MAX_PATH - 10, L'x') + LR"(\foo)";

	const Path path(name);

	// static_cast gives error in Intellisense
	EXPECT_EQ(std::wstring(LR"(\\?\)") + systemDirectory.c_str() + LR"(\)" + name, path.c_str());
}

TEST_F(Path_Test, ctor_ErrorGettingPathName_ThrowError) {
	EXPECT_CALL(m_win32, GetFullPathNameW(t::StrEq(L"Q:\\foo"), t::Gt(0u), t::_, nullptr))
		.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_NOT_SUPPORTED, 0));

	EXPECT_THROW(Path(L"Q:\\foo"), m3c::windows_exception);
}

TEST_F(Path_Test, ctor_ErrorGettingLongPathBufferSize_ThrowError) {
	std::wstring name(MAX_PATH, 'x');
	EXPECT_CALL(m_win32, GetFullPathNameW(t::StrEq(LR"(Q:\foo\)" + name), t::Gt(0u), t::_, nullptr))
		.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_NOT_SUPPORTED, 0));

	EXPECT_THROW(Path(LR"(Q:\foo\)" + name), m3c::windows_exception);
}

TEST_F(Path_Test, ctor_ErrorGettingLongPathName_ThrowError) {
	std::wstring name(MAX_PATH, 'x');
	EXPECT_CALL(m_win32, GetFullPathNameW(t::StrEq(LR"(Q:\foo\)" + name), t::Gt(0u), t::_, nullptr))
		.WillOnce(dtgm::SetLastErrorAndReturn(0, MAX_PATH * 2))
		.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_NOT_SUPPORTED, 0));

	EXPECT_THROW(Path(LR"(Q:\foo\)" + name), m3c::windows_exception);
}

TEST_F(Path_Test, ctor_ErrorNormalizing_ThrowError) {
	EXPECT_CALL(m_win32, PathCchCanonicalizeEx(t::_, t::_, t::StrEq(L"Q:\\foo"), t::_))
		.WillOnce(t::Return(HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)));

	EXPECT_THROW(Path(LR"(Q:\foo)"), m3c::com_exception);
}

TEST_F(Path_Test, ctor_ErrorRemovingBackslash_ThrowError) {
	EXPECT_CALL(m_win32, PathCchRemoveBackslashEx(t::StrEq(L"Q:\\foo"), t::_, t::_, t::_))
		.WillOnce(t::Return(HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)));

	EXPECT_THROW(Path(LR"(Q:\foo)"), m3c::com_exception);
}

TEST_F(Path_Test, ctor_String_MakePath) {
	const std::wstring value(LR"(Q:\foo)");

	const Path path(value);

	EXPECT_STREQ(LR"(Q:\foo)", path.c_str());
}

TEST_F(Path_Test, ctor_StringView_MakePath) {
	const std::wstring_view value(LR"(Q:\foox)", 6);

	const Path path(value);

	EXPECT_STREQ(LR"(Q:\foo)", path.c_str());
}

TEST_F(Path_Test, opCompare_IsSame_CompareEqual) {
	const Path path(LR"(Q:\foo)");

	EXPECT_TRUE(path == Path(LR"(Q:\foo)"));
	EXPECT_FALSE(path != Path(LR"(Q:\foo)"));
	EXPECT_FALSE(path < Path(LR"(Q:\foo)"));
	EXPECT_TRUE(path <= Path(LR"(Q:\foo)"));
	EXPECT_FALSE(path > Path(LR"(Q:\foo)"));
	EXPECT_TRUE(path >= Path(LR"(Q:\foo)"));

	EXPECT_EQ(path.hash(), Path(LR"(Q:\foo)").hash());
}

TEST_F(Path_Test, opCompare_IsIdentity_CompareEqual) {
	const Path path(LR"(Q:\foo)");

	EXPECT_TRUE(path == path);
	EXPECT_FALSE(path != path);
	EXPECT_FALSE(path < path);
	EXPECT_TRUE(path <= path);
	EXPECT_FALSE(path > path);
	EXPECT_TRUE(path >= path);

	EXPECT_EQ(path.hash(), path.hash());
}

TEST_F(Path_Test, opCompare_IsSameWithDifferentCase_CompareEqual) {
	const Path path(LR"(Q:\foo)");

	EXPECT_TRUE(path == Path(LR"(q:\Foo)"));
	EXPECT_FALSE(path != Path(LR"(q:\Foo)"));
	EXPECT_FALSE(path < Path(LR"(q:\Foo)"));
	EXPECT_TRUE(path <= Path(LR"(q:\Foo)"));
	EXPECT_FALSE(path > Path(LR"(q:\Foo)"));
	EXPECT_TRUE(path >= Path(LR"(q:\Foo)"));

	EXPECT_EQ(path.hash(), Path(LR"(q:\Foo)").hash());
}

TEST_F(Path_Test, opCompare_IsSameWithUmlaut_CompareEqual) {
	const Path path(L"Q:\\foo\u00E4\u00DF");

	EXPECT_TRUE(path == Path(L"Q:\\foo\u00E4\u00DF"));
	EXPECT_FALSE(path != Path(L"Q:\\foo\u00E4\u00DF"));
	EXPECT_FALSE(path < Path(L"Q:\\foo\u00E4\u00DF"));
	EXPECT_TRUE(path <= Path(L"Q:\\foo\u00E4\u00DF"));
	EXPECT_FALSE(path > Path(L"Q:\\foo\u00E4\u00DF"));
	EXPECT_TRUE(path >= Path(L"Q:\\foo\u00E4\u00DF"));

	EXPECT_EQ(path.hash(), Path(L"Q:\\foo\u00E4\u00DF").hash());
}

TEST_F(Path_Test, opCompare_IsSameWithUmlautAndAccentAndDifferentCase_CompareEqual) {
	const Path path(L"Q:\\foo\u00E4\u00E9");

	EXPECT_TRUE(path == Path(L"Q:\\foo\u00C4\u00C9"));
	EXPECT_FALSE(path != Path(L"Q:\\foo\u00C4\u00C9"));
	EXPECT_FALSE(path < Path(L"Q:\\foo\u00C4\u00C9"));
	EXPECT_TRUE(path <= Path(L"Q:\\foo\u00C4\u00C9"));
	EXPECT_FALSE(path > Path(L"Q:\\foo\u00C4\u00C9"));
	EXPECT_TRUE(path >= Path(L"Q:\\foo\u00C4\u00C9"));

	EXPECT_EQ(path.hash(), Path(L"Q:\\foo\u00C4\u00C9").hash());
}

TEST_F(Path_Test, opCompare_IsLessThan_CompareLessThan) {
	const Path path(LR"(Q:\foo)");

	EXPECT_FALSE(path == Path(LR"(Q:\zar)"));
	EXPECT_TRUE(path != Path(LR"(Q:\zar)"));
	EXPECT_TRUE(path < Path(LR"(Q:\zar)"));
	EXPECT_TRUE(path <= Path(LR"(Q:\zar)"));
	EXPECT_FALSE(path > Path(LR"(Q:\zar)"));
	EXPECT_FALSE(path >= Path(LR"(Q:\zar)"));

	EXPECT_NE(path.hash(), Path(LR"(Q:\zar)").hash());
}

TEST_F(Path_Test, opCompare_IsGreaterThan_CompareGreaterThan) {
	const Path path(LR"(Q:\foo)");

	EXPECT_FALSE(path == Path(LR"(Q:\bar)"));
	EXPECT_TRUE(path != Path(LR"(Q:\bar)"));
	EXPECT_FALSE(path < Path(LR"(Q:\bar)"));
	EXPECT_FALSE(path <= Path(LR"(Q:\bar)"));
	EXPECT_TRUE(path > Path(LR"(Q:\bar)"));
	EXPECT_TRUE(path >= Path(LR"(Q:\bar)"));

	EXPECT_NE(path.hash(), Path(LR"(Q:\bar)").hash());
}

TEST_F(Path_Test, opCompare_IsDifferentForDiaeresisOnly_CompareLessThan) {
	const Path path(L"Q:\\foo");

	EXPECT_FALSE(path == Path(L"Q:\\fo\u00F6"));  // F6 == o umlaut
	EXPECT_TRUE(path != Path(L"Q:\\fo\u00F6"));
	EXPECT_TRUE(path < Path(L"Q:\\fo\u00F6"));
	EXPECT_TRUE(path <= Path(L"Q:\\fo\u00F6"));
	EXPECT_FALSE(path > Path(L"Q:\\fo\u00F6"));
	EXPECT_FALSE(path >= Path(L"Q:\\fo\u00F6"));

	EXPECT_NE(path.hash(), Path(L"Q:\\fo\u00F6").hash());
}

TEST_F(Path_Test, opCompare_IsDifferentForAccentOnly_CompareLessThan) {
	const Path path(L"Q:\\foo");

	EXPECT_FALSE(path == Path(L"Q:\\fo\u00F3"));  // F3 == o accent ´
	EXPECT_TRUE(path != Path(L"Q:\\fo\u00F3"));
	EXPECT_TRUE(path < Path(L"Q:\\fo\u00F3"));
	EXPECT_TRUE(path <= Path(L"Q:\\fo\u00F3"));
	EXPECT_FALSE(path > Path(L"Q:\\fo\u00F3"));
	EXPECT_FALSE(path >= Path(L"Q:\\fo\u00F3"));

	EXPECT_NE(path.hash(), Path(L"Q:\\fo\u00F3").hash());
}

TEST_F(Path_Test, opCompare_IsDifferentAccents_CompareNotEqual) {
	const Path path(L"Q:\\foo\u00E0");  // E0 == a accent `

	EXPECT_FALSE(path == Path(L"Q:\\fo\u00E1"));  // E1 == a accent ´
	EXPECT_TRUE(path != Path(L"Q:\\fo\u00E1"));
	EXPECT_TRUE(path < Path(L"Q:\\fo\u00E1"));
	EXPECT_TRUE(path <= Path(L"Q:\\fo\u00E1"));
	EXPECT_FALSE(path > Path(L"Q:\\fo\u00E1"));
	EXPECT_FALSE(path >= Path(L"Q:\\fo\u00E1"));

	EXPECT_NE(path.hash(), Path(L"Q:\\fo\u00E1").hash());
}

TEST_F(Path_Test, opCompare_ErrorComparing_ThrowException) {
	EXPECT_CALL(m_win32, CompareStringOrdinal(t::StrEq(L"Q:\\foo"), t::_, t::StrEq(L"Q:\\foo"), t::_, t::_))
		.Times(6)
		.WillRepeatedly(dtgm::SetLastErrorAndReturn(ERROR_NOT_SUPPORTED, 0));
	const Path path(LR"(Q:\foo)");

#pragma warning(suppress : 4834)
	EXPECT_THROW(path == Path(LR"(Q:\foo)"), m3c::windows_exception);
#pragma warning(suppress : 4552)
	EXPECT_THROW(path != Path(LR"(Q:\foo)"), m3c::windows_exception);
#pragma warning(suppress : 4834)
	EXPECT_THROW(path < Path(LR"(Q:\foo)"), m3c::windows_exception);
#pragma warning(suppress : 4834)
	EXPECT_THROW(path <= Path(LR"(Q:\foo)"), m3c::windows_exception);
#pragma warning(suppress : 4834)
	EXPECT_THROW(path > Path(LR"(Q:\foo)"), m3c::windows_exception);
#pragma warning(suppress : 4834)
	EXPECT_THROW(path >= Path(LR"(Q:\foo)"), m3c::windows_exception);
}

TEST_F(Path_Test, opAppend_Directory_AppendDirectory) {
	Path path(LR"(Q:\foo)");

	path /= L"bar";

	EXPECT_STREQ(LR"(Q:\foo\bar)", path.c_str());
}

TEST_F(Path_Test, opAppend_DirectoryWithSeparator_AppendDirectory) {
	Path path(LR"(Q:\foo)");

	path /= LR"(\bar)";

	EXPECT_STREQ(LR"(Q:\foo\bar)", path.c_str());
}

TEST_F(Path_Test, opAppend_Empty_DoNotChange) {
	Path path(LR"(Q:\foo)");

	path /= L"";

	EXPECT_STREQ(LR"(Q:\foo)", path.c_str());
}

TEST_F(Path_Test, opAppend_RootPath_ChangeRoot) {
	Path path(LR"(Q:\foo)");

	path /= LR"(R:\bar\)";

	EXPECT_STREQ(LR"(R:\bar)", path.c_str());
}

TEST_F(Path_Test, opAppend_LongPath_MakeLongPath) {
	Path path(LR"(Q:\foo)");
	std::wstring name(MAX_PATH, L'x');

	path /= name;

	EXPECT_EQ(LR"(\\?\Q:\foo\)" + name, path.c_str());
}

TEST_F(Path_Test, opAppend_ResultIsLong_MakeLongPath) {
	Path path(LR"(Q:\foo)" + std::wstring(MAX_PATH / 2, L'y'));
	std::wstring name(MAX_PATH / 2, L'x');
	ASSERT_THAT(path.c_str(), t::Not(t::StartsWith(LR"(\\?\)")));

	path /= name;

	EXPECT_EQ(LR"(\\?\Q:\foo)" + std::wstring(MAX_PATH / 2, L'y') + L'\\' + name, path.c_str());
}

TEST_F(Path_Test, opAppend_LongPathWithDotDot_MakeLongPathAndRemoveDotDot) {
	Path path(LR"(Q:\foo)");
	const std::wstring name(MAX_PATH, L'x');

	path /= (name + LR"(\bar\..\baz)");

	EXPECT_EQ(LR"(\\?\Q:\foo\)" + name + LR"(\baz)", path.c_str());
}

TEST_F(Path_Test, opAppend_DotDotToLong_RemoveDotDot) {
	const std::wstring name = LR"(Q:\)" + std::wstring(MAX_PATH, L'x') + LR"(\foo)";
	Path path(name);

	path /= LR"(bar\..\baz)";

	EXPECT_EQ(LR"(\\?\)" + name + LR"(\baz)", path.c_str());
}

TEST_F(Path_Test, opAppend_ErrorAppending_ThrowException) {
	EXPECT_CALL(m_win32, PathCchAppendEx(t::StrEq(L"Q:\\foo"), t::_, t::StrEq(L"bar"), t::_))
		.WillOnce(t::Return(HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)));
	Path path(LR"(Q:\foo)");

	EXPECT_THROW(path /= L"bar", m3c::com_exception);
	EXPECT_STREQ(LR"(Q:\foo)", path.c_str());
}

TEST_F(Path_Test, opAppend_ErrorRemovingBackslash_ThrowException) {
	EXPECT_CALL(m_win32, PathCchAppendEx(t::StrEq(L"Q:\\foo"), t::_, t::StrEq(L"bar"), t::_));
	EXPECT_CALL(m_win32, PathCchRemoveBackslashEx(t::StrEq(L"Q:\\foo\\bar"), t::_, t::_, t::_))
		.WillOnce(t::Return(HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)));
	Path path(LR"(Q:\foo)");

	EXPECT_THROW(path /= L"bar", m3c::com_exception);
	EXPECT_STREQ(LR"(Q:\foo)", path.c_str());
}

TEST_F(Path_Test, opConcat_Directory_ReturnWithDirectoryAppended) {
	const Path path(LR"(Q:\foo)");

	const Path result = path / L"bar";

	EXPECT_STREQ(LR"(Q:\foo\bar)", result.c_str());
}

TEST_F(Path_Test, opConcat_DirectoryWithSeparator_ReturnWithDirectoryAppended) {
	const Path path(LR"(Q:\foo)");

	const Path result = path / LR"(\bar)";

	EXPECT_STREQ(LR"(Q:\foo\bar)", result.c_str());
}

TEST_F(Path_Test, opConcat_Empty_ReturnUnchanged) {
	const Path path(LR"(Q:\foo)");

	const Path result = path / L"";

	EXPECT_STREQ(LR"(Q:\foo)", result.c_str());
}

TEST_F(Path_Test, opConcat_RootPath_ReturnNewRoot) {
	const Path path(LR"(Q:\foo)");

	const Path result = path / LR"(R:\bar\)";

	EXPECT_STREQ(LR"(R:\bar)", result.c_str());
}

TEST_F(Path_Test, opConcat_LongPath_ReturnLongPath) {
	const Path path(LR"(Q:\foo)");
	std::wstring name(MAX_PATH, L'x');

	const Path result = path / name;

	EXPECT_EQ(LR"(\\?\Q:\foo\)" + name, result.c_str());
}

TEST_F(Path_Test, opConcat_ResultIsLong_MakeLongPath) {
	const Path path(LR"(Q:\foo)" + std::wstring(MAX_PATH / 2, L'y'));
	std::wstring name(MAX_PATH / 2, L'x');
	ASSERT_THAT(path.c_str(), t::Not(t::StartsWith(LR"(\\?\)")));

	const Path result = path / name;

	EXPECT_EQ(LR"(\\?\Q:\foo)" + std::wstring(MAX_PATH / 2, L'y') + L'\\' + name, result.c_str());
}

TEST_F(Path_Test, opConcat_LongPathWithDotDot_ReturnLongPathAndRemoveDotDot) {
	const Path path(LR"(Q:\foo)");
	const std::wstring name(MAX_PATH, L'x');

	const Path result = path / (name + LR"(\bar\..\baz)");

	EXPECT_EQ(LR"(\\?\Q:\foo\)" + name + LR"(\baz)", result.c_str());
}

TEST_F(Path_Test, opConcat_DotDotToLong_ReturnWithDotDotRemoved) {
	const std::wstring name = LR"(Q:\)" + std::wstring(MAX_PATH, L'x') + LR"(\foo)";
	const Path path(name);

	const Path result = path / LR"(bar\..\baz)";

	EXPECT_EQ(LR"(\\?\)" + name + LR"(\baz)", result.c_str());
}

TEST_F(Path_Test, str_call_ReturnString) {
	const Path path(LR"(Q:\foo)");

	// static_cast gives error in Intellisense
	const std::wstring_view value = path.sv();
	EXPECT_EQ(LR"(Q:\foo)", value);
}

TEST_F(Path_Test, Exists_IsSystemDirectory_ReturnTrue) {
	const Path path = TestUtils::GetSystemDirectory();

	EXPECT_TRUE(path.Exists());
}

TEST_F(Path_Test, Exists_IsSystemRoot_ReturnTrue) {
	Path path = TestUtils::GetSystemDirectory();
	for (Path parent = path.GetParent(); path != parent; parent = path.GetParent()) {
		path = parent;
	}

	EXPECT_TRUE(path.Exists());
}

TEST_F(Path_Test, Exists_DoesNotExist_ReturnFalse) {
	const Path path(LR"(Q:\file_does_not_exist.321)");

	EXPECT_FALSE(path.Exists());
}

TEST_F(Path_Test, Exists_Error_ThrowException) {
	EXPECT_CALL(m_win32, GetFileAttributesW(t::StrEq(L"Q:\\foo.txt")))
		.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_NOT_SUPPORTED, INVALID_FILE_ATTRIBUTES));
	const Path path(LR"(Q:\foo.txt)");

#pragma warning(suppress : 4834)
	EXPECT_THROW(path.Exists(), m3c::windows_exception);
}

TEST_F(Path_Test, IsDirectory_IsSystemDirectory_ReturnTrue) {
	const Path path = TestUtils::GetSystemDirectory();

	EXPECT_TRUE(path.IsDirectory());
}

TEST_F(Path_Test, IsDirectory_IsFileInSystemDirectory_ReturnFalse) {
	Path path = TestUtils::GetSystemDirectory();
	path /= L"user32.dll";
	ASSERT_TRUE(path.Exists());

	EXPECT_FALSE(path.IsDirectory());
}

TEST_F(Path_Test, IsDirectory_DoesNotExist_ThrowException) {
	const Path path(LR"(Q:\file_does_not_exist.321)");

#pragma warning(suppress : 4834)
	EXPECT_THROW(path.IsDirectory(), m3c::windows_exception);
}

TEST_F(Path_Test, IsDirectory_Error_ThrowException) {
	EXPECT_CALL(m_win32, GetFileAttributesW(t::StrEq(L"Q:\\foo.txt")))
		.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_NOT_SUPPORTED, INVALID_FILE_ATTRIBUTES));
	const Path path(LR"(Q:\foo.txt)");

#pragma warning(suppress : 4834)
	EXPECT_THROW(path.IsDirectory(), m3c::windows_exception);
}


TEST_F(Path_Test, GetParent_IsSubDirectory_ReturnRoot) {
	const Path path(LR"(Q:\foo)");

	const Path parent = path.GetParent();

	EXPECT_STREQ(LR"(Q:\)", parent.c_str());
}

TEST_F(Path_Test, GetParent_IsVolumeSubDirectory_ReturnRoot) {
	const Path path(kTestVolume + LR"(\foo)");
	const Path parent = path.GetParent();

	EXPECT_EQ(kTestVolume + LR"(\)", parent.c_str());
}

TEST_F(Path_Test, GetParent_IsSubDirectoryWithSubDirectory_ReturnSubDirectory) {
	const Path path(LR"(Q:\foo\bar)");

	const Path parent = path.GetParent();

	EXPECT_STREQ(LR"(Q:\foo)", parent.c_str());
}

TEST_F(Path_Test, GetParent_IsSubDirectoryWithFile_ReturnSubDirectory) {
	const Path path(LR"(Q:\foo\bar.txt)");

	const Path parent = path.GetParent();

	EXPECT_STREQ(LR"(Q:\foo)", parent.c_str());
}

TEST_F(Path_Test, GetParent_IsDriveRoot_ReturnRoot) {
	const Path path(LR"(Q:\)");

	const Path parent = path.GetParent();

	EXPECT_STREQ(LR"(Q:\)", parent.c_str());
}

TEST_F(Path_Test, GetParent_IsVolumeRoot_ReturnRoot) {
	const Path path(kTestVolume + LR"(\)");

	const Path parent = path.GetParent();

	EXPECT_EQ(kTestVolume + LR"(\)", parent.c_str());
}

TEST_F(Path_Test, GetParent_IsLong_GetWithPrefix) {
	const Path path(LR"(Q:\foo\)" + std::wstring(MAX_PATH, L'x'));
	ASSERT_THAT(path.c_str(), t::StartsWith(LR"(\\?\)"));

	const Path parent = path.GetParent();

	EXPECT_STREQ(LR"(\\?\Q:\foo)", parent.c_str());
}

TEST_F(Path_Test, GetParent_ErrorGettingParent_ThrowException) {
	EXPECT_CALL(m_win32, PathCchRemoveFileSpec(t::StrEq(L"Q:\\foo\\bar.txt"), t::_))
		.WillOnce(t::Return(HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)));
	const Path path(LR"(Q:\foo\bar.txt)");

#pragma warning(suppress : 4834)
	EXPECT_THROW(path.GetParent(), m3c::com_exception);
}

TEST_F(Path_Test, GetFilename_IsFileInRoot_ReturnFilename) {
	const Path path(LR"(Q:\foo.txt)");

	const Filename filename = path.GetFilename();

	EXPECT_STREQ(L"foo.txt", filename.c_str());
}

TEST_F(Path_Test, GetFilename_IsFileInVolumeRoot_ReturnFilename) {
	const Path path(kTestVolume + LR"(\foo.txt)");

	const Filename filename = path.GetFilename();

	EXPECT_STREQ(L"foo.txt", filename.c_str());
}

TEST_F(Path_Test, GetFilename_IsFileInSubDirectory_ReturnFilename) {
	const Path path(LR"(Q:\bar\foo.txt)");

	const Filename filename = path.GetFilename();

	EXPECT_STREQ(L"foo.txt", filename.c_str());
}

TEST_F(Path_Test, GetFilename_IsFileInVolumeSubDirectory_ReturnFilename) {
	const Path path(kTestVolume + LR"(\bar\foo.txt)");

	const Filename filename = path.GetFilename();

	EXPECT_STREQ(L"foo.txt", filename.c_str());
}

TEST_F(Path_Test, GetFilename_IsDirectoryInRoot_ReturnFilename) {
	const Path path(LR"(Q:\foo)");

	const Filename filename = path.GetFilename();

	EXPECT_STREQ(L"foo", filename.c_str());
}

TEST_F(Path_Test, GetFilename_IsDirectoryInVolumeRoot_ReturnFilename) {
	const Path path(kTestVolume + LR"(\foo)");

	const Filename filename = path.GetFilename();

	EXPECT_STREQ(L"foo", filename.c_str());
}

TEST_F(Path_Test, GetFilename_IsDirectoryInSubDirectory_ReturnFilename) {
	const Path path(LR"(Q:\bar\foo)");

	const Filename filename = path.GetFilename();

	EXPECT_STREQ(L"foo", filename.c_str());
}

TEST_F(Path_Test, GetFilename_IsDriveRoot_ReturnEmpty) {
	const Path path(LR"(Q:\)");

	const Filename filename = path.GetFilename();

	EXPECT_STREQ(L"", filename.c_str());
}

TEST_F(Path_Test, GetFilename_IsVolumeRoot_ReturnEmpty) {
	const Path path(kTestVolume + LR"(\)");

	const Filename filename = path.GetFilename();

	EXPECT_STREQ(L"", filename.c_str());
}

TEST_F(Path_Test, GetFilename_IsLong_ReturnEmpty) {
	const std::wstring name(1024, 'x');
	const Path path(LR"(Q:\foo\)" + name + LR"(\bar.txt)");

	const Filename filename = path.GetFilename();

	EXPECT_STREQ(L"bar.txt", filename.c_str());
}

TEST_F(Path_Test, GetFilename_ErrorGettingPath_ThrowError) {
	EXPECT_CALL(m_win32, GetFullPathNameW(t::StrEq(LR"(Q:\bar\foo.txt)"), t::Gt(0u), t::_, t::NotNull()))
		.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_NOT_SUPPORTED, 0));
	const Path path(LR"(Q:\bar\foo.txt)");

#pragma warning(suppress : 4834)
	EXPECT_THROW(path.GetFilename(), m3c::windows_exception);
}

TEST_F(Path_Test, GetFilename_ErrorGettingLongPath_ThrowError) {
	const std::wstring name(1024, 'x');
	EXPECT_CALL(m_win32, GetFullPathNameW(t::StrEq(LR"(\\?\Q:\bar\)" + name + L".txt"), t::Gt(0u), t::_, t::NotNull()))
		.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_NOT_SUPPORTED, 0));
	const Path path(LR"(Q:\bar\)" + name + L".txt");

#pragma warning(suppress : 4834)
	EXPECT_THROW(path.GetFilename(), m3c::windows_exception);
}

TEST_F(Path_Test, GetFilename_ErrorGettingLongPathData_ThrowError) {
	const std::wstring name(1024, 'x');
	EXPECT_CALL(m_win32, GetFullPathNameW(t::StrEq(LR"(\\?\Q:\bar\)" + name + L".txt"), t::Gt(0u), t::_, t::NotNull()))
		.WillOnce(dtgm::SetLastErrorAndReturn(0, 2048))
		.WillOnce(dtgm::SetLastErrorAndReturn(ERROR_NOT_SUPPORTED, 0));
	const Path path(LR"(Q:\bar\)" + name + L".txt");

#pragma warning(suppress : 4834)
	EXPECT_THROW(path.GetFilename(), m3c::windows_exception);
}

TEST_F(Path_Test, swap_ValueWithValue_ValueAndValue) {
	Path path(kTestVolume + LR"(\foo)");
	Path oth(LR"(Q:\bar)");

	path.swap(oth);

	EXPECT_STREQ(LR"(Q:\bar)", path.c_str());
	EXPECT_EQ(kTestVolume + LR"(\foo)", oth.c_str());
}

TEST_F(Path_Test, stdSwap_ValueWithValue_ValueAndValue) {
	Path path(kTestVolume + LR"(\foo)");
	Path oth(LR"(Q:\bar)");

	std::swap(path, oth);

	EXPECT_STREQ(LR"(Q:\bar)", path.c_str());
	EXPECT_EQ(kTestVolume + LR"(\foo)", oth.c_str());
}


TEST_F(Path_Test, stdHash_Value_ReturnHash) {
	const std::wstring str(LR"(Q:\bar\foo.txt)");
	const Path path(str);
	const size_t h = std::hash<Path>{}(path);

	EXPECT_EQ(Path(str).hash(), h);
}

TEST_F(Path_Test, stdHash_DiffersInCase_HashEquals) {
	const std::wstring str(LR"(Q:\bar\foo.txt)");
	const std::wstring oth(LR"(Q:\bar\Foo.txt)");
	const Path path(str);
	const Path othPath(oth);
	const size_t h = std::hash<Path>{}(path);
	const size_t o = std::hash<Path>{}(othPath);

	ASSERT_TRUE(path == othPath);
	EXPECT_EQ(o, h);
}

TEST_F(Path_Test, stdHash_DiffersInCaseWithUmlaut_HashEquals) {
	const std::wstring str(L"Q:\\bar\\fo\u00F6.txt");
	const std::wstring oth(L"Q:\\bar\\fo\u00D6.txt");
	const Path path(str);
	const Path othPath(oth);
	const size_t h = std::hash<Path>{}(path);
	const size_t o = std::hash<Path>{}(othPath);

	ASSERT_TRUE(path == othPath);
	EXPECT_EQ(o, h);
}

TEST_F(Path_Test, stdHash_LongValue_ReturnHash) {
	const std::wstring str(LR"(Q:\bar\)" + std::wstring(MAX_PATH, 'x') + LR"(\foo.txt)");
	const Path path(str);

	const size_t h = std::hash<Path>{}(path);

	EXPECT_EQ(Path(str).hash(), h);
}

TEST_F(Path_Test, ForceDelete_NotExists_ThrowsException) {
	const Path path(kTestVolume + LR"(\foo)");
	ASSERT_FALSE(path.Exists());

	EXPECT_THROW(path.ForceDelete(), m3c::windows_exception);
}

TEST_P(Path_DeleteTest, ForceDelete_Exists_Delete) {
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

TEST_P(Path_DeleteTest, ForceDelete_ErrorGettingAttributes_ThrowException) {
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

TEST_P(Path_DeleteTest, ForceDelete_ErrorRemovingReadOnlyAttribute_ThrowException) {
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

TEST_P(Path_DeleteTest, ForceDelete_ErrorDeleting_ThrowException) {
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

TEST_P(Path_DeleteTest, ForceDelete_ErrorRestoringReadOnlyAttributeAfterDeleteError_ThrowException) {
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

TEST_P(Path_DeleteTest, ForceDelete_ErrorFindingFirstNameBufferSize_ThrowException) {
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

TEST_P(Path_DeleteTest, ForceDelete_ErrorFindingFirstName_ThrowException) {
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

TEST_P(Path_DeleteTest, ForceDelete_ErrorFindingSecondNameBufferSize_ThrowException) {
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

TEST_P(Path_DeleteTest, ForceDelete_ErrorFindingSecondName_ThrowException) {
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

TEST_P(Path_DeleteTest, ForceDelete_ErrorGettingHardlinkRoot_ThrowException) {
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

TEST_P(Path_DeleteTest, ForceDelete_ErrorBuildingHardlinkPathForSecondName_ThrowException) {
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

TEST_P(Path_DeleteTest, ForceDelete_ErrorRestoringHardlinkReadOnlyAttributeForSecondName_Return) {
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

TEST_P(Path_DeleteTest, ForceDelete_ErrorBuildingHardlinkPathForFirstName_ThrowException) {
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

TEST_P(Path_DeleteTest, ForceDelete_ErrorRestoringHardlinkReadOnlyAttributeForFirstName_Return) {
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

INSTANTIATE_TEST_SUITE_P(Path_DeleteTest, Path_DeleteTest, t::Combine(t::Values(Mode::kDirectory, Mode::kFile, Mode::kHardlink), t::Values(Attributes::kNormal, Attributes::kReadOnly, Attributes::kHidden, Attributes::kSystem, Attributes::kHidden | Attributes::kSystem, Attributes::kReadOnly | Attributes::kHidden | Attributes::kSystem)), [](const t::TestParamInfo<Path_DeleteTest::ParamType> &param) {
	std::string attributes = t::PrintToString(std::get<1>(param.param));
	attributes.erase(std::remove(attributes.begin(), attributes.end(), '+'));
	return fmt::format("{:03}_{}_{}", param.index, t::PrintToString(std::get<0>(param.param)), attributes);
});

}  // namespace systools::test
