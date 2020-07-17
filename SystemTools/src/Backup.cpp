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

#include "systools/Backup.h"

#include "systools/BackupStrategy.h"
#include "systools/DirectoryScanner.h"
#include "systools/Path.h"
#include "systools/ThreeWayMerge.h"

#include <llamalog/llamalog.h>
#include <m3c/Handle.h>
#include <m3c/exception.h>
#include <m3c/finally.h>

#include <windows.h>

#include <algorithm>
#include <cassert>
#include <compare>
#include <cstddef>
#include <exception>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>


namespace systools {

namespace {

int CompareName(const ScannedFile& lhs, const ScannedFile& rhs) {
	// attributes are always updated for directories
	const std::weak_ordering cmp = lhs.GetName() <=> rhs.GetName();
	return cmp < 0 ? -1 : cmp == 0 ? 0 : 1;
}

bool SameAttributes(const ScannedFile& lhs, const ScannedFile& rhs) {
	if (lhs.GetLastWriteTime() != rhs.GetLastWriteTime()
		|| lhs.GetSize() != rhs.GetSize()
		|| (lhs.GetAttributes() & BackupStrategy::kCopyAttributeMask) != (rhs.GetAttributes() & BackupStrategy::kCopyAttributeMask)
		// check creation time last because it rarely changes
		|| lhs.GetCreationTime() != rhs.GetCreationTime()) {
		return false;
	}

	return lhs.GetStreams() == rhs.GetStreams();
}

bool SameSecurity(const ScannedFile& lhs, const ScannedFile& rhs) {
	return lhs.GetSecurity() == rhs.GetSecurity();
}

constexpr std::size_t MaxOfDifferenceAndZero(const std::size_t minuend, const std::size_t subtrahend) noexcept {
	return minuend > subtrahend ? minuend - subtrahend : 0;
}

void WaitForScanNoThrow(BackupStrategy& strategy, DirectoryScanner& scanner) noexcept {
	try {
		// if no operation is currently in progress, wait just happily returns
		strategy.WaitForScan(scanner);
	} catch (const std::exception& e) {
		// dtor has nothrow requirement
		SLOG_ERROR("WaitForScan: {}", e);
	} catch (...) {
		SLOG_ERROR("WaitForScan");
	}
}

}  // namespace


struct Backup::Match final {
	Match(std::optional<ScannedFile> s, std::optional<ScannedFile> r, std::optional<ScannedFile> d) noexcept
		: src(std::move(s))
		, ref(std::move(r))
		, dst(std::move(d)) {
		assert(!src.has_value() || !ref.has_value() || src->GetName() == ref->GetName());
		assert(!src.has_value() || !dst.has_value() || src->GetName() == dst->GetName());
		assert(!src.has_value() || !dst.has_value() || src->GetName() == dst->GetName());
		assert(!ref.has_value() || !dst.has_value() || ref->GetName() == dst->GetName());
	}
	Match(const Match& match) = default;
	Match(Match&& match) noexcept = default;
	~Match() noexcept = default;

	Match& operator=(const Match& match) = default;
	Match& operator=(Match&& match) noexcept = default;

	std::optional<ScannedFile> src;
	std::optional<ScannedFile> ref;
	std::optional<ScannedFile> dst;
};


std::uint64_t Backup::Statistics::GetFolders() const noexcept {
	return m_added.GetFolders() + m_updated.GetFolders() + m_retained.GetFolders();
}
std::uint64_t Backup::Statistics::GetFiles() const noexcept {
	return m_added.GetFiles() + m_updated.GetFiles() + m_retained.GetFiles();
}

std::uint64_t Backup::Statistics::GetBytesTotal() const noexcept {
	return m_added.GetSize() + m_updated.GetSize() + m_retained.GetSize();
}

std::uint64_t Backup::Statistics::GetBytesInHardLinks() const noexcept {
	return m_bytesInHardLinks;
}

std::uint64_t Backup::Statistics::GetBytesCopied() const noexcept {
	return m_bytesCopied;
}

std::uint64_t Backup::Statistics::GetBytesCreatedInHardLinks() const noexcept {
	return m_bytesCreatedInHardLinks;
}

void Backup::Statistics::OnAdd(const Match& match) {
	OnEvent(m_added, *match.src);
}

void Backup::Statistics::OnUpdate(const Match& match) {
	OnEvent(m_updated, *match.src);

	if (!match.src->IsDirectory() && match.ref.has_value() && match.dst.has_value() && match.ref->IsHardLink(*match.dst)) {
		m_bytesInHardLinks += match.dst->GetSize();
	}
}

void Backup::Statistics::OnRetain(const Match& match) {
	OnEvent(m_retained, *match.src);

	if (!match.src->IsDirectory() && match.ref.has_value() && match.dst.has_value() && match.ref->IsHardLink(*match.dst)) {
		m_bytesInHardLinks += match.dst->GetSize();
	}
}

void Backup::Statistics::OnRemove(const Match& match) {
	OnEvent(m_removed, *match.dst);
}

void Backup::Statistics::OnReplace(const Match& match) {
	OnEvent(m_replaced, *match.dst);
	OnEvent(m_updated, *match.src);  // do NOT count the hard link sizes here because they are from the OLD file!
}

void Backup::Statistics::OnSecurityUpdate(const Match& match) {
	OnEvent(m_securityUpdated, *match.src);
}

void Backup::Statistics::OnCopy(const std::uint64_t bytes) {
	m_bytesCopied += bytes;
}

void Backup::Statistics::OnHardLink(const std::uint64_t bytes) {
	m_bytesInHardLinks += bytes;
	m_bytesCreatedInHardLinks += bytes;
}

void Backup::Statistics::OnEvent(Entry& entry, const ScannedFile& file) {
	if (file.IsDirectory()) {
		++entry.m_folders;
	} else {
		++entry.m_files;
		entry.m_size += file.GetSize();
	}
}


Backup::Backup(BackupStrategy& strategy) noexcept
	: m_strategy(strategy) {
	// empty
}

Backup::Statistics Backup::CreateBackup(const std::vector<Path>& src, const Path& ref, const Path& dst) {
	{
		TOKEN_PRIVILEGES privileges;
		privileges.PrivilegeCount = 1;
		if (!LookupPrivilegeValueW(nullptr, SE_SECURITY_NAME, &privileges.Privileges->Luid)) {
			THROW(m3c::windows_exception(GetLastError()), "LookupPrivilegeValueW");
		}
		privileges.Privileges->Attributes = SE_PRIVILEGE_ENABLED;

		HANDLE handle;  // NOLINT(cppcoreguidelines-init-variables): Initialized as out parameter.
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &handle)) {
			THROW(m3c::windows_exception(GetLastError()), "OpenProcessToken");
		}

		m3c::Handle hToken(handle);
		const BOOL result = AdjustTokenPrivileges(hToken, FALSE, &privileges, 0, nullptr, nullptr);
		const DWORD lastError = GetLastError();
		if (!result || lastError != ERROR_SUCCESS) {
			THROW(m3c::windows_exception(lastError), "AdjustTokenPrivileges");
		}
	}

	// reset statistics
	m_statistics = Statistics();
	if (src.empty()) {
		// nothing to do
		return m_statistics;
	}

	// TODO: root folder
	// TODO: ref and dst must be on same volume

	// group source folders by path
	std::unordered_map<Path, std::unordered_set<Filename>> srcPaths;
	std::unordered_set<Filename> allsrcFilenames;
	for (const Path& srcPath : src) {
		// src folders MUST exist
		if (!m_strategy.Exists(srcPath) || !m_strategy.IsDirectory(srcPath)) {
			THROW(std::exception(), "{} is not a directory", srcPath);
		}

		const Path parent = srcPath.GetParent();
		Filename filename = srcPath.GetFilename();
		assert(!filename.sv().empty());

		// source folders names MUST be unique
		const auto result = allsrcFilenames.insert(filename);
		if (!result.second) {
			THROW(std::exception(), "{} and {} have the same name", srcPath, parent / *result.first);
		}
		srcPaths[parent].insert(std::move(filename));
	}

	// ref and dst folders MAY exist

	const bool refExists = m_strategy.Exists(ref);
	if (refExists && !m_strategy.IsDirectory(ref)) {
		THROW(std::exception(), "{} is not a directory", ref);
	}
	const bool dstExists = m_strategy.Exists(dst);
	if (dstExists && !m_strategy.IsDirectory(dst)) {
		THROW(std::exception(), "{} is not a directory", dst);
	}

	// get matching contents of ref and dst folders
	DirectoryScanner::Result refDirectories;
	DirectoryScanner::Result refFiles;
	DirectoryScanner::Result dstDirectories;
	DirectoryScanner::Result dstFiles;
	const LambdaScannerFilter refdstFilter([&allsrcFilenames](const Filename& name) {
		return allsrcFilenames.contains(name);
	});

	// Ensure that all asynchronous operations on local variables are finished before stack unwind
	const auto waitForAsync = m3c::finally([this]() noexcept {
		WaitForScanNoThrow(m_strategy, m_refScanner);
		WaitForScanNoThrow(m_strategy, m_dstScanner);
	});

	if (refExists) {
		refDirectories.reserve(allsrcFilenames.size());
		m_strategy.Scan(ref, m_refScanner, refDirectories, refFiles, DirectoryScanner::Flags::kDefault, refdstFilter);
	}
	if (dstExists) {
		dstDirectories.reserve(allsrcFilenames.size());
		m_strategy.Scan(dst, m_dstScanner, dstDirectories, dstFiles, DirectoryScanner::Flags::kFolderSecurity, refdstFilter);
	}

	for (auto it = srcPaths.cbegin(), begin = it, end = srcPaths.cend(); it != end; ++it) {
		const Path& srcParentPath = it->first;
		const std::unordered_set<Filename>& filenames = it->second;

		DirectoryScanner::Result srcDirectories;
		DirectoryScanner::Result srcFiles;
		const LambdaScannerFilter srcFilter([filenames](const Filename& name) {
			return filenames.contains(name);
		});
		// different scope for containers, thus a second wait
		const auto waitForAsyncSrc = m3c::finally([this]() noexcept {
			WaitForScanNoThrow(m_strategy, m_srcScanner);
		});

		m_strategy.Scan(srcParentPath, m_srcScanner, srcDirectories, srcFiles, DirectoryScanner::Flags::kFolderStreams | DirectoryScanner::Flags::kFolderSecurity, srcFilter);

		// check ref and dst on first iteration
		if (it == begin) {
			if (refExists) {
				m_strategy.WaitForScan(m_refScanner);
				if (!refFiles.empty()) {
					THROW(std::exception(), "{} is not a directory", ref / refFiles.cbegin()->GetName());
				}
			}
			if (dstExists) {
				m_strategy.WaitForScan(m_dstScanner);
				if (!dstFiles.empty()) {
					THROW(std::exception(), "{} is not a directory", dst / dstFiles.cbegin()->GetName());
				}
			} else {
				// create destination directory on first iteration if required
				m_strategy.CreateDirectoryRecursive(dst);
				// do NOT count for statistics
			}
		}
		m_strategy.WaitForScan(m_srcScanner);
		if (!srcFiles.empty()) {
			// should NEVER happen because it was already checked earlier
			assert(false);
			THROW(std::exception(), "{} is not a directory", srcParentPath / srcFiles.cbegin()->GetName());
		}
		if (srcDirectories.size() != filenames.size()) {
			// should NEVER happen because it was already checked earlier
			assert(false);
			THROW(std::exception(), "Something went wrong for folders in {}", srcParentPath);
		}

		std::vector<Match> copy;
		std::vector<Match> extra;
		copy.reserve(srcDirectories.size());
		extra.reserve(MaxOfDifferenceAndZero(dstDirectories.size(), srcDirectories.size()));
		ThreeWayMerge(srcDirectories, refDirectories, dstDirectories, copy, extra, CompareName);
		if (copy.size() != filenames.size() || std::any_of(copy.cbegin(), copy.cend(), [](const Match& match) noexcept {
				return !match.src.has_value();
			})) {
			// should NEVER happen
			assert(false);
			THROW(std::exception(), "Something went wrong for folders in {}", srcParentPath);
		}
		CopyDirectories(srcParentPath, ref, dst, copy);
	}

	return m_statistics;
}

void Backup::CopyDirectories(const std::optional<Path>& optionalSrc, const std::optional<Path>& optionalRef, const Path& dst, const std::vector<Match>& directories) {
	assert(!directories.empty());
	constexpr std::size_t kReserveDirectories = 64;
	constexpr std::size_t kReserveFiles = 256;

	std::optional<std::size_t> idx[2];

	std::optional<Path> srcPath[2];
	std::optional<Path> refPath[2];
	std::optional<Path> dstPath[2];
	std::optional<Path> dstTargetPath[2];

	DirectoryScanner::Result srcDirectories[2];
	DirectoryScanner::Result refDirectories[2];
	DirectoryScanner::Result dstDirectories[2];

	DirectoryScanner::Result srcFiles[2];
	DirectoryScanner::Result refFiles[2];
	DirectoryScanner::Result dstFiles[2];

	// Ensure that all asynchronous operations on local variables are finished before stack unwind
	const auto waitForAsync = m3c::finally([this]() noexcept {
		WaitForScanNoThrow(m_strategy, m_srcScanner);
		WaitForScanNoThrow(m_strategy, m_refScanner);
		WaitForScanNoThrow(m_strategy, m_dstScanner);
	});

	// loop requires one iteration more then directories.size()!
	for (std::size_t index = 0, maxIndex = directories.size(); index <= maxIndex; ++index) {
		// scan next directory
		if (index < maxIndex) {
			const std::uint_fast8_t scanIndex = index & 1;
			assert(!idx[scanIndex].has_value());

			assert(!srcPath[scanIndex].has_value());
			assert(!refPath[scanIndex].has_value());
			assert(!dstPath[scanIndex].has_value());
			assert(!dstTargetPath[scanIndex].has_value());

			assert(srcDirectories[scanIndex].empty());
			assert(refDirectories[scanIndex].empty());
			assert(dstDirectories[scanIndex].empty());

			assert(srcFiles[scanIndex].empty());
			assert(refFiles[scanIndex].empty());
			assert(dstFiles[scanIndex].empty());

			idx[scanIndex] = index;
			const Match& match = directories[index];

			if (match.src.has_value()) {
				assert(optionalSrc.has_value());
				srcPath[scanIndex] = *optionalSrc / match.src->GetName();
				dstTargetPath[scanIndex] = dst / match.src->GetName();
				if (match.src->GetAttributes() & BackupStrategy::kUnsupportedAttributesMask) {
					THROW(std::exception(), "Directory has unsupported attributes {}: {}", match.src->GetAttributes(), *srcPath[scanIndex]);
				}

				srcDirectories[scanIndex].reserve(kReserveDirectories);
				srcFiles[scanIndex].reserve(kReserveFiles);
				m_strategy.Scan(*srcPath[scanIndex], m_srcScanner, srcDirectories[scanIndex], srcFiles[scanIndex],
								DirectoryScanner::Flags::kFolderSecurity | (m_fileSecurity ? DirectoryScanner::Flags::kFileSecurity : DirectoryScanner::Flags::kDefault) | DirectoryScanner::Flags::kFolderStreams, kAcceptAllScannerFilter);
			}
			if (match.ref.has_value()) {
				assert(optionalRef.has_value());
				refPath[scanIndex] = *optionalRef / match.ref->GetName();
				if (match.ref->GetAttributes() & BackupStrategy::kUnsupportedAttributesMask) {
					THROW(std::exception(), "Directory has unsupported attributes {}: {}", match.ref->GetAttributes(), *refPath[scanIndex]);
				}

				refDirectories[scanIndex].reserve(kReserveDirectories);
				refFiles[scanIndex].reserve(kReserveFiles);
				m_strategy.Scan(*refPath[scanIndex], m_refScanner, refDirectories[scanIndex], refFiles[scanIndex],
								m_fileSecurity ? DirectoryScanner::Flags::kFileSecurity : DirectoryScanner::Flags::kDefault, kAcceptAllScannerFilter);
			}
			if (match.dst.has_value()) {
				dstPath[scanIndex] = dst / match.dst->GetName();
				if (match.dst->GetAttributes() & BackupStrategy::kUnsupportedAttributesMask) {
					THROW(std::exception(), "Directory has unsupported attributes {}: {}", match.dst->GetAttributes(), *dstPath[scanIndex]);
				}

				dstDirectories[scanIndex].reserve(kReserveDirectories);
				dstFiles[scanIndex].reserve(kReserveFiles);
				m_strategy.Scan(*dstPath[scanIndex], m_dstScanner, dstDirectories[scanIndex], dstFiles[scanIndex],
								DirectoryScanner::Flags::kFolderSecurity | (m_fileSecurity ? DirectoryScanner::Flags::kFileSecurity : DirectoryScanner::Flags::kDefault), kAcceptAllScannerFilter);
			}
		}

		// wait and continue for first iteration
		if (!index) {
			m_strategy.WaitForScan(m_srcScanner);
			m_strategy.WaitForScan(m_refScanner);
			m_strategy.WaitForScan(m_dstScanner);
			continue;
		}

		// Calculate delta while next entries are scanned
		const std::uint_fast8_t readIndex = (index & 1) ^ 1;  // == (index - 1) & 1;

		std::vector<Match> copyDirectories;
		std::vector<Match> extraDirectories;
		// Heuristics for sizing the lists
		copyDirectories.reserve(srcDirectories[readIndex].size());
		extraDirectories.reserve(MaxOfDifferenceAndZero(dstDirectories[readIndex].size(), srcDirectories[readIndex].size()));

		ThreeWayMerge(srcDirectories[readIndex], refDirectories[readIndex], dstDirectories[readIndex], copyDirectories, extraDirectories, CompareName);

		srcDirectories[readIndex].clear();
		refDirectories[readIndex].clear();
		dstDirectories[readIndex].clear();
		// shrink structures if they got too big
		if (srcDirectories[readIndex].capacity() > kReserveDirectories * 2) {
			srcDirectories[readIndex].shrink_to_fit();
		}
		if (refDirectories[readIndex].capacity() > kReserveDirectories * 2) {
			refDirectories[readIndex].shrink_to_fit();
		}
		if (dstDirectories[readIndex].capacity() > kReserveDirectories * 2) {
			dstDirectories[readIndex].shrink_to_fit();
		}

		std::vector<Match> copyFiles;
		std::vector<Match> extraFiles;
		// Heuristics for sizing the lists
		copyFiles.reserve(srcFiles[readIndex].size());
		extraFiles.reserve(MaxOfDifferenceAndZero(dstFiles[readIndex].size(), srcFiles[readIndex].size()));

		ThreeWayMerge(srcFiles[readIndex], refFiles[readIndex], dstFiles[readIndex], copyFiles, extraFiles, CompareName);

		srcFiles[readIndex].clear();
		refFiles[readIndex].clear();
		dstFiles[readIndex].clear();
		// shrink structures if they got too big
		if (srcFiles[readIndex].capacity() > kReserveFiles * 2) {
			srcFiles[readIndex].shrink_to_fit();
		}
		if (refFiles[readIndex].capacity() > kReserveFiles * 2) {
			refFiles[readIndex].shrink_to_fit();
		}
		if (dstFiles[readIndex].capacity() > kReserveFiles * 2) {
			dstFiles[readIndex].shrink_to_fit();
		}

		// wait for disk activity to end
		m_strategy.WaitForScan(m_srcScanner);
		m_strategy.WaitForScan(m_refScanner);
		m_strategy.WaitForScan(m_dstScanner);

		assert(idx[readIndex].has_value());
		const Match& match = directories[*idx[readIndex]];
		assert(srcPath[readIndex].has_value() == match.src.has_value());
		assert(refPath[readIndex].has_value() == match.ref.has_value());
		assert(dstPath[readIndex].has_value() == match.dst.has_value());
		assert(dstTargetPath[readIndex].has_value() == srcPath[readIndex].has_value());

		// remove stale entries from destination
		if (match.dst.has_value()) {
			for (const Match& extraFile : extraFiles) {
				const Path dstFile = *dstPath[readIndex] / extraFile.dst->GetName();
				LOG_DEBUG("Delete file {}", dstFile);
				m_strategy.Delete(dstFile);
				m_statistics.OnRemove(extraFile);
			}
			extraFiles.clear();
			extraFiles.shrink_to_fit();  // reclaim memory

			if (!extraDirectories.empty()) {
				CopyDirectories(std::nullopt, std::nullopt, *dstPath[readIndex], extraDirectories);
				extraDirectories.clear();
			}
			extraDirectories.shrink_to_fit();  // reclaim memory

			if (!match.src.has_value()) {
				// DeleteDirectory
				LOG_DEBUG("Remove directory {}", *dstPath[readIndex]);
				m_strategy.Delete(*dstPath[readIndex]);
				m_statistics.OnRemove(match);
			}
		}

		if (match.src.has_value()) {
			if (!match.src->GetStreams().empty()) {
				// not yet implemented
				THROW(std::domain_error("streams for directories are not (yet) supported"));
			}

			if (!match.dst.has_value()) {
				// CreateDirectory
				LOG_DEBUG("Create directory {}", *dstTargetPath[readIndex]);
				m_strategy.CreateDirectory(*dstTargetPath[readIndex], *srcPath[readIndex], *match.src);
				m_statistics.OnAdd(match);
			} else {
				if (!srcPath[readIndex]->GetFilename().IsSameStringAs(dstPath[readIndex]->GetFilename())) {
					assert(srcPath[readIndex]->GetFilename() == dstPath[readIndex]->GetFilename());
					LOG_DEBUG("Rename directory {} to {}", *dstPath[readIndex], *dstTargetPath[readIndex]);
					m_strategy.Rename(*dstPath[readIndex], *dstTargetPath[readIndex]);
					m_statistics.OnUpdate(match);
				} else if (!SameAttributes(*match.src, *match.dst)) {
					// distinguish changes in source data from technical changes because of copying files
					m_statistics.OnUpdate(match);
					// attributes will be set after all files have been written
				} else {
					m_statistics.OnRetain(match);
				}

				// adjust security if required
				if (!SameSecurity(*match.src, *match.dst)) {
					LOG_DEBUG("Update security of {}", *dstTargetPath[readIndex]);
					m_strategy.SetSecurity(*dstTargetPath[readIndex], *match.src);
					m_statistics.OnSecurityUpdate(match);
				}
			}
		}

		// compare and copy files
		for (const Match& matchedFile : copyFiles) {
			assert(match.src.has_value());
			assert(matchedFile.src.has_value());

			const Path srcFile = *srcPath[readIndex] / matchedFile.src->GetName();
			if (matchedFile.src->GetAttributes() & BackupStrategy::kUnsupportedAttributesMask) {
				THROW(std::exception(), "File has unsupported attributes {}: {}", matchedFile.src->GetAttributes(), srcFile);
			}

			const Path dstTargetFile = *dstTargetPath[readIndex] / matchedFile.src->GetName();

			// check if dst is the same as src
			if (matchedFile.dst.has_value()) {
				const Path dstFile = *dstPath[readIndex] / matchedFile.dst->GetName();

				if (!SameAttributes(*matchedFile.src, *matchedFile.dst)) {
					// remove dst if it has changed attributes
					LOG_DEBUG("File has changed, removing {}", dstFile);
				} else if (matchedFile.dst->GetAttributes() & BackupStrategy::kUnsupportedAttributesMask) {
					// remove dst if it has unsupported attributes
					LOG_DEBUG("File has unsupported attributes {}, removing: {}", matchedFile.dst->GetAttributes(), dstFile);
				} else {
					// check if security must be updated
					const bool differentSecurity = m_fileSecurity && !SameSecurity(*matchedFile.src, *matchedFile.dst);
					if (differentSecurity && matchedFile.dst->IsHardLink(*matchedFile.ref)) {
						// file is hard link, so changing security would modify copy in ref -> delete and create new
						goto dstDifferent;
					}

					if (m_compareContents) {
						// compare contents of src and dst
						LOG_DEBUG("Compare files {} and {}", srcFile, dstFile);
						if (!m_strategy.Compare(srcFile, dstFile, m_fileComparer)) {
							goto dstDifferent;
						}
						const std::vector<ScannedFile::Stream>& srcStreams = matchedFile.src->GetStreams();
						const std::vector<ScannedFile::Stream>& dstStreams = matchedFile.dst->GetStreams();
						assert(srcStreams.size() == dstStreams.size());

						for (std::size_t i = 0, max = srcStreams.size(); i < max; ++i) {
							assert(srcStreams[i].GetName() == dstStreams[i].GetName());
							const Path srcStreamName = srcFile + srcStreams[i].GetName();
							const Path dstStreamName = dstFile + dstStreams[i].GetName();

							LOG_DEBUG("Compare streams {} and {}", srcStreamName, dstStreamName);
							if (!m_strategy.Compare(srcStreamName, dstStreamName, m_fileComparer)) {
								goto dstDifferent;
							}
						}
					}

					if (matchedFile.src->GetName().IsSameStringAs(matchedFile.dst->GetName())) {
						m_statistics.OnRetain(matchedFile);
					} else {
						assert(matchedFile.src->GetName() == matchedFile.dst->GetName());
						// change case
						LOG_DEBUG("Rename {} to {}", dstFile, dstTargetFile);
						m_strategy.Rename(dstFile, dstTargetFile);
						m_statistics.OnUpdate(matchedFile);
					}

					// adjust security if required
					if (differentSecurity) {
						LOG_DEBUG("Update security of {}", dstTargetFile);
						m_strategy.SetSecurity(dstTargetFile, *matchedFile.src);
						m_statistics.OnSecurityUpdate(matchedFile);
					}

					continue;

				dstDifferent:
					// delete outdated copy in target
					LOG_DEBUG("Delete file for replacement {}", dstFile);
				}
				m_strategy.Delete(dstFile);
				m_statistics.OnReplace(matchedFile);
			} else {
				m_statistics.OnAdd(matchedFile);
			}

			// check if ref is the same as src (if not same hard-link as dst)
			if (matchedFile.ref.has_value() && SameAttributes(*matchedFile.src, *matchedFile.ref) && !(matchedFile.dst.has_value() && matchedFile.ref->IsHardLink(*matchedFile.dst)) && (!m_fileSecurity || SameSecurity(*matchedFile.src, *matchedFile.ref))) {
				const Path refFile = *refPath[readIndex] / matchedFile.ref->GetName();

				if (m_compareContents) {
					// compare contents of src and ref
					LOG_DEBUG("Compare files {} and {}", srcFile, refFile);
					if (!m_strategy.Compare(srcFile, refFile, m_fileComparer)) {
						goto refDifferent;
					}
				}
				assert(matchedFile.src->GetSize() == matchedFile.ref->GetSize());
				// if same create hard link for ref in dst and continue
				LOG_DEBUG("Create link from {} to {}", refFile, dstTargetFile);
				m_strategy.CreateHardLink(dstTargetFile, refFile);
				m_statistics.OnHardLink(matchedFile.src->GetSize());
				continue;
			}
		refDifferent:

			// copy src to dst
			LOG_DEBUG("Copy file {} to {}", srcFile, dstTargetFile);
			m_strategy.Copy(srcFile, dstTargetFile);
			// copying does copy attributes and security, however we want the original file times
			m_strategy.SetAttributes(dstTargetFile, *matchedFile.src);
			m_statistics.OnCopy(matchedFile.src->GetSize());
		}
		copyFiles.clear();
		copyFiles.shrink_to_fit();

		if (match.src.has_value()) {
			if (!copyDirectories.empty()) {
				CopyDirectories(srcPath[readIndex], refPath[readIndex], *dstTargetPath[readIndex], copyDirectories);
			}

			// UpdateDirectoryAttributes (after any copy operations might have modified the timestamps)
			m_strategy.SetAttributes(*dstTargetPath[readIndex], *match.src);
		}

		// mark data as "processed"
		idx[readIndex].reset();
		srcPath[readIndex].reset();
		refPath[readIndex].reset();
		dstPath[readIndex].reset();
		dstTargetPath[readIndex].reset();
	}
}

}  // namespace systools
