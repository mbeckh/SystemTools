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

#pragma once

#include <m3c/lazy_string.h>

#include <fmt/core.h>

#include <sal.h>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

#ifndef SYSTOOLS_NO_INLINE
#define SYSTOOLS_NO_INLINE
#endif

namespace llamalog {
class LogLine;
}  // namespace llamalog

namespace systools {

class Filename {
private:
	static constexpr std::uint16_t kInlineBufferSize = 32;

public:
	using string_type = m3c::lazy_wstring<kInlineBufferSize>;  // NOLINT(readability-identifier-naming): Follow naming of STL and lazy_wstring.

public:
	Filename(const Filename& oth) = default;
	Filename(Filename&& oth) noexcept = default;

	explicit Filename(const wchar_t* filename);
	Filename(const wchar_t* filename, const std::size_t length)
		: m_filename(filename, length) {
		// empty
	}

	explicit Filename(const std::wstring& filename)
		: m_filename(filename) {
		// empty
	}
	explicit Filename(std::wstring&& filename) noexcept
		: m_filename(std::move(filename)) {
		// empty
	}

	explicit Filename(const std::wstring_view& filename)
		: m_filename(filename) {
		// empty
	}

	explicit Filename(string_type& filename)
		: m_filename(filename) {
		// empty
	}

	explicit Filename(string_type&& filename) noexcept
		: m_filename(std::move(filename)) {
		// empty
	}

	template <std::uint16_t kSize>
	explicit Filename(const m3c::lazy_wstring<kSize>& filename)
		: m_filename(filename) {
		// empty
	}

	template <std::uint16_t kSize>
	explicit Filename(m3c::lazy_wstring<kSize>&& filename)
		: m_filename(std::move(filename)) {
		// empty
	}

	~Filename() noexcept = default;

public:
	Filename& operator=(const Filename& oth) = default;
	Filename& operator=(Filename&& oth) noexcept = default;

	[[nodiscard]] bool operator==(const Filename& filename) const {
		return (*this <=> filename) == 0;
	}

	[[nodiscard]] bool operator==(const wchar_t* const filename) const {
		return (*this <=> filename) == 0;
	}

	[[nodiscard]] bool operator==(const std::wstring& filename) const {
		return (*this <=> filename) == 0;
	}

	[[nodiscard]] bool operator==(const std::wstring_view& filename) const {
		return (*this <=> filename) == 0;
	}

	[[nodiscard]] std::weak_ordering operator<=>(const Filename& filename) const;
	[[nodiscard]] std::weak_ordering operator<=>(const wchar_t* filename) const;
	[[nodiscard]] std::weak_ordering operator<=>(const std::wstring& filename) const;
	[[nodiscard]] std::weak_ordering operator<=>(const std::wstring_view& filename) const;

public:
	[[nodiscard]] std::wstring_view sv() const noexcept {  // NOLINT(readability-identifier-naming): Naming relates to STL types.
		return m_filename.sv();
	}
	[[nodiscard]] const string_type& str() const noexcept {  // NOLINT(readability-identifier-naming): Naming relates to STL(-like) types.
		return m_filename;
	}
	[[nodiscard]] const wchar_t* c_str() const noexcept {  // NOLINT(readability-identifier-naming): Naming relates to STL types.
		return m_filename.c_str();
	}
	[[nodiscard]] std::size_t size() const noexcept {  // NOLINT(readability-identifier-naming): Naming relates to STL types.
		return m_filename.size();
	}
	[[nodiscard]] bool IsSameStringAs(const Filename& filename) const noexcept;

	/// @brief Swap two objects.
	/// @param filename The other `Filename`.
	void swap(Filename& filename) noexcept;  // NOLINT(readability-identifier-naming): Naming relates to STL types.

	/// @brief Get a hash value for the object.
	/// @return A hash value calculated based on the string value.
	[[nodiscard]] std::size_t hash() const noexcept;  // NOLINT(readability-identifier-naming): Naming relates to STL types.

private:
	string_type m_filename;
};

class Path {
private:
	static constexpr std::uint16_t kInlineBufferSize = 128;

public:
	using string_type = m3c::lazy_wstring<kInlineBufferSize>;  // NOLINT(readability-identifier-naming): Follow naming of STL and lazy_wstring.

public:
	Path(const Path&) = default;
	Path(Path&&) noexcept = default;

	/// @brief Creates a new path from a null-terminated C-string.
	/// @param path The pointer to the null-terminated C-string.
	explicit Path(_In_z_ const wchar_t* path);

	/// @brief Creates a new path from a `std::wstring`.
	/// @param path The `std::wstring` object.
	explicit Path(const std::wstring& path);

	/// @brief Creates a new path from a `std::wstring_view`.
	/// @param path The `std::wstring_view` object.
	explicit Path(const std::wstring_view& path);

	explicit Path(string_type& path)
		: m_path(path) {
		// empty
	}

	explicit Path(string_type&& path) noexcept
		: m_path(std::move(path)) {
		// empty
	}

	template <std::size_t kSize>
	explicit Path(const m3c::lazy_wstring<kSize>& path)
		: m_path(path) {
		// empty
	}

	template <std::size_t kSize>
	explicit Path(m3c::lazy_wstring<kSize>&& path)
		: m_path(std::move(path)) {
		// empty
	}

private:
	/// @brief Creates a new path from a null-terminated C-string.
	/// @param path The pointer to the null-terminated C-string.
	/// @param length Used as a hint for sizing internal buffers.
	Path(_In_z_ const wchar_t* path, std::size_t length);

public:
	Path(const Path& path, const wchar_t* sub, std::size_t subSize);

	/// @brief Creates a new path from a @p path and a @p sub path.
	/// @param path The `Path` object.
	/// @param sub The sub path to append.
	Path(const Path& path, const std::wstring& sub)
		: Path(path, sub.c_str(), sub.size()) {
	}
	Path(const Path& path, const std::wstring_view& sub)
		: Path(path, sub.data(), sub.size()) {
	}
	Path(const Path& path, const Filename& sub)
		: Path(path, sub.sv()) {
	}

public:
	~Path() noexcept = default;

public:
	Path& operator=(const Path&) = default;
	Path& operator=(Path&&) noexcept = default;

	[[nodiscard]] bool operator==(const Path& path) const {
		return (*this <=> path) == 0;
	}

	[[nodiscard]] bool operator==(const wchar_t* const path) const {
		return (*this <=> path) == 0;
	}

	[[nodiscard]] bool operator==(const std::wstring& path) const {
		return (*this <=> path) == 0;
	}

	[[nodiscard]] bool operator==(const std::wstring_view& path) const {
		return (*this <=> path) == 0;
	}

	[[nodiscard]] std::weak_ordering operator<=>(const Path& path) const;
	[[nodiscard]] std::weak_ordering operator<=>(const wchar_t* path) const;
	[[nodiscard]] std::weak_ordering operator<=>(const std::wstring& path) const;
	[[nodiscard]] std::weak_ordering operator<=>(const std::wstring_view& path) const;

	Path& operator/=(_In_z_ const wchar_t* sub);
	Path& operator/=(const std::wstring& sub);
	Path& operator/=(const std::wstring_view& sub);
	Path& operator/=(const Filename& sub);
	[[nodiscard]] Path operator/(const wchar_t* sub) const;
	[[nodiscard]] Path operator/(const std::wstring& sub) const;
	[[nodiscard]] Path operator/(const std::wstring_view& sub) const;
	[[nodiscard]] Path operator/(const Filename& sub) const;

	Path& operator+=(wchar_t append);
	Path& operator+=(_In_z_ const wchar_t* append);
	Path& operator+=(const std::wstring& append);
	Path& operator+=(const std::wstring_view& append);
	Path& operator+=(const Filename& append);

	template <std::size_t kSize>
	Path& operator+=(const m3c::lazy_wstring<kSize>& append) const {
		// using + for strong exception guarantee
		return *this = std::move(*this + append);
	}

	[[nodiscard]] Path operator+(wchar_t append) const;
	[[nodiscard]] Path operator+(_In_z_ const wchar_t* append) const;
	[[nodiscard]] Path operator+(const std::wstring& append) const;
	[[nodiscard]] Path operator+(const std::wstring_view& append) const;
	[[nodiscard]] Path operator+(const Filename& append) const;

	template <std::size_t kSize>
	[[nodiscard]] Path operator+(const m3c::lazy_wstring<kSize>& append) const {
		Path result(*this);
		result.m_path += append;
		return result;
	}

public:
	[[nodiscard]] std::wstring_view sv() const noexcept {  // NOLINT(readability-identifier-naming): Naming relates to STL types.
		return m_path.sv();
	}
	[[nodiscard]] const string_type& str() const noexcept {  // NOLINT(readability-identifier-naming): Naming relates to STL(-like) types.
		return m_path;
	}
	[[nodiscard]] const wchar_t* c_str() const noexcept {  // NOLINT(readability-identifier-naming): Naming relates to STL types.
		return m_path.c_str();
	}
	[[nodiscard]] std::size_t size() const noexcept {  // NOLINT(readability-identifier-naming): Naming relates to STL types.
		return m_path.size();
	}

	[[nodiscard]] SYSTOOLS_NO_INLINE bool Exists() const;
	[[nodiscard]] SYSTOOLS_NO_INLINE bool IsDirectory() const;
	[[nodiscard]] Path GetParent() const;
	[[nodiscard]] Filename GetFilename() const;

	void ForceDelete() const;

	/// @brief Swap two objects.
	/// @param path The other `Path`.
	void swap(Path& path) noexcept;  // NOLINT(readability-identifier-naming): Naming relates to STL types.


	/// @brief Get a hash value for the object.
	/// @return A hash value calculated based on the string value.
	[[nodiscard]] std::size_t hash() const noexcept;  // NOLINT(readability-identifier-naming): Naming relates to STL types.

private:
	string_type m_path;
};

/// @brief Swap function.
/// @param filename A `Filename` object.
/// @param oth Another `Filename` object.
inline void swap(Filename& filename, Filename& oth) noexcept {  // NOLINT(readability-identifier-naming): Specialization for std::swap.
	filename.swap(oth);
}

/// @brief Swap function.
/// @param path A `Path` object.
/// @param oth Another `Path` object.
inline void swap(Path& path, Path& oth) noexcept {  // NOLINT(readability-identifier-naming): Specialization for std::swap.
	path.swap(oth);
}

llamalog::LogLine& operator<<(llamalog::LogLine& logLine, const Filename& filename);
llamalog::LogLine& operator<<(llamalog::LogLine& logLine, const Path& path);

namespace internal {

struct FilesystemBaseFormatter {
	/// @brief Parse the format string.
	/// @param ctx see `fmt::formatter::parse`.
	/// @return see `fmt::formatter::parse`.
	fmt::format_parse_context::iterator parse(const fmt::format_parse_context& ctx);  // NOLINT(readability-identifier-naming): MUST use name as in fmt::formatter.

protected:
	fmt::format_context::iterator Format(const wchar_t* str, std::size_t len, fmt::format_context& ctx) const;

private:
	std::string m_format;
};

}  // namespace internal

}  // namespace systools

/// @brief Specialization of std::hash.
template <>
struct std::hash<systools::Filename> {
	[[nodiscard]] std::size_t operator()(const systools::Filename& filename) const noexcept {
		return filename.hash();
	}
};

/// @brief Specialization of std::hash.
template <>
struct std::hash<systools::Path> {
	[[nodiscard]] std::size_t operator()(const systools::Path& path) const noexcept {
		return path.hash();
	}
};

template <>
struct fmt::formatter<systools::Filename> : public systools::internal::FilesystemBaseFormatter {
	/// @brief Format the `Filename`.
	/// @param arg A `Filename`.
	/// @param ctx see `fmt::formatter::format`.
	/// @return see `fmt::formatter::format`.
	fmt::format_context::iterator format(const systools::Filename& arg, fmt::format_context& ctx);  // NOLINT(readability-identifier-naming): MUST use name as in fmt::formatter.
};

template <>
struct fmt::formatter<systools::Path> : public systools::internal::FilesystemBaseFormatter {
	/// @brief Format the `Path`.
	/// @param arg A `Path`.
	/// @param ctx see `fmt::formatter::format`.
	/// @return see `fmt::formatter::format`.
	fmt::format_context::iterator format(const systools::Path& arg, fmt::format_context& ctx);  // NOLINT(readability-identifier-naming): MUST use name as in fmt::formatter.
};
