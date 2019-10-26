#include "systools/Backup.h"

#include "systools/DirectoryScanner.h"
#include "systools/Path.h"
#include "systools/ThreeWayMerge.h"

#include <llamalog/llamalog.h>
#include <m3c/com_ptr.h>
#include <m3c/exception.h>

#include <shellapi.h>
#include <shobjidl.h>
#include <windows.h>

#include <algorithm>
#include <cassert>
#include <compare>
#include <optional>
#include <string>
#include <vector>

namespace systools {

struct Backup::Match {
	Match(std::optional<DirectoryScanner::Entry> src, std::optional<DirectoryScanner::Entry> ref, std::optional<DirectoryScanner::Entry> dst) noexcept
		: src(std::move(src))
		, ref(std::move(ref))
		, dst(std::move(dst)) {
		assert(!src.has_value() || !ref.has_value() || src->name == ref->name);
		assert(!src.has_value() || !dst.has_value() || src->name == dst->name);
		assert(!ref.has_value() || !dst.has_value() || ref->name == dst->name);
	}
	Match(const Match& match) = default;
	Match(Match&& match) noexcept = default;
	~Match() noexcept = default;

	Match& operator=(const Match& match) = default;
	Match& operator=(Match&& match) noexcept = default;

	std::optional<DirectoryScanner::Entry> src;
	std::optional<DirectoryScanner::Entry> ref;
	std::optional<DirectoryScanner::Entry> dst;
};

namespace {

constexpr DWORD kCopyAttributeMask = FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_COMPRESSED | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED | FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM;
constexpr DWORD kUnsupportedAttributesMask = FILE_ATTRIBUTE_ENCRYPTED | FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_SPARSE_FILE;

void EnsurePathExists(const Path& path) {
	if (path.Exists()) {
		return;
	}
	EnsurePathExists(path.GetParent());
	if (!CreateDirectoryW(path.c_str(), nullptr)) {
		THROW(m3c::windows_exception(GetLastError()), "CreateDirectoryW", path);
	}
}

std::optional<DirectoryScanner::Entry> CreateEntry(const Path& path) {
	std::optional<DirectoryScanner::Entry> result;

	const m3c::Handle hFile = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY | FILE_READ_DATA | FILE_READ_EA, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (!hFile) {
		if (const DWORD lastError = GetLastError(); lastError != ERROR_FILE_NOT_FOUND && lastError != ERROR_PATH_NOT_FOUND) {
			THROW(m3c::windows_exception(lastError), "CreateFile {}", path);
		}
		return result;
	}
	FILE_STANDARD_INFO fileStandardInfo;
	if (!GetFileInformationByHandleEx(hFile, FileStandardInfo, &fileStandardInfo, sizeof(fileStandardInfo))) {
		THROW(m3c::windows_exception(GetLastError()), "GetFileInformationByHandleEx {}", path);
	}
	if (!fileStandardInfo.Directory) {
		THROW(std::exception(), "{} is not a directory", path);
	}

	FILE_BASIC_INFO fileBasicInfo;
	if (!GetFileInformationByHandleEx(hFile, FileBasicInfo, &fileBasicInfo, sizeof(fileBasicInfo))) {
		THROW(m3c::windows_exception(GetLastError()), "GetFileInformationByHandleEx {}", path);
	}
	if (fileBasicInfo.FileAttributes & kUnsupportedAttributesMask) {
		THROW(std::exception(), "directory has unsupported attributes {}: {}", fileBasicInfo.FileAttributes, path);
	}

	FILE_ID_INFO fileIdInfo;
	if (!GetFileInformationByHandleEx(hFile, FileIdInfo, &fileIdInfo, sizeof(fileIdInfo))) {
		THROW(m3c::windows_exception(GetLastError()), "GetFileInformationByHandleEx {}", path);
	}

	// get the file name to have proper capitalization
	std::wstring name;
	const DWORD size = GetFinalPathNameByHandleW(hFile, name.data(), 0, FILE_NAME_NORMALIZED | VOLUME_NAME_NONE);
	if (!size) {
		THROW(m3c::windows_exception(GetLastError()), "GetFinalPathNameByHandle {}", path);
	}
	name.resize(size);
	const DWORD len = GetFinalPathNameByHandleW(hFile, name.data(), size, FILE_NAME_NORMALIZED | VOLUME_NAME_NONE);
	if (!len) {
		THROW(m3c::windows_exception(GetLastError()), "GetFinalPathNameByHandle {}", path);
	}
	if (len != size - 1) {
		THROW(m3c::windows_exception(GetLastError()), "GetFinalPathNameByHandle {}", path);
	}
	name.resize(len);

	const std::size_t pos = name.rfind('\\', len);
	if (pos == std::wstring::npos || pos >= name.size() - 1) {
		THROW(std::exception(), "invalid path for {}: {}", path, name);
	}
	const Filename filename(name.substr(name.rfind('\\') + 1));
	result.emplace(filename, fileStandardInfo.EndOfFile, fileBasicInfo.CreationTime, fileBasicInfo.LastWriteTime, fileBasicInfo.FileAttributes, fileIdInfo.FileId);
	return result;
}

int CompareDirectoryEntries(const DirectoryScanner::Entry& e0, const DirectoryScanner::Entry& e1) {
	// attributes are always updated for directories
	return e0.name.CompareTo(e1.name);
};

int CompareFileEntries(const DirectoryScanner::Entry& e0, const DirectoryScanner::Entry& e1) {
	// first check if same file
	if (const int cmp = e0.name.CompareTo(e1.name); cmp) {
		return cmp;
	}

	// then check values first that change when file is modified
	if (e0.lastWriteTime < e1.lastWriteTime) {
		return -1;
	}
	if (e0.lastWriteTime > e1.lastWriteTime) {
		return 1;
	}
	if (e0.size < e1.size) {
		return -1;
	}
	if (e0.size > e1.size) {
		return 1;
	}
	if ((e0.attributes & kCopyAttributeMask) < (e1.attributes & kCopyAttributeMask)) {
		return -1;
	}
	if ((e0.attributes & kCopyAttributeMask) > (e1.attributes & kCopyAttributeMask)) {
		return 1;
	}
	// check creation time last because it rarely changes
	if (e0.creationTime < e1.creationTime) {
		return -1;
	}
	if (e0.creationTime > e1.creationTime) {
		return 1;
	}
	return 0;
};

void Rename(const Path& existingName, const Path& newName) {
	if (!MoveFileExW(existingName.c_str(), newName.c_str(), 0)) {
		if (const DWORD lastError = GetLastError(); lastError != ERROR_ACCESS_DENIED) {
			THROW(m3c::windows_exception(lastError), "MoveFileEx {} to {}", existingName, newName);
		}
		m3c::com_ptr<IShellItem> item;
#pragma comment(lib, "shell32.lib")
		COM_HR(SHCreateItemFromParsingName(existingName.c_str(), nullptr, __uuidof(IShellItem), (void**) &item), "SHCreateItemFromParsingName {}", existingName);
		m3c::com_ptr<IFileOperation> fo;
		COM_HR(CoCreateInstance(__uuidof(FileOperation), nullptr, CLSCTX_ALL, __uuidof(IFileOperation), (void**) &fo), "CoCreateInstance");
		COM_HR(fo->SetOperationFlags(FOF_NO_UI), "SetOperationFlags");
		const Filename filename = newName.GetFilename();
		COM_HR(fo->RenameItem(item.get(), filename.c_str(), nullptr), "RenameItem {} to {}", existingName, filename);
		COM_HR(fo->PerformOperations(), "PerformOperations");
	}
}

void CopyAttributesAndTimestamps(const DirectoryScanner::Entry& src, const Path& dst) {
	const m3c::Handle hDst = CreateFileW(dst.c_str(), FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (!hDst) {
		THROW(m3c::windows_exception(GetLastError()), "CreateFile {}", dst);
	}

	FILE_BASIC_INFO fileBasicInfo;
	if (!GetFileInformationByHandleEx(hDst, FileBasicInfo, &fileBasicInfo, sizeof(fileBasicInfo))) {
		THROW(m3c::windows_exception(GetLastError()), "GetFileInformationByHandleEx {}", dst);
	}

	if (fileBasicInfo.LastWriteTime.QuadPart == src.lastWriteTime && fileBasicInfo.CreationTime.QuadPart == src.creationTime && (fileBasicInfo.FileAttributes & kCopyAttributeMask) == (src.attributes & kCopyAttributeMask)) {
		return;
	}

	LOG_DEBUG("Copy attributes and timestamp from source to {}", dst);

	// set any missing attributes
	fileBasicInfo.FileAttributes |= (src.attributes & kCopyAttributeMask);
	// remove unset attributes, but retain attributes which are not copied
	fileBasicInfo.FileAttributes &= (src.attributes | ~kCopyAttributeMask);

	fileBasicInfo.CreationTime.QuadPart = src.creationTime;
	fileBasicInfo.LastWriteTime.QuadPart = src.lastWriteTime;

	if (!SetFileInformationByHandle(hDst, FileBasicInfo, &fileBasicInfo, sizeof(fileBasicInfo))) {
		THROW(m3c::windows_exception(GetLastError()), "SetFileInformationByHandle {}", dst);
	}
}

}  // namespace

void Backup::CompareDirectories(const std::vector<Path>& src, const Path& ref, const Path& dst) {
	for (std::size_t index = 0, max = src.size(); index < max; ++index) {
		if (src[index].Exists() && !src[index].IsDirectory()) {
			THROW(std::exception(), "{} is not a directory", src[index]);
		}
	}
	if (ref.Exists() && !ref.IsDirectory()) {
		THROW(std::exception(), "{} is not a directory", ref);
	}
	if (dst.Exists() && !dst.IsDirectory()) {
		THROW(std::exception(), "{} is not a directory", dst);
	}

	EnsurePathExists(dst);

	for (std::size_t index = 0, max = src.size(); index < max; ++index) {
		const Path srcPath = src[index].GetParent();

		std::optional<DirectoryScanner::Entry> srcEntry = CreateEntry(src[index]);
		std::optional<DirectoryScanner::Entry> refEntry = CreateEntry(ref / srcEntry->name);
		std::optional<DirectoryScanner::Entry> dstEntry = CreateEntry(dst / srcEntry->name);

		const Match match = {std::move(srcEntry), std::move(refEntry), std::move(dstEntry)};
		CompareDirectories(srcPath, ref, dst, {match});
	}
}

void Backup::CompareDirectories(const std::optional<Path>& optionalSrc, const std::optional<Path>& optionalRef, const Path& dst, const std::vector<Match>& directories) {
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

	// loop requires one iteration more then directories.size()!
	for (std::size_t index = 0, max = directories.size(); index <= max; ++index) {
		// scan next directory
		if (index < max) {
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
				srcPath[scanIndex] = *optionalSrc / match.src->name;
				dstTargetPath[scanIndex] = dst / match.src->name;
				if (match.src->attributes & kUnsupportedAttributesMask) {
					THROW(std::exception(), "Directory has unsupported attributes {}: {}", match.src->attributes, *srcPath[scanIndex]);
				}
				m_srcScanner.Scan(*srcPath[scanIndex], srcDirectories[scanIndex], srcFiles[scanIndex]);
			}
			if (match.ref.has_value()) {
				assert(optionalRef.has_value());
				refPath[scanIndex] = *optionalRef / match.ref->name;
				if (match.ref->attributes & kUnsupportedAttributesMask) {
					THROW(std::exception(), "Directory has unsupported attributes {}: {}", match.ref->attributes, *refPath[scanIndex]);
				}
				m_refScanner.Scan(*refPath[scanIndex], refDirectories[scanIndex], refFiles[scanIndex]);
			}
			if (match.dst.has_value()) {
				dstPath[scanIndex] = dst / match.dst->name;
				if (match.dst->attributes & kUnsupportedAttributesMask) {
					THROW(std::exception(), "Directory has unsupported attributes {}: {}", match.dst->attributes, *dstPath[scanIndex]);
				}
				m_dstScanner.Scan(*dstPath[scanIndex], dstDirectories[scanIndex], dstFiles[scanIndex]);
			}
		}

		// wait and continue for first iteration
		if (!index) {
			m_srcScanner.Wait();
			m_refScanner.Wait();
			m_dstScanner.Wait();
			continue;
		}

		// Calculate delta while next entries are scanned
		const std::uint_fast8_t readIndex = (index & 1) ^ 1;  // == (index - 1) & 1;

		std::vector<Match> copyDirectories;
		std::vector<Match> staleDirectories;
		ThreeWayMerge(srcDirectories[readIndex], refDirectories[readIndex], dstDirectories[readIndex], copyDirectories, staleDirectories, CompareDirectoryEntries);

		srcDirectories[readIndex].clear();
		refDirectories[readIndex].clear();
		dstDirectories[readIndex].clear();

		std::vector<Match> copyFiles;
		std::vector<Match> staleFiles;
		ThreeWayMerge(srcFiles[readIndex], refFiles[readIndex], dstFiles[readIndex], copyFiles, staleFiles, CompareFileEntries);

		srcFiles[readIndex].clear();
		refFiles[readIndex].clear();
		dstFiles[readIndex].clear();

		// wait for disk activity to end
		m_srcScanner.Wait();
		m_refScanner.Wait();
		m_dstScanner.Wait();

		assert(idx[readIndex].has_value());
		const Match& match = directories[*idx[readIndex]];
		assert(srcPath[readIndex].has_value() == match.src.has_value());
		assert(refPath[readIndex].has_value() == match.ref.has_value());
		assert(dstPath[readIndex].has_value() == match.dst.has_value());
		assert(dstTargetPath[readIndex].has_value() == srcPath[readIndex].has_value());

		// remove stale entries from destination
		if (match.dst.has_value()) {
			for (const Match& staleFile : staleFiles) {
				const Path dstFile = *dstPath[readIndex] / staleFile.dst->name;
				LOG_DEBUG("Delete file {}", dstFile);
				dstFile.ForceDelete();
			}
			staleFiles.clear();

			CompareDirectories(std::nullopt, std::nullopt, *dstPath[readIndex], staleDirectories);
			staleDirectories.clear();

			if (!match.src.has_value()) {
				// DeleteDirectory
				LOG_DEBUG("Remove directory {}", *dstPath[readIndex]);
				dstPath[readIndex]->ForceDelete();
			}
		}

		if (match.src.has_value()) {
			if (!match.dst.has_value()) {
				// CreateDirectory
				LOG_DEBUG("Create directory {}", *dstTargetPath[readIndex]);
				if (!CreateDirectoryExW(srcPath[readIndex]->c_str(), dstTargetPath[readIndex]->c_str(), nullptr)) {
					THROW(m3c::windows_exception(GetLastError()), "CreateDirectoryEx {}", *dstTargetPath[readIndex]);
				}
			} else if (!srcPath[readIndex]->GetFilename().IsSameStringAs(dstPath[readIndex]->GetFilename())) {
				assert(srcPath[readIndex]->GetFilename() == dstPath[readIndex]->GetFilename());
				LOG_DEBUG("Rename directory {} to {}", *dstPath[readIndex], *dstTargetPath[readIndex]);
				Rename(*dstPath[readIndex], *dstTargetPath[readIndex]);
			}
		}

		// compare and copy files
		for (const Match& matchedFile : copyFiles) {
			assert(match.src.has_value());
			assert(matchedFile.src.has_value());

			const Path srcFile = *srcPath[readIndex] / matchedFile.src->name;
			if (matchedFile.src->attributes & kUnsupportedAttributesMask) {
				THROW(std::exception(), "File has unsupported attributes {}: {}", matchedFile.src->attributes, srcFile);
			}

			const Path dstTargetFile = *dstTargetPath[readIndex] / matchedFile.src->name;

			// check if dst is the same as src
			if (matchedFile.dst.has_value()) {
				assert(match.dst.has_value());
				const Path dstFile = *dstPath[readIndex] / matchedFile.dst->name;

				if (matchedFile.dst->attributes & kUnsupportedAttributesMask) {
					// remove dst if it has unsupported attributes
					LOG_DEBUG("File has unsupported attributes {}: {}", matchedFile.dst->attributes, dstFile);
				} else {
					// compare src and dst
					LOG_DEBUG("Compare files {} and {}", srcFile, dstFile);
					// TODO
					const bool same = true;
					if (same) {
						if (!matchedFile.src->name.IsSameStringAs(matchedFile.dst->name)) {
							assert(matchedFile.src->name == matchedFile.dst->name);
							// change case
							LOG_DEBUG("Rename {} to {}", dstFile, dstTargetFile);
							Rename(dstFile, dstTargetFile);
						}
						continue;
					}
					// delete outdated copy in target
					LOG_DEBUG("Delete file for replacement {}", dstFile);
				}
				dstFile.ForceDelete();
			}

			// check if ref is the same as src (if not same hard-link as dst)
			if (matchedFile.ref.has_value() && !(matchedFile.dst.has_value() && std::memcmp(&matchedFile.ref->fileId, &matchedFile.dst->fileId, sizeof(matchedFile.ref->fileId)) == 0)) {
				const Path refFile = *refPath[readIndex] / matchedFile.ref->name;

				// compare src and ref
				LOG_DEBUG("Compare files {} and {}", srcFile, refFile);
				const bool same = true;
				if (same) {
					// if same create hard link for ref in dst and continue
					LOG_DEBUG("Create link from {} to {}", refFile, dstTargetFile);
					if (!CreateHardLinkW(dstTargetFile.c_str(), refFile.c_str(), nullptr)) {
						THROW(m3c::windows_exception(GetLastError()), "CreateHardLink {} to {}", refFile, dstTargetFile);
					}
					continue;
				}
			}

			// copy src to dst
			LOG_DEBUG("Copy file {} to {}", srcFile, dstTargetFile);

			BOOL cancel = FALSE;
			COPYFILE2_EXTENDED_PARAMETERS params = {sizeof(params), COPY_FILE_FAIL_IF_EXISTS | COPY_FILE_NO_BUFFERING, &cancel, nullptr, nullptr};
			COM_HR(CopyFile2(srcFile.c_str(), dstTargetFile.c_str(), &params), "CopyFile2 {} to {}", srcFile, dstTargetFile);
			// TODO
			CopyAttributesAndTimestamps(*match.src, dstTargetFile);
		}
		copyFiles.clear();

		if (match.src.has_value()) {
			CompareDirectories(srcPath[readIndex], refPath[readIndex], *dstTargetPath[readIndex], copyDirectories);

			// UpdateDirectoryAttributes (after any copy operations might have modified the timestamps)
			CopyAttributesAndTimestamps(*match.src, *dstTargetPath[readIndex]);
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
#if 0
std::wstring MakeSystemPath(const std::wstring& basePath, const std::wstring& relativePath) {
	return L"\\\\?\\" + basePath + (relativePath.empty ? L"" : (L"\\" + relativePath));
}

void OverlappedCompletion(DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered, LPOVERLAPPED lpOverlapped) {
	// no op
}

bool IsSameContent(const std::wstring& sourceFile, const std::wstring& previousFile) {
	// get physical sector size using  IOCTL_STORAGE_QUERY_PROPERTY  with STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR

	HANDLE hSource = CreateFileW(sourceFile.c_str(), FILE_READ_DATA, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED, nullptr);
	if (hSource == INVALID_HANDLE_VALUE) {
	}
	HANDLE hPrevious = CreateFileW(sourceFile.c_str(), FILE_READ_DATA, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED, nullptr);
	if (hPrevious == INVALID_HANDLE_VALUE) {
	}

	std::byte sourceBuffer[4096];    // -> align
	std::byte previousBuffer[4096];  // -> align

	OVERLAPPED sourceOverlapped = {};
	sourceOverlapped.Offset = 0;
	sourceOverlapped.OffsetHigh = 0;
	ReadFileEx(hSource, sourceBuffer, size /* align */, &sourceOverlapped, OverlappedCompletion);

	OVERLAPPED previousOverlapped = {};
	previousOverlapped.Offset = 0;
	previousOverlapped.OffsetHigh = 0;
	ReadFileEx(hPrevious, previousBuffer, size /* align */, &previousOverlapped, OverlappedCompletion);

	DWORD sourceBytesRead;
	BOOL result = GetOverlappedResultEx(hSource, &sourceOverlapped, &sourceBytesRead, INFINITE, TRUE);

	DWORD previousBytesRead;
	BOOL result = GetOverlappedResultEx(hPrevious, &previousOverlapped, &previousBytesRead, INFINITE, TRUE);
}

void CopyToTarget(const std::wstring& sourceDirectory, const std::wstring& targetDirectory, const std::wstring& previousDirectory, const std::wstring& relativePath) {
	const std::wstring source = MakeSystemPath(sourceDirectory, relativePath);
	const std::wstring target = MakeSystemPath(targetDirectory, relativePath);
	const std::wstring previous = MakeSystemPath(previousDirectory, relativePath);

	const wchar_t* const wszSource = source.c_str();
	const wchar_t* const wszTarget = target.c_str();

	// create target directory if it does not exist
	CreateDirectoryExW(wszSource, wszTarget, nullptr);

	// set attributes for target directory

	// get all directories and files in source directory
	const std::wstring pattern = source + L"\\*";
	WIN32_FIND_DATA findData;
	HANDLE hFindFile = FindFirstFileExW(pattern.c_str(), FINDEX_INFO_LEVELS::FindExInfoBasic, &findData, FINDEX_SEARCH_OPS::FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);

	if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
		// for each directory: recurse
		CopyToTarget(sourceDirectory, targetDirectory, previousDirectory, relativePath + L"\\" + findData.cFileName);
	} else {
		// check file in previous directory
		WIN32_FILE_ATTRIBUTE_DATA previousAttributes;
		const std::wstring sourceFile = source + L"\\" + findData.cFileName;
		const std::wstring targetFile = target + L"\\" + findData.cFileName;
		const std::wstring previousFile = previous + L"\\" + findData.cFileName;
		const BOOL result = GetFileAttributesExW(previousFile.c_str(), GET_FILEEX_INFO_LEVELS::GetFileExInfoStandard, &previousAttributes);
		// if file exists, compare size, last write time, attributes and contents
		if (result
			&& findData.nFileSizeLow == previousAttributes.nFileSizeLow && findData.nFileSizeHigh == previousAttributes.nFileSizeHigh
			&& CompareFileTime(&findData.ftLastWriteTime, &previousAttributes.ftLastWriteTime) == 0
			&& findData.dwFileAttributes == previousAttributes.dwFileAttributes
			&& IsSameContent(sourceFile, previousFile)) {
			// if same, create a hard link
			const BOOL result = CreateHardLinkW(targetFile.c_str(), previousFile.c_str(), nullptr);
		} else {
			//
			// SymLink?
			// if not same, copy
			BOOL cancel;
			COPYFILE2_EXTENDED_PARAMETERS extendedParameters = {};
			extendedParameters.dwSize = sizeof(extendedParameters);
			extendedParameters.dwCopyFlags = COPY_FILE_COPY_SYMLINK | COPY_FILE_FAIL_IF_EXISTS | COPY_FILE_NO_BUFFERING;
			extendedParameters.pfCancel = &cancel;
			extendedParameters.pProgressRoutine = nullptr;
			extendedParameters.pvCallbackContext = nullptr;
			const HRESULT result = CopyFile2(sourceFile.c_str(), targetFile.c_str(), &extendedParameters);
		}
		FindNextFileW(hFindFile, &findData);
	}
}

int main() {
	//CopyToTarget()
	return 0;
}

#endif
