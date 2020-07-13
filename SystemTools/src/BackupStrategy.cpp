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

#include "systools/BackupStrategy.h"

#include "systools/DirectoryScanner.h"
#include "systools/FileComparer.h"
#include "systools/Path.h"

#include <llamalog/llamalog.h>
#include <m3c/Handle.h>
#include <m3c/com_ptr.h>
#include <m3c/exception.h>
#include <m3c/lazy_string.h>

#include <accctrl.h>
#include <aclapi.h>
#include <objbase.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <memory>

namespace systools {

//
// BaseBackupStrategy
//

bool BaseBackupStrategy::Exists(const Path& path) const {
	return path.Exists();
}

bool BaseBackupStrategy::IsDirectory(const Path& path) const {
	return path.IsDirectory();
}

bool BaseBackupStrategy::Compare(const Path& src, const Path& target, FileComparer& fileComparer) const {
	return fileComparer.Compare(src, target);
}

void BaseBackupStrategy::Scan(const Path& path, DirectoryScanner& scanner, std::vector<ScannedFile>& directories, std::vector<ScannedFile>& files, const DirectoryScanner::Flags flags, const ScannerFilter& filter) const {
	scanner.Scan(path, directories, files, flags, filter);
}

void BaseBackupStrategy::WaitForScan(DirectoryScanner& scanner) const {
	scanner.Wait();
}


//
// DryRunBackupStrategy
//

void DryRunBackupStrategy::CreateDirectory(const Path& /* path */, const Path& /* templatePath */, const ScannedFile& /* securitySource */) const {
}

void DryRunBackupStrategy::CreateDirectoryRecursive(const Path& /* path */) const {
}

void DryRunBackupStrategy::SetAttributes(const Path& /* path */, const ScannedFile& /* attributesSource */) const {
}

void DryRunBackupStrategy::SetSecurity(const Path& /* path */, const ScannedFile& /* securitySource */) const {
}

void DryRunBackupStrategy::Rename(const Path& /* existingName */, const Path& /* newName */) const {
}

void DryRunBackupStrategy::Copy(const Path& /* source */, const Path& /* target */) const {
}

void DryRunBackupStrategy::CreateHardLink(const Path& /* path */, const Path& /* existing */) const {
}

void DryRunBackupStrategy::Delete(const Path& /* path */) const {
}


//
// WritingBackupStrategy
//

void WritingBackupStrategy::CreateDirectory(const Path& path, const Path& templatePath, const ScannedFile& securitySource) const {
	SECURITY_ATTRIBUTES security = {.nLength = sizeof(SECURITY_ATTRIBUTES), .lpSecurityDescriptor = securitySource.GetSecurity().pSecurityDescriptor.get(), .bInheritHandle = FALSE};
	if (!CreateDirectoryExW(templatePath.c_str(), path.c_str(), &security)) {
		THROW(m3c::windows_exception(GetLastError()), "CreateDirectoryEx {}", path);
	}
}

void WritingBackupStrategy::CreateDirectoryRecursive(const Path& path) const {
	if (path.Exists()) {
		return;
	}
	CreateDirectoryRecursive(path.GetParent());
	if (!::CreateDirectoryW(path.c_str(), nullptr)) {
		THROW(m3c::windows_exception(GetLastError()), "CreateDirectoryW", path);
	}
}

void WritingBackupStrategy::SetAttributes(const Path& path, const ScannedFile& attributesSource) const {
	const m3c::Handle hDst = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (!hDst) {
		THROW(m3c::windows_exception(GetLastError()), "CreateFile {}", path);
	}

	FILE_BASIC_INFO fileBasicInfo;
	if (!GetFileInformationByHandleEx(hDst, FileBasicInfo, &fileBasicInfo, sizeof(fileBasicInfo))) {
		THROW(m3c::windows_exception(GetLastError()), "GetFileInformationByHandleEx {}", path);
	}

	if (fileBasicInfo.LastWriteTime.QuadPart == attributesSource.GetLastWriteTime() && fileBasicInfo.CreationTime.QuadPart == attributesSource.GetCreationTime() && (fileBasicInfo.FileAttributes & kCopyAttributeMask) == (attributesSource.GetAttributes() & kCopyAttributeMask)) {
		return;
	}

	LOG_DEBUG("Copy attributes and timestamp from source to {}", path);

	// set any missing attributes
	fileBasicInfo.FileAttributes |= (attributesSource.GetAttributes() & kCopyAttributeMask);
	// remove unset attributes, but retain attributes which are not copied
	fileBasicInfo.FileAttributes &= (attributesSource.GetAttributes() | ~kCopyAttributeMask);

	fileBasicInfo.CreationTime.QuadPart = attributesSource.GetCreationTime();
	fileBasicInfo.LastWriteTime.QuadPart = attributesSource.GetLastWriteTime();

	if (!SetFileInformationByHandle(hDst, FileBasicInfo, &fileBasicInfo, sizeof(fileBasicInfo))) {
		THROW(m3c::windows_exception(GetLastError()), "SetFileInformationByHandle {}", path);
	}
}

void WritingBackupStrategy::SetSecurity(const Path& path, const ScannedFile& securitySource) const {
	constexpr SECURITY_INFORMATION kSecurityInformation = ATTRIBUTE_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION | SACL_SECURITY_INFORMATION | SCOPE_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION | UNPROTECTED_SACL_SECURITY_INFORMATION;
	// for some reason, the first argument to SetNamedSecurityInfoW is _writable_
	m3c::lazy_wstring<128> writablePath(path.sv());
	const ScannedFile::Security& security = securitySource.GetSecurity();
	const DWORD result = SetNamedSecurityInfoW(writablePath.data(), SE_FILE_OBJECT, kSecurityInformation, security.pOwner, security.pGroup, security.pDacl, security.pSacl);
	if (result != ERROR_SUCCESS) {
		THROW(m3c::windows_exception(result), "SetNamedSecurityInfoW {}", path);
	}
}

void WritingBackupStrategy::Rename(const Path& existingName, const Path& newName) const {
	if (!MoveFileExW(existingName.c_str(), newName.c_str(), 0)) {
		if (const DWORD lastError = GetLastError(); lastError != ERROR_ACCESS_DENIED) {
			THROW(m3c::windows_exception(lastError), "MoveFileEx {} to {}", existingName, newName);
		}
		m3c::com_ptr<IShellItem> item;
		COM_HR(SHCreateItemFromParsingName(existingName.c_str(), nullptr, __uuidof(IShellItem), reinterpret_cast<void**>(&item)), "SHCreateItemFromParsingName {}", existingName);
		m3c::com_ptr<IFileOperation> fo;
		COM_HR(CoCreateInstance(__uuidof(FileOperation), nullptr, CLSCTX_ALL, __uuidof(IFileOperation), reinterpret_cast<void**>(&fo)), "CoCreateInstance");
		COM_HR(fo->SetOperationFlags(FOF_NO_UI), "SetOperationFlags");
		const Filename filename = newName.GetFilename();
		COM_HR(fo->RenameItem(item.get(), filename.c_str(), nullptr), "RenameItem {} to {}", existingName, filename);
		COM_HR(fo->PerformOperations(), "PerformOperations");
	}
}

void WritingBackupStrategy::Copy(const Path& source, const Path& target) const {
	BOOL cancel = FALSE;
	//COPYFILE2_EXTENDED_PARAMETERS params = {sizeof(params), COPY_FILE_FAIL_IF_EXISTS | COPY_FILE_NO_BUFFERING, &cancel, nullptr, nullptr};
	//COM_HR(CopyFile2(source.c_str(), target.c_str(), &params), "CopyFile2 {} to {}", source, target);
	if (!CopyFileEx(source.c_str(), target.c_str(), nullptr, nullptr, &cancel, COPY_FILE_FAIL_IF_EXISTS | COPY_FILE_NO_BUFFERING)) {
		THROW(m3c::windows_exception(GetLastError()), "CopyFileEx {} to {}", source, target);
	}
}

void WritingBackupStrategy::CreateHardLink(const Path& path, const Path& existing) const {
	if (!::CreateHardLinkW(path.c_str(), existing.c_str(), nullptr)) {
		THROW(m3c::windows_exception(GetLastError()), "CreateHardLink {} from {}", path, existing);
	}
}

void WritingBackupStrategy::Delete(const Path& path) const {
	path.ForceDelete();
}

}  // namespace systools
