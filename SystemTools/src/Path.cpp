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

/// @file

#include "systools/Path.h"

#include <llamalog/llamalog.h>
#include <m3c/Handle.h>
#include <m3c/exception.h>
#include <m3c/lazy_string.h>
#include <m3c/string_encode.h>

#include <fmt/core.h>

#include <pathcch.h>
#include <strsafe.h>
#include <windows.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cwchar>
#include <exception>
#include <memory>
#include <string>

namespace systools {

namespace {

/// @brief The length of the prefix for long paths (not including the trailing 0 character).
constexpr std::size_t kPrefixLen = (sizeof(LR"(\\?\)") - 1) / sizeof(wchar_t);

std::weak_ordering CompareFilenames(const wchar_t* file0, std::size_t size0, const wchar_t* file1, std::size_t size1) {
	const int cmp = CompareStringOrdinal(file0, static_cast<int>(size0), file1, static_cast<int>(size1), TRUE);
	if (cmp == CSTR_LESS_THAN) {
		return std::weak_ordering::less;
	}
	if (cmp == CSTR_EQUAL) {
		return std::weak_ordering::equivalent;
	}
	if (cmp == CSTR_GREATER_THAN) {
		return std::weak_ordering::greater;
	}
	THROW(m3c::windows_exception(GetLastError()), "CompareStringOrdinal {} / {}", file0, file1);
}

template <std::uint16_t kSize0, std::uint16_t kSize1>
std::weak_ordering CompareFilenames(const m3c::lazy_wstring<kSize0>& file0, const m3c::lazy_wstring<kSize1>& file1) {
	const int cmp = CompareStringOrdinal(file0.c_str(), static_cast<int>(file0.size()), file1.c_str(), static_cast<int>(file1.size()), TRUE);
	if (cmp == CSTR_LESS_THAN) {
		return std::weak_ordering::less;
	}
	if (cmp == CSTR_EQUAL) {
		return std::weak_ordering::equivalent;
	}
	if (cmp == CSTR_GREATER_THAN) {
		return std::weak_ordering::greater;
	}
	THROW(m3c::windows_exception(GetLastError()), "CompareStringOrdinal {} / {}", file0, file1);
}

[[nodiscard]] std::size_t GetCaseInsensitiveHash(const wchar_t* str, std::size_t length) noexcept {
	if (!length) {
		return 0;
	}

	const int size = static_cast<int>(length);
	if (size <= MAX_PATH) {
		wchar_t buffer[MAX_PATH];
		if (const int len = LCMapStringW(LOCALE_SYSTEM_DEFAULT, LCMAP_LOWERCASE, str, size, buffer, MAX_PATH); len) {
			int h;  // NOLINT(cppcoreguidelines-init-variables): Initialized as out parameter.
			if (LCMapStringW(LOCALE_SYSTEM_DEFAULT, LCMAP_HASH, buffer, len, reinterpret_cast<wchar_t*>(&h), static_cast<int>(sizeof(h)))) {
				return h;
			}
		}
	} else {
		const std::unique_ptr<wchar_t[]> buffer = std::make_unique<wchar_t[]>(size);
		if (const int len = LCMapStringW(LOCALE_SYSTEM_DEFAULT, LCMAP_LOWERCASE, str, size, buffer.get(), size); len) {
			int h;  // NOLINT(cppcoreguidelines-init-variables): Initialized as out parameter.
			if (LCMapStringW(LOCALE_SYSTEM_DEFAULT, LCMAP_HASH, buffer.get(), len, reinterpret_cast<wchar_t*>(&h), static_cast<int>(sizeof(h)))) {
				return h;
			}
		}
	}

	// die if function fails because error is really severe and could corrupt all data
	SLOG_FATAL("LCMapStringW {}: {}", str, lg::LastError());
	assert(false);
	std::terminate();
}

template <std::uint16_t kSize>
[[nodiscard]] std::size_t GetCaseInsensitiveHash(const m3c::basic_lazy_string<kSize, wchar_t>& str) noexcept {
	if (str.empty()) {
		return 0;
	}

	const int size = static_cast<int>(str.size());
	if (size <= MAX_PATH) {
		wchar_t buffer[MAX_PATH];
		if (const int len = LCMapStringW(LOCALE_SYSTEM_DEFAULT, LCMAP_LOWERCASE, str.c_str(), size, buffer, MAX_PATH); len) {
			int h;  // NOLINT(cppcoreguidelines-init-variables): Initialized as out parameter.
			if (LCMapStringW(LOCALE_SYSTEM_DEFAULT, LCMAP_HASH, buffer, len, reinterpret_cast<wchar_t*>(&h), static_cast<int>(sizeof(h)))) {
				return h;
			}
		}
	} else {
		const std::unique_ptr<wchar_t[]> buffer = std::make_unique<wchar_t[]>(size);
		if (const int len = LCMapStringW(LOCALE_SYSTEM_DEFAULT, LCMAP_LOWERCASE, str.c_str(), size, buffer.get(), size); len) {
			int h;  // NOLINT(cppcoreguidelines-init-variables): Initialized as out parameter.
			if (LCMapStringW(LOCALE_SYSTEM_DEFAULT, LCMAP_HASH, buffer.get(), len, reinterpret_cast<wchar_t*>(&h), static_cast<int>(sizeof(h)))) {
				return h;
			}
		}
	}

	// die if function fails because error is really severe and could corrupt all data
	SLOG_FATAL("LCMapStringW {}: {}", str, lg::LastError());
	assert(false);
	std::terminate();
}

}  // namespace

Filename::Filename(const wchar_t* filename)
	: Filename(filename, std::wcslen(filename)) {
	// empty
}

std::weak_ordering Filename::operator<=>(const Filename& filename) const {
	return CompareFilenames(m_filename, filename.m_filename);
}

bool Filename::IsSameStringAs(const Filename& filename) const noexcept {
	return sv() == filename.sv();
}

void Filename::swap(Filename& filename) noexcept {  // NOLINT(readability-identifier-naming): For std::swap.
	std::swap(m_filename, filename.m_filename);
}

std::size_t Filename::hash() const noexcept {  // NOLINT(readability-identifier-naming): For std::hash.
	return GetCaseInsensitiveHash(m_filename);
}


Path::Path(_In_z_ const wchar_t* path)
	: Path(path, std::wcslen(path)) {
}

Path::Path(const std::wstring& path)
	: Path(path.c_str(), path.size()) {
	// empty
}

Path::Path(const std::wstring_view& path)
	// std::basic_string_view is not required to be null-terminated
	: Path(m3c::lazy_wstring<128>(path).c_str(), path.size()) {
	// empty
}

Path::Path(_In_z_ const wchar_t* path, const std::size_t length) {
	DWORD len;  // NOLINT(cppcoreguidelines-init-variables): Initialized in following block.
	if (length < MAX_PATH) {
		wchar_t buffer[MAX_PATH];
		len = GetFullPathNameW(path, MAX_PATH, buffer, nullptr);
		if (!len) {
			THROW(m3c::windows_exception(GetLastError()), "GetFullPathName {}", path);
		}

		if (len < MAX_PATH) {
			wchar_t pathBuffer[MAX_PATH];
			COM_HR(PathCchCanonicalizeEx(pathBuffer, MAX_PATH, buffer, PATHCCH_ALLOW_LONG_PATHS), "PathCchCanonicalizeEx {}", buffer);
			COM_HR(PathCchRemoveBackslash(pathBuffer, MAX_PATH), "PathCchRemoveBackslashEx {}", pathBuffer);
			m_path = pathBuffer;
			return;
		}
	} else {
		len = static_cast<DWORD>(length);
	}

	std::wstring fullPath(len, L'\0');

	// according to spec, it is allowed to set the terminating 0 character in std::basic_string to 0
	len = GetFullPathNameW(path, len + 1, fullPath.data(), nullptr);
	if (!len) {
		THROW(m3c::windows_exception(GetLastError()), "GetFullPathName {}", path);
	}

	if (len > fullPath.size()) {
		fullPath.resize(len - 1);

		len = GetFullPathNameW(path, len, fullPath.data(), nullptr);
		if (!len || len > fullPath.size()) {
			THROW(m3c::windows_exception(GetLastError()), "GetFullPathName {}", path);
		}
	}

	m_path.resize(len + kPrefixLen);
	COM_HR(PathCchCanonicalizeEx(m_path.data(), m_path.size() + 1, fullPath.c_str(), PATHCCH_ALLOW_LONG_PATHS), "PathCchCanonicalizeEx {}", fullPath);

	wchar_t* pEnd;          // NOLINT(cppcoreguidelines-init-variables): Initialized as out parameter.
	std::size_t remaining;  // NOLINT(cppcoreguidelines-init-variables): Initialized as out parameter.
	COM_HR(PathCchRemoveBackslashEx(m_path.data(), m_path.size() + 1, &pEnd, &remaining), "PathCchRemoveBackslashEx {}", m_path.c_str());
	m_path.resize(pEnd - m_path.data() + (*pEnd ? 1 : 0));
}

Path::Path(const Path& path, const wchar_t* sub, std::size_t subSize) {
	if (path.size() + subSize + kPrefixLen + 1 < MAX_PATH) {
		// try fast algorithm
		wchar_t buffer[MAX_PATH];
		COM_HR(StringCchCopyNW(buffer, MAX_PATH, path.c_str(), path.size()), "StringCchCopyNW {}", path);
		COM_HR(PathCchAppendEx(buffer, MAX_PATH, sub, PATHCCH_ALLOW_LONG_PATHS), "PathCchAppendEx {} {}", path, sub);
		COM_HR(PathCchRemoveBackslash(buffer, MAX_PATH), "PathCchRemoveBackslash {}", buffer);
		m_path = buffer;
		return;
	}

	m_path = path.m_path;
	m_path.resize(path.size() + subSize + kPrefixLen + 1);  // \\?\ plus \ character

	// according to spec, it is allowed to set the terminating 0 character in std::basic_string to 0
	COM_HR(PathCchAppendEx(m_path.data(), m_path.size() + 1, sub, PATHCCH_ALLOW_LONG_PATHS), "PathCchAppendEx {} {}", path.m_path, sub);

	wchar_t* pEnd;          // NOLINT(cppcoreguidelines-init-variables): Initialized as out parameter.
	std::size_t remaining;  // NOLINT(cppcoreguidelines-init-variables): Initialized as out parameter.
	COM_HR(PathCchRemoveBackslashEx(m_path.data(), m_path.size() + 1, &pEnd, &remaining), "PathCchRemoveBackslashEx {}", m_path.c_str());
	m_path.resize(pEnd - m_path.data() + (*pEnd ? 1 : 0));
}

std::weak_ordering Path::operator<=>(const Path& path) const {
	return CompareFilenames(m_path.c_str(), m_path.size(), path.c_str(), path.size());
}

std::weak_ordering Path::operator<=>(const std::wstring& path) const {
	return CompareFilenames(m_path.c_str(), m_path.size(), path.c_str(), path.size());
}

std::weak_ordering Path::operator<=>(const std::wstring_view& path) const {
	return CompareFilenames(m_path.c_str(), m_path.size(), path.data(), path.size());
}

Path& Path::operator/=(_In_z_ const wchar_t* sub) {
	// using / for strong exception guarantee
	return *this = Path(*this, sub, std::wcslen(sub));
}

Path& Path::operator/=(const std::wstring& sub) {
	// using / for strong exception guarantee
	return *this = Path(*this, sub);
}

Path& Path::operator/=(const std::wstring_view& sub) {
	// using / for strong exception guarantee
	return *this = Path(*this, sub);
}

Path& Path::operator/=(const Filename& sub) {
	// using / for strong exception guarantee
	return *this = Path(*this, sub);
}

Path Path::operator/(_In_z_ const wchar_t* sub) const {
	return Path(*this, sub, std::wcslen(sub));
}

Path Path::operator/(const std::wstring& sub) const {
	return Path(*this, sub);
}

Path Path::operator/(const std::wstring_view& sub) const {
	return Path(*this, sub);
}

Path Path::operator/(const Filename& sub) const {
	return Path(*this, sub);
}

Path& Path::operator+=(const wchar_t append) {
	// using + for strong exception guarantee
	return *this = *this + append;
}

Path& Path::operator+=(_In_z_ const wchar_t* const append) {
	// using + for strong exception guarantee
	return *this = *this + append;
}

Path& Path::operator+=(const std::wstring& append) {
	// using + for strong exception guarantee
	return *this = *this + append;
}

Path& Path::operator+=(const std::wstring_view& append) {
	// using + for strong exception guarantee
	return *this = *this + append;
}

Path& Path::operator+=(const Filename& append) {
	// using + for strong exception guarantee
	return *this = *this + append;
}

Path Path::operator+(const wchar_t append) const {
	Path result(*this);
	result.m_path += append;
	return result;
}

Path Path::operator+(_In_z_ const wchar_t* const append) const {
	Path result(*this);
	result.m_path += append;
	return result;
}

Path Path::operator+(const std::wstring& append) const {
	Path result(*this);
	result.m_path += append;
	return result;
}

Path Path::operator+(const std::wstring_view& append) const {
	Path result(*this);
	result.m_path += append;
	return result;
}

Path Path::operator+(const Filename& append) const {
	Path result(*this);
	result.m_path += append.sv();
	return result;
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
	return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

Path Path::GetParent() const {
	Path result(*this);

	// according to spec, it is allowed to set the terminating 0 character in std::basic_string to 0
	const HRESULT hr = PathCchRemoveFileSpec(result.m_path.data(), result.m_path.size() + 1);
	COM_HR(hr, "PathCchRemoveFileSpec {}", result.m_path);
	if (hr != S_FALSE) {
		wchar_t* pEnd;          // NOLINT(cppcoreguidelines-init-variables): Initialized as out parameter.
		std::size_t remaining;  // NOLINT(cppcoreguidelines-init-variables): Initialized as out parameter.
		COM_HR(PathCchRemoveBackslashEx(result.m_path.data(), result.m_path.size() + 1, &pEnd, &remaining), "PathCchRemoveBackslash {}", result.m_path.c_str());
		result.m_path.resize(pEnd - result.m_path.data() + (*pEnd ? 1 : 0));
	}
	return result;
}

Filename Path::GetFilename() const {
	wchar_t* pFilename;  // NOLINT(cppcoreguidelines-init-variables): Initialized as out parameter.
	DWORD len;           // NOLINT(cppcoreguidelines-init-variables): Initialized in the followin block.
	if (m_path.size() < MAX_PATH) {
		wchar_t buffer[MAX_PATH];
		len = GetFullPathNameW(m_path.c_str(), MAX_PATH, buffer, &pFilename);
		if (!len) {
			THROW(m3c::windows_exception(GetLastError()), "GetFullPathName {}", m_path);
		}
		if (len < MAX_PATH) {
			return pFilename ? Filename(pFilename, buffer + len - pFilename) : Filename(L"");
		}
		// I have no idea, how this should ever happen because path is always normalized
		assert(false);
	} else {
		len = static_cast<DWORD>(m_path.size());
	}

	std::wstring path(len, L'\0');
	// according to spec, it is allowed to set the terminating 0 character in std::basic_string to 0
	len = GetFullPathNameW(m_path.c_str(), len + 1, path.data(), &pFilename);
	if (!len) {
		THROW(m3c::windows_exception(GetLastError()), "GetFullPathName {}", m_path);
	}
	if (len > path.size()) {
		path.resize(len);
		len = GetFullPathNameW(m_path.c_str(), len + 1, path.data(), &pFilename);
		if (!len || len > path.size()) {
			THROW(m3c::windows_exception(GetLastError()), "GetFullPathName {}", m_path);
		}
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
			// according to spec, it is allowed to set the terminating 0 character in std::basic_string to 0
			alternativePath[0].resize(size - 1);

			hFind = FindFirstFileNameW(m_path.c_str(), 0, &size, alternativePath[0].data());
			if (!hFind) {
				THROW(m3c::windows_exception(GetLastError()), "FindFirstFileName {}", m_path);
			}
			assert(alternativePath[0][size - 1] == L'\0');
		}

		size = 0;
		if (!FindNextFileNameW(hFind, &size, alternativePath[1].data())) {
			if (const DWORD lastError = GetLastError(); lastError == ERROR_MORE_DATA) {
				// according to spec, it is allowed to set the terminating 0 character in std::basic_string to 0
				alternativePath[1].resize(size - 1);

				if (!FindNextFileNameW(hFind, &size, alternativePath[1].data())) {
					THROW(m3c::windows_exception(lastError), "FindFirstFileName {}", m_path);
				}
				assert(alternativePath[1][size - 1] == L'\0');
			} else if (lastError != ERROR_HANDLE_EOF) {
				THROW(m3c::windows_exception(lastError), "FindNextFileName {}", m_path);
			} else {
				// ERROR_HANDLE_EOF -> ok
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
		const wchar_t* pRootEnd;  // NOLINT(cppcoreguidelines-init-variables): Initialized as out parameter.
		COM_HR(PathCchSkipRoot(m_path.c_str(), &pRootEnd), "PathCchSkipRoot {}", m_path);
		const std::size_t rootLen = pRootEnd - m_path.c_str() + kPrefixLen /* \\?\ */;

		std::wstring path(rootLen + alternativePath[1].size(), L'\0');
		COM_HR(PathCchCombineEx(path.data(), path.size() + 1, m_path.c_str(), alternativePath[1].c_str(), PATHCCH_ALLOW_LONG_PATHS), "PathCchCombineEx {} {}", m_path, alternativePath[1]);

		if (!SetFileAttributesW(path.c_str(), attributes)) {
			LOG_DEBUG("SetFileAttributes {}: {}", path.c_str(), lg::LastError());  // path might have trailing \0

			path.resize(rootLen + alternativePath[0].size());
			COM_HR(PathCchCombineEx(path.data(), path.size() + 1, m_path.c_str(), alternativePath[0].c_str(), PATHCCH_ALLOW_LONG_PATHS), "PathCchCombineEx {} {}", m_path, alternativePath[0]);
			if (!SetFileAttributesW(path.c_str(), attributes)) {
				THROW(m3c::windows_exception(GetLastError()), "SetFileAttributes {}", path.c_str());  // path might have trailing \0
			}
		}
	}
}

void Path::swap(Path& path) noexcept {  // NOLINT(readability-identifier-naming): For std::swap.
	std::swap(m_path, path.m_path);
}

std::size_t Path::hash() const noexcept {  // NOLINT(readability-identifier-naming): For std::hash.
	return GetCaseInsensitiveHash(m_path.c_str(), m_path.size());
}

llamalog::LogLine& operator<<(llamalog::LogLine& logLine, const Filename& filename) {
	return logLine << filename.sv();
}

llamalog::LogLine& operator<<(llamalog::LogLine& logLine, const Path& path) {
	return logLine << path.sv();
}

namespace internal {

fmt::format_parse_context::iterator filesystem_base_formatter::parse(const fmt::format_parse_context& ctx) {  // NOLINT(readability-identifier-naming, readability-convert-member-functions-to-static): MUST use name as in fmt::formatter.
	auto it = ctx.begin();
	const auto last = ctx.end();
	if (it != last && *it == ':') {
		++it;
	}
	auto end = it;
	while (end != ctx.end() && *end != '}') {
		++end;
	}

	m_format.reserve(end - it + 3);
	m_format.assign("{:");
	m_format.append(it, end);
	m_format.push_back('}');
	return end;
}

fmt::format_context::iterator filesystem_base_formatter::Format(const wchar_t* const str, const std::size_t len, fmt::format_context& ctx) const {
	const std::string value = m3c::EncodeUtf8(str, len);
	return fmt::format_to(ctx.out(), m_format, value);
}

}  // namespace internal
}  // namespace systools

fmt::format_context::iterator fmt::formatter<systools::Filename>::format(const systools::Filename& arg, fmt::format_context& ctx) {  // NOLINT(readability-identifier-naming, readability-convert-member-functions-to-static): MUST use name as in fmt::formatter.
	return Format(arg.c_str(), arg.size(), ctx);
}

fmt::format_context::iterator fmt::formatter<systools::Path>::format(const systools::Path& arg, fmt::format_context& ctx) {  // NOLINT(readability-identifier-naming, readability-convert-member-functions-to-static): MUST use name as in fmt::formatter.
	return Format(arg.c_str(), arg.size(), ctx);
}
