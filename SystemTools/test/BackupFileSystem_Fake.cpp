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

#include "BackupFileSystem_Fake.h"

#include "systools/BackupStrategy.h"
#include "systools/DirectoryScanner.h"

#include <llamalog/llamalog.h>
#include <m3c/string_encode.h>

#include <numeric>
#include <ostream>
#include <random>

namespace systools::test {

namespace {

std::mt19937 g_random(1234U);
std::uniform_int_distribution g_sizeDistribution(10, 200);

class FakeFileSystemException : public std::exception {
	// empty
};

}  // namespace

class BackupFileSystem_Fake::ScannedFile_Fake : public ScannedFile {
public:
	explicit ScannedFile_Fake(const Entry& entry)
		: ScannedFile(Filename(entry.filename), LARGE_INTEGER{.QuadPart = static_cast<std::int64_t>(entry.size)}, LARGE_INTEGER{.QuadPart = entry.creationTime}, LARGE_INTEGER{.QuadPart = entry.lastWriteTime}, entry.attributes, GetFileId(entry.fileId), GetStreams(entry.streams)) {
		assert(entry.size <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()));
		GetSecurity().pSecurityDescriptor.reset(new security_type(entry.security));
	}

private:
	static FILE_ID_128 GetFileId(const std::uint64_t id) {
		static_assert(sizeof(FILE_ID_128::Identifier) >= sizeof(id));
		FILE_ID_128 result{};
		std::memcpy(&result.Identifier[sizeof(result.Identifier) - sizeof(id)], &id, sizeof(id));
		return result;
	}

	static std::vector<ScannedFile::Stream> GetStreams(const std::vector<Entry::Stream>& streams) {
		std::vector<ScannedFile::Stream> result;
		result.reserve(streams.size());
		for (const Entry::Stream& stream : streams) {
			result.emplace_back(ScannedFile::Stream::name_type(stream.name), LARGE_INTEGER{.QuadPart = static_cast<std::int64_t>(stream.size)}, stream.attributes);
		}
		return result;
	}
};

BackupFileSystem_Fake::Entry::Entry(const Filename& filename, bool directory)
	: filename(filename.c_str())
	, attributes(directory ? FILE_ATTRIBUTE_DIRECTORY : 0)
	, size(directory ? 0 : g_sizeDistribution(g_random))
	, content(directory ? "" : "default") {
	// empty
}

bool BackupFileSystem_Fake::Entry::operator==(const Entry& oth) const noexcept {
	return size == oth.size
		   && creationTime == oth.creationTime
		   && lastWriteTime == oth.lastWriteTime
		   && attributes == oth.attributes
		   && filename == oth.filename
		   && content == oth.content
		   && fileId == oth.fileId
		   && streams == oth.streams
		   && security == oth.security;
}

bool BackupFileSystem_Fake::Entry::IsDirectory() const {
	return attributes & FILE_ATTRIBUTE_DIRECTORY;
}

bool BackupFileSystem_Fake::Entry::HasSameAttributes(const Entry& oth) const noexcept {
	return size == oth.size
		   && creationTime == oth.creationTime
		   && lastWriteTime == oth.lastWriteTime
		   && (attributes & BackupStrategy::kCopyAttributeMask) == (oth.attributes & BackupStrategy::kCopyAttributeMask);
}

BackupFileSystem_Fake::Entry BackupFileSystem_Fake::Entry::CreateCopy() const {
	Entry result = *this;
	result.fileId = ++BackupFileSystem_Fake::m_fileId;
	return result;
}


[[nodiscard]] bool BackupFileSystem_Fake::Entry::Stream::operator==(const Stream& oth) const noexcept {
	return name == oth.name
		   && size == oth.size
		   && attributes == oth.attributes
		   && content == oth.content;
}

BackupFileSystem_Fake::BackupFileSystem_Fake() {
	m_files.max_load_factor(0.75f);
	m_files.reserve(16000);

	m_directories.max_load_factor(0.75f);
	m_directories.reserve(16000);
}

void BackupFileSystem_Fake::Add(Path path, Entry entry, const bool root) {
	Add(std::move(path), path.GetParent(), std::move(entry), root);
}

void BackupFileSystem_Fake::Add(Path path, Path parent, Entry entry, const bool root) {
	if (root) {
		if (parent != path && !Exists(parent)) {
			Add(parent, Entry(parent.GetFilename(), true), true);
		}
	} else {
		if (parent == path) {
			THROW(FakeFileSystemException(), "{} must be root", path);
		}
		if (!IsDirectory(parent)) {
			THROW(FakeFileSystemException(), "parent {} is not a directory for {}", parent, path);
		}
	}

	std::unordered_set<Path>& directory = m_directories[std::move(parent)];
	if (directory.empty()) {
		directory.max_load_factor(0.75f);
		directory.reserve(256);
	}
	if (!directory.insert(path).second) {
		THROW(FakeFileSystemException(), "{} is duplicate in {}", path, path.GetParent());
	}
	if (!m_files.insert({std::move(path), std::move(entry)}).second) {
		THROW(FakeFileSystemException(), "{} is duplicate", path);
	}
}

const std::unordered_map<Path, BackupFileSystem_Fake::Entry>& BackupFileSystem_Fake::GetFiles() const noexcept {
	return m_files;
}

void BackupFileSystem_Fake::Dump() {
	std::vector<Path> paths;
	paths.reserve(m_files.size());
	for (auto it : m_files) {
		paths.emplace_back(it.first.GetParent() / it.second.filename);
	}
	std::sort(paths.begin(), paths.end());

	for (const Path& path : paths) {
		const Entry& entry = m_files[path];
		std::cout << std::setw(10) << std::hex << entry.creationTime;
		std::cout << " ";
		std::cout << std::setw(10) << std::hex << entry.lastWriteTime;
		std::cout << " ";
		if (entry.IsDirectory()) {
			std::cout << "<DIR>";
		} else {
			std::cout << std::setw(5) << std::dec << std::setfill(' ') << std::right << entry.size;
		}
		std::cout << " ";
		std::cout << std::setw(4) << std::hex << std::setfill('0') << entry.attributes;
		std::cout << " ";
		std::cout << std::setw(8) << entry.fileId;
		std::cout << " ";
		std::cout << m3c::EncodeUtf8((path.GetParent() / entry.filename).c_str());
		std::cout << " ";
		if (!entry.IsDirectory() && entry.content != "default") {
			std::cout << "[" << entry.content.sv() << "]";
		}
		std::cout << "\n";
	}
}

bool BackupFileSystem_Fake::Exists(const Path& path) const {
	return m_files.contains(path);
}

bool BackupFileSystem_Fake::IsDirectory(const Path& path) const {
	auto it = m_files.find(path);
	if (it == m_files.end()) {
		THROW(FakeFileSystemException(), "{} not found", path);
	}
	return it->second.IsDirectory();
}

[[nodiscard]] BackupFileSystem_Fake::content_type BackupFileSystem_Fake::ReadFile(const Path& path) const {
	const std::wstring_view str = path.sv();
	const std::size_t pos = str.find_first_of(L':');

	if (pos == decltype(str)::npos) {
		if (IsDirectory(path)) {
			THROW(FakeFileSystemException(), "{} is a directory", path);
		}
		return m_files.at(path).content;
	}

	const Entry& entry = m_files.at(Path(str.substr(0, pos)));
	const std::wstring_view streamName = str.substr(pos);

	for (const Entry::Stream& stream : entry.streams) {
		if (stream.name == streamName) {
			return stream.content;
		}
	}

	THROW(FakeFileSystemException(), "{} not found", path);
}

bool BackupFileSystem_Fake::Compare(const Path& src, const Path& target) const {
	return ReadFile(src) == ReadFile(target);
}

void BackupFileSystem_Fake::CreateDirectory(const Path& path, const Path& templatePath, const ScannedFile& securitySource) {
	if (!IsDirectory(templatePath)) {
		THROW(FakeFileSystemException(), "{} is not a directory", templatePath);
	}
	Path parent = path.GetParent();
	if (!IsDirectory(parent)) {
		THROW(FakeFileSystemException(), "{} is not a directory for {}", parent, path);
	}

	Entry entry = m_files.at(templatePath);
	entry.creationTime = ++m_timestamp;
	entry.lastWriteTime = m_timestamp;
	entry.filename = path.GetFilename().c_str();
	entry.fileId = ++m_fileId;
	entry.streams.clear();
	entry.security = *reinterpret_cast<const security_type*>(securitySource.GetSecurity().pSecurityDescriptor.get());
	Add(path, std::move(parent), std::move(entry), false);
}

void BackupFileSystem_Fake::CreateDirectoryRecursive(const Path& path) {
	const Path parent = path.GetParent();
	if (parent == path) {
		THROW(FakeFileSystemException(), "root is missing for {}", path);
	}
	if (!Exists(parent)) {
		CreateDirectoryRecursive(parent);
	}
	if (!IsDirectory(parent)) {
		THROW(FakeFileSystemException(), "{} is not a directory for {}", parent, path);
	}

	Add(path, std::move(parent), Entry(path.GetFilename(), true), false);
}

void BackupFileSystem_Fake::SetAttributes(const Path& path, const ScannedFile& attributesSource) {
	Entry& entry = m_files.at(path);
	entry.creationTime = attributesSource.GetCreationTime();
	entry.lastWriteTime = attributesSource.GetLastWriteTime();
	entry.attributes = (entry.attributes & ~BackupStrategy::kCopyAttributeMask) | (attributesSource.GetAttributes() & BackupStrategy::kCopyAttributeMask);
}

void BackupFileSystem_Fake::SetSecurity(const Path& path, const ScannedFile& securitySource) {
	Entry& entry = m_files.at(path);
	entry.security = *reinterpret_cast<const security_type*>(securitySource.GetSecurity().pSecurityDescriptor.get());
}

void BackupFileSystem_Fake::Rename(const Path& existingName, const Path& newName) {
	const Path parent = newName.GetParent();
	if (!IsDirectory(parent)) {
		THROW(FakeFileSystemException(), "{} is not a directory for {}", parent, newName);
	}

	auto node = m_files.extract(existingName);
	if (node.empty()) {
		THROW(FakeFileSystemException(), "{} does not exist", existingName);
	}
	node.key() = newName;
	node.mapped().filename = newName.GetFilename().c_str();
	if (!m_files.insert(std::move(node)).inserted) {
		THROW(FakeFileSystemException(), "{} already exists", newName);
	};

	if (m_directories.at(parent).erase(existingName) != 1) {
		THROW(FakeFileSystemException(), "{} does not exist in {}", existingName, parent);
	}

	std::unordered_set<Path>& directory = m_directories[newName.GetParent()];
	if (!directory.insert(newName).second) {
		THROW(FakeFileSystemException(), "{} already exists in {}", newName, newName.GetParent());
	}
}

void BackupFileSystem_Fake::Copy(const Path& source, const Path& target) {
	if (IsDirectory(source)) {
		THROW(FakeFileSystemException(), "{} is a directory", source);
	}
	const Path parent = target.GetParent();
	if (!IsDirectory(parent)) {
		THROW(FakeFileSystemException(), "{} is not a directory for {}", parent, target);
	}

	Entry entry = m_files.at(source);
	entry.creationTime = ++m_timestamp;
	entry.filename = target.GetFilename().c_str();
	entry.fileId = ++m_fileId;
	Add(target, std::move(parent), std::move(entry), false);
}

void BackupFileSystem_Fake::CreateHardLink(const Path& path, const Path& existing) {
	const Path parent = path.GetParent();
	if (!IsDirectory(parent)) {
		THROW(FakeFileSystemException(), "{} is not a directory for {}", parent, path);
	}
	if (IsDirectory(existing)) {
		THROW(FakeFileSystemException(), "{} is a directory", existing);
	}

	Entry entry = m_files.at(existing);
	entry.filename = path.GetFilename().c_str();
	Add(path, std::move(parent), std::move(entry), false);
}

void BackupFileSystem_Fake::Delete(const Path& path) {
	if (m_files.erase(path) != 1) {
		THROW(FakeFileSystemException(), "error removing {}", path);
	}
	if (m_directories.at(path.GetParent()).erase(path) != 1) {
		THROW(FakeFileSystemException(), "{} does not exist in {}", path, path.GetParent());
	}
}

void BackupFileSystem_Fake::Scan(const Path& path, std::vector<ScannedFile>& directories, std::vector<ScannedFile>& files, DirectoryScanner::Flags /* flags */, const ScannerFilter& filter) const {
	if (!IsDirectory(path)) {
		THROW(FakeFileSystemException(), "{} is not a directory", path);
	}
	const auto it = m_directories.find(path);
	if (it != m_directories.end()) {
		for (const Path& pathEntry : it->second) {
			const BackupFileSystem_Fake::Entry& entry = m_files.at(pathEntry);
			ScannedFile_Fake fakeFile(entry);
			if (filter.Accept(entry.filename)) {
				if (entry.IsDirectory()) {
					directories.push_back(std::move(fakeFile));
				} else {
					files.push_back(std::move(fakeFile));
				}
			}
		}
	}
}

}  // namespace systools::test
