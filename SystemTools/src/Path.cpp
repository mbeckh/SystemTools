#include "systools/Path.h"

#include <llamalog/llamalog.h>
#include <m3c/Handle.h>
#include <m3c/exception.h>
#include <m3c/string_encode.h>

#include <pathcch.h>
#include <windows.h>

#include <algorithm>
#include <string>

namespace systools {

namespace {

constexpr wchar_t kVolumePrefix[] = LR"(\\?\Volume{)";
constexpr wchar_t kUuid[] = L"00000000-0000-0000-0000-0000000000";
constexpr std::size_t kVolumeRootLen = sizeof(kVolumePrefix) / sizeof(kVolumePrefix[0]) + sizeof(kUuid) / sizeof(kUuid[0]) /* + }\ (already part of result because of 2 x \0 */;

int CompareFilenames(const std::wstring& file0, const std::wstring& file1) {
	if (const int cmp = CompareStringOrdinal(file0.c_str(), static_cast<int>(file0.size()), file1.c_str(), static_cast<int>(file1.size()), TRUE); cmp) {
		return cmp - 2;
	}
	THROW(m3c::windows_exception(GetLastError()), "CompareStringOrdinal {} / {}", file0, file1);
}

bool IsVolumeRoot(const std::wstring& str) noexcept {
	return ((str.size() > kVolumeRootLen && str[kVolumeRootLen] == L'\0') || str.size() == kVolumeRootLen)
		   && str[kVolumeRootLen - 2] == L'}' && str[kVolumeRootLen - 1] == L'\\'
		   && str.starts_with(kVolumePrefix)
		   && std::all_of(
			   str.cbegin() + sizeof(kVolumePrefix) / sizeof(kVolumePrefix[0]) - 1 /* \0 */,
			   str.cbegin() + sizeof(kVolumePrefix) / sizeof(kVolumePrefix[0]) - 1 + sizeof(kUuid) / sizeof(kUuid[0]) - 1 /* \0 */,
			   [](const wchar_t ch) noexcept {
				   return ch >= L'0' && ch <= L'9' || ch >= L'a' && ch <= L'f' || ch >= L'A' && ch <= L'F' || ch == L'-';
			   });
}

/// @brief Trim string to actual size removing any trailing \ and \0 characters.
void TrimPath(std::wstring& str) {
	assert(str.find(L'\0') != std::wstring::npos);  // not yet truncated

	// special handling to keep backslash in \\?\Volume{00000000-0000-0000-0000-0000000000}\\ .
	if (IsVolumeRoot(str)) {
		str.resize(kVolumeRootLen);
	} else {
		wchar_t* pEnd;
		std::size_t remaining;
		COM_HR(PathCchRemoveBackslashEx(str.data(), str.size(), &pEnd, &remaining), "PathCchRemoveBackslash {}", str);
		str.resize(pEnd - str.data() + (*pEnd ? 1 : 0));
	}

	assert(str.find(L'\0') == std::wstring::npos);  // properly truncated
}

}  // namespace

bool Filename::operator==(const Filename& filename) const {
	return CompareFilenames(m_filename, filename.m_filename) == 0;
}

bool Filename::operator!=(const Filename& filename) const {
	return CompareFilenames(m_filename, filename.m_filename) != 0;
}

const int Filename::CompareTo(const Filename& filename) const {
	return CompareFilenames(m_filename, filename.m_filename);
}


bool Filename::IsSameStringAs(const Filename& filename) const noexcept {
	return m_filename == filename.m_filename;
}

Path::Path(const std::wstring& path) {
	const DWORD size = GetFullPathNameW(path.c_str(), 0, m_path.data(), nullptr);
	if (!size) {
		THROW(m3c::windows_exception(GetLastError()), "GetFullPathName {}", path);
	}

	constexpr std::size_t kPrefixLen = (sizeof(LR"(\\?\)") - 1) / sizeof(wchar_t);
	const std::size_t bufferSize = size + kPrefixLen + 1 /* for PATHCCH_ENSURE_TRAILING_SLASH */;
	m_path.resize(bufferSize);  // room for \\?\ prefix
	const DWORD len = GetFullPathNameW(path.c_str(), size, m_path.data(), nullptr);
	if (!len || len >= size) {
		THROW(m3c::windows_exception(GetLastError()), "GetFullPathName {}", path);
	}

	// add \\?\ if required
	COM_HR(PathCchAppendEx(m_path.data(), bufferSize, L".", PATHCCH_ENSURE_TRAILING_SLASH | PATHCCH_ALLOW_LONG_PATHS), "PathCchAppendEx {}", m_path);

	TrimPath(m_path);  // implicitly resizes
}

bool Path::operator==(const Path& path) const {
	return CompareFilenames(m_path, path.m_path) == 0;
}

bool Path::operator!=(const Path& path) const {
	return CompareFilenames(m_path, path.m_path) != 0;
}

Path& Path::operator/=(const std::wstring& sub) {
	// using / for strong exception guarantee
	return *this = std::move(*this / sub);
}

Path& Path::operator/=(const Filename& sub) {
	// static_cast gives error in Intellisense
	return operator/=(sub.operator const std::wstring&());
}

Path Path::operator/(const std::wstring& sub) const {
	Path result(*this);
	const std::size_t len = result.m_path.size() + sub.size();
	result.m_path.resize(len + 6);  // \\?\ plus \ plus terminating \0
	COM_HR(PathCchAppendEx(result.m_path.data(), result.m_path.size(), sub.c_str(), PATHCCH_ALLOW_LONG_PATHS), "PathCchAppendEx {} {}", result.m_path, sub);
	TrimPath(result.m_path);  // implicitly resizes
	return result;
}

Path Path::operator/(const Filename& sub) const {
	// static_cast gives error in Intellisense
	return operator/(sub.operator const std::wstring&());
}


bool Path::Exists() const {
	if (GetFileAttributesW(m_path.c_str()) != INVALID_FILE_ATTRIBUTES) {
		return true;
	}
	if (const DWORD lastError = GetLastError(); lastError != ERROR_FILE_NOT_FOUND && lastError != ERROR_PATH_NOT_FOUND) {
		THROW(m3c::windows_exception(lastError), "GetFileAttributes {}", m_path);
	}
	return false;
}

bool Path::IsDirectory() const {
	const DWORD attributes = GetFileAttributesW(m_path.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES) {
		THROW(m3c::windows_exception(GetLastError()), "GetFileAttributes {}", m_path);
	}
	return attributes & FILE_ATTRIBUTE_DIRECTORY;
}

Path Path::GetParent() const {
	Path result(*this);

	if (PathCchIsRoot(m_path.c_str()) || IsVolumeRoot(m_path)) {
		// prevent error because PathCchRemoveFileSpec expects \0 as part of the size
		return result;
	}
	const HRESULT hr = PathCchRemoveFileSpec(result.m_path.data(), result.m_path.size());
	COM_HR(hr, "PathCchRemoveFileSpec {}", m_path);
	if (hr != S_FALSE) {
		TrimPath(result.m_path);
	}
	return result;
}

Filename Path::GetFilename() const {
	wchar_t* pFilename;
	std::wstring path;
	const DWORD size = GetFullPathNameW(m_path.c_str(), 0, path.data(), nullptr);
	if (!size) {
		THROW(m3c::windows_exception(GetLastError()), "GetFullPathName {}", m_path);
	}
	path.resize(size);
	const DWORD len = GetFullPathNameW(m_path.c_str(), size, path.data(), &pFilename);
	if (!len || len >= size) {
		THROW(m3c::windows_exception(GetLastError()), "GetFullPathName {}", m_path);
	}

	return pFilename ? Filename(pFilename, path.data() + len - pFilename) : Filename(L"");
}

void Path::ForceDelete() const {
	const DWORD attributes = GetFileAttributesW(m_path.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES) {
		THROW(m3c::windows_exception(GetLastError()), "GetFileAttributes {}", m_path);
	}

	// delete directory
	if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
		if ((attributes & FILE_ATTRIBUTE_READONLY) && !SetFileAttributesW(m_path.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY)) {
			THROW(m3c::windows_exception(GetLastError()), "SetFileAttributes {}", m_path);
		}
		if (!RemoveDirectoryW(m_path.c_str())) {
			// try to restore read-only attribute
			if (!SetFileAttributesW(m_path.c_str(), attributes)) {
				LOG_ERROR("SetFileAttributes {}: {}", m_path, lg::LastError());
			}
			THROW(m3c::windows_exception(GetLastError()), "RemoveDirectory {}", m_path);
		}
		return;
	}

	// store information to reset read-only flag (2 paths is enough because only one is deleted)
	std::wstring alternativePath[2];
	if (attributes & FILE_ATTRIBUTE_READONLY) {
		DWORD size = 0;
		m3c::FindHandle hFind = FindFirstFileNameW(m_path.c_str(), 0, &size, alternativePath[0].data());
		if (!hFind) {
			if (const DWORD lastError = GetLastError(); lastError != ERROR_MORE_DATA) {
				THROW(m3c::windows_exception(lastError), "FindFirstFileName {}", m_path);
			}
			alternativePath[0].resize(size);

			hFind = FindFirstFileNameW(m_path.c_str(), 0, &size, alternativePath[0].data());
			if (!hFind) {
				THROW(m3c::windows_exception(GetLastError()), "FindFirstFileName {}", m_path);
			}
			assert(alternativePath[0][size - 1] == L'\0');
			alternativePath[0].resize(size - 1);
		}

		size = 0;
		if (!FindNextFileNameW(hFind, &size, alternativePath[1].data())) {
			if (const DWORD lastError = GetLastError(); lastError == ERROR_MORE_DATA) {
				alternativePath[1].resize(size);

				if (!FindNextFileNameW(hFind, &size, alternativePath[1].data())) {
					THROW(m3c::windows_exception(lastError), "FindFirstFileName {}", m_path);
				}
				assert(alternativePath[1][size - 1] == L'\0');
				alternativePath[1].resize(size - 1);
			} else if (lastError != ERROR_HANDLE_EOF) {
				THROW(m3c::windows_exception(lastError), "FindNextFileName {}", m_path);
			}
		}

		assert(!alternativePath[0].empty());
		if (!SetFileAttributesW(m_path.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY)) {
			THROW(m3c::windows_exception(GetLastError()), "SetFileAttributes {}", m_path);
		}
	}

	// delete file
	if (!DeleteFileW(m_path.c_str())) {
		// try to restore read-only attribute
		if (!SetFileAttributesW(m_path.c_str(), attributes)) {
			LOG_ERROR("SetFileAttributes {}: {}", m_path, lg::LastError());
		}
		THROW(m3c::windows_exception(GetLastError()), "DeleteFile {}", m_path);
	}

	// reset read-only flag
	if (!alternativePath[0].empty() && !alternativePath[1].empty()) {
		const wchar_t* pRootEnd;
		COM_HR(PathCchSkipRoot(m_path.c_str(), &pRootEnd), "PathCchSkipRoot {}", m_path);
		const std::size_t rootLen = pRootEnd - m_path.c_str() + 5 /* \\?\ + \0 */;

		std::wstring path(rootLen + alternativePath[1].size(), L'\0');
		COM_HR(PathCchCombineEx(path.data(), path.size(), m_path.c_str(), alternativePath[1].c_str(), PATHCCH_ALLOW_LONG_PATHS), "PathCchCombineEx {} {}", m_path, alternativePath[1]);

		if (!SetFileAttributesW(path.c_str(), attributes)) {
			LOG_DEBUG("SetFileAttributes {}: {}", path.c_str(), lg::LastError());  // path might have trailing \0

			path.resize(rootLen + alternativePath[0].size());
			COM_HR(PathCchCombineEx(path.data(), path.size(), m_path.c_str(), alternativePath[0].c_str(), PATHCCH_ALLOW_LONG_PATHS), "PathCchCombineEx {} {}", m_path, alternativePath[0]);
			if (!SetFileAttributesW(path.c_str(), attributes)) {
				THROW(m3c::windows_exception(GetLastError()), "SetFileAttributes {}", path.c_str());  // path might have trailing \0
			}
		}
	}
}

}  // namespace systools

llamalog::LogLine& operator<<(llamalog::LogLine& logLine, const systools::Filename& filename) {
	// static_cast gives error in Intellisense
	return logLine << (filename.operator const std::wstring&());
}

llamalog::LogLine& operator<<(llamalog::LogLine& logLine, const systools::Path& path) {
	// static_cast gives error in Intellisense
	return logLine << (path.operator const std::wstring&());
}

fmt::format_parse_context::iterator fmt::formatter<systools::Filename>::parse(const fmt::format_parse_context& ctx) {  // NOLINT(readability-identifier-naming): MUST use name as in fmt::formatter.
	auto it = ctx.begin();
	if (it != ctx.end() && *it == ':') {
		++it;
	}
	auto end = it;
	while (end != ctx.end() && *end != '}') {
		++end;
	}
	return end;
}

fmt::format_context::iterator fmt::formatter<systools::Filename>::format(const systools::Filename& arg, fmt::format_context& ctx) {  // NOLINT(readability-identifier-naming): MUST use name as in fmt::formatter.
	const std::string value = m3c::EncodeUtf8(arg.c_str(), arg.size());
	return std::copy(value.begin(), value.end(), ctx.out());
}

fmt::format_parse_context::iterator fmt::formatter<systools::Path>::parse(const fmt::format_parse_context& ctx) {  // NOLINT(readability-identifier-naming): MUST use name as in fmt::formatter.
	auto it = ctx.begin();
	if (it != ctx.end() && *it == ':') {
		++it;
	}
	auto end = it;
	while (end != ctx.end() && *end != '}') {
		++end;
	}
	return end;
}

fmt::format_context::iterator fmt::formatter<systools::Path>::format(const systools::Path& arg, fmt::format_context& ctx) {  // NOLINT(readability-identifier-naming): MUST use name as in fmt::formatter.
	const std::string value = m3c::EncodeUtf8(arg.c_str(), arg.size());
	return std::copy(value.begin(), value.end(), ctx.out());
}
