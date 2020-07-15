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

#pragma once

#include "systools/DirectoryScanner.h"
#include "systools/Path.h"

#include <m3c/lazy_string.h>

#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef __clang_analyzer__
// Avoid collisions with Windows API defines
#undef CreateDirectory
#undef CreateHardLink
#endif

namespace systools::test {

class BackupFileSystem_Fake {
public:
	using content_type = m3c::lazy_string<16>;
	using security_type = m3c::lazy_string<16>;

public:
	struct Entry {
		struct Stream {
			m3c::lazy_wstring<32> name;
			std::uint64_t size;
			DWORD attributes;
			content_type content;

			[[nodiscard]] bool operator==(const Stream& oth) const noexcept;
		};
		Entry() noexcept = default;
		Entry(const Filename& filename, bool directory);
		Entry(const Entry&) = default;
		Entry(Entry&&) noexcept = default;

		[[nodiscard]] bool operator==(const Entry& oth) const noexcept;

		[[nodiscard]] bool IsDirectory() const;
		[[nodiscard]] bool HasSameAttributes(const Entry& oth) const noexcept;

		[[nodiscard]] Entry CreateCopy() const;

		std::uint64_t size;
		std::int64_t creationTime = ++BackupFileSystem_Fake::m_timestamp;
		std::int64_t lastWriteTime = ++BackupFileSystem_Fake::m_timestamp;
		DWORD attributes;
		std::wstring filename;
		content_type content;
		std::uint64_t fileId = ++BackupFileSystem_Fake::m_fileId;
		std::vector<Stream> streams;
		security_type security;
	};

private:
	class ScannedFile_Fake;

public:
	BackupFileSystem_Fake();

public:
	void Add(Path path, Entry entry, bool root = false);
	const std::unordered_map<Path, Entry>& GetFiles() const noexcept;
	void Dump();

	bool Exists(const Path& path) const;
	bool IsDirectory(const Path& path) const;

	bool Compare(const Path& src, const Path& target) const;
	void CreateDirectory(const Path& path, const Path& templatePath, const ScannedFile& securitySource);
	void CreateDirectoryRecursive(const Path& path);
	void SetAttributes(const Path& path, const ScannedFile& attributesSource);
	void SetSecurity(const Path& path, const ScannedFile& securitySource);
	void Rename(const Path& existingName, const Path& newName);
	void Copy(const Path& source, const Path& target);
	void CreateHardLink(const Path& path, const Path& existing);
	void Delete(const Path& path);

	void Scan(const Path& path, std::vector<ScannedFile>& directories, std::vector<ScannedFile>& files, DirectoryScanner::Flags flags, const ScannerFilter& filter) const;

private:
	void Add(Path path, Path parent, Entry entry, bool root);
	[[nodiscard]] content_type ReadFile(const Path& path) const;

private:
	std::unordered_map<Path, Entry> m_files;
	std::unordered_map<Path, std::unordered_set<Path>> m_directories;

	inline static std::int64_t m_timestamp = 0;
	inline static std::uint64_t m_fileId = 0;

	friend Entry;
};

}  // namespace systools::test
