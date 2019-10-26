#pragma once

#include <fmt/core.h>

#include <string>

namespace llamalog {
class LogLine;
}
namespace systools {

class Filename {
public:
	explicit Filename(const std::wstring& filename)
		: m_filename(filename) {
		// empty
	}
	Filename(const wchar_t* filename, const std::size_t len)
		: m_filename(filename, len) {
		// empty
	}
	Filename(const Filename&) = default;
	Filename(Filename&&) noexcept = default;
	~Filename() noexcept = default;

public:
	Filename& operator=(const Filename&) = default;
	Filename& operator=(Filename&&) noexcept = default;

	bool operator==(const Filename& filename) const;
	bool operator!=(const Filename& filename) const;

	explicit operator const std::wstring&() const noexcept {
		return m_filename;
	}

public:
	const wchar_t* c_str() const noexcept {
		return m_filename.c_str();
	}
	const std::size_t size() const noexcept {
		return m_filename.size();
	}
	const int CompareTo(const Filename& filename) const;
	bool IsSameStringAs(const Filename& filename) const noexcept;

private:
	std::wstring m_filename;
};

class Path {
public:
	explicit Path(const std::wstring& path);
	Path(const Path&) = default;
	Path(Path&&) noexcept = default;
	~Path() noexcept = default;

public:
	Path& operator=(const Path&) = default;
	Path& operator=(Path&&) noexcept = default;

	bool operator==(const Path& path) const;
	bool operator!=(const Path& path) const;

	Path& operator/=(const std::wstring& sub);
	Path& operator/=(const Filename& sub);
	Path operator/(const std::wstring& sub) const;
	Path operator/(const Filename& sub) const;

	explicit operator const std::wstring&() const noexcept {
		return m_path;
	}

public:
	const wchar_t* c_str() const noexcept {
		return m_path.c_str();
	}
	const std::size_t size() const noexcept {
		return m_path.size();
	}

	bool Exists() const;
	bool IsDirectory() const;
	Path GetParent() const;
	Filename GetFilename() const;

	void ForceDelete() const;

private:
	std::wstring m_path;
};

}  // namespace systools

llamalog::LogLine& operator<<(llamalog::LogLine& logLine, const systools::Filename& filename);
llamalog::LogLine& operator<<(llamalog::LogLine& logLine, const systools::Path& path);

template <>
struct fmt::formatter<systools::Filename> {
	/// @brief Parse the format string.
	/// @param ctx see `fmt::formatter::parse`.
	/// @return see `fmt::formatter::parse`.
	fmt::format_parse_context::iterator parse(const fmt::format_parse_context& ctx);  // NOLINT(readability-identifier-naming): MUST use name as in fmt::formatter.

	/// @brief Format the `Filename`.
	/// @param arg A `Filename`.
	/// @param ctx see `fmt::formatter::format`.
	/// @return see `fmt::formatter::format`.
	fmt::format_context::iterator format(const systools::Filename& arg, fmt::format_context& ctx);  // NOLINT(readability-identifier-naming): MUST use name as in fmt::formatter.
};

template <>
struct fmt::formatter<systools::Path> {
	/// @brief Parse the format string.
	/// @param ctx see `fmt::formatter::parse`.
	/// @return see `fmt::formatter::parse`.
	fmt::format_parse_context::iterator parse(const fmt::format_parse_context& ctx);  // NOLINT(readability-identifier-naming): MUST use name as in fmt::formatter.

	/// @brief Format the `Path`.
	/// @param arg A `Path`.
	/// @param ctx see `fmt::formatter::format`.
	/// @return see `fmt::formatter::format`.
	fmt::format_context::iterator format(const systools::Path& arg, fmt::format_context& ctx);  // NOLINT(readability-identifier-naming): MUST use name as in fmt::formatter.
};
