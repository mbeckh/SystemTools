#pragma once

#include <systools/DirectoryScanner.h>

#include <windows.h>

#include <vector>

namespace systools {

class Path;
class FileComparer;

class __declspec(novtable) BackupStrategy {
public:
	BackupStrategy() noexcept = default;
	BackupStrategy(const BackupStrategy&) = delete;
	BackupStrategy(BackupStrategy&&) = delete;
	virtual ~BackupStrategy() noexcept = default;

public:
	BackupStrategy& operator=(const BackupStrategy&) = delete;
	BackupStrategy& operator=(BackupStrategy&&) = delete;

public:
	// Path Operations
	[[nodiscard]] virtual bool Exists(const Path& path) const = 0;
	[[nodiscard]] virtual bool IsDirectory(const Path& path) const = 0;

	// File Operations
	virtual bool Compare(const Path& src, const Path& target, FileComparer& fileComparer) const = 0;
	virtual void CreateDirectory(const Path& path, const Path& templatePath, const ScannedFile& securitySource) const = 0;
	virtual void CreateDirectoryRecursive(const Path& path) const = 0;
	virtual void SetAttributes(const Path& path, const ScannedFile& attributesSource) const = 0;
	virtual void SetSecurity(const Path& path, const ScannedFile& securitySource) const = 0;
	virtual void Rename(const Path& existingName, const Path& newName) const = 0;
	virtual void Copy(const Path& source, const Path& target) const = 0;
	virtual void CreateHardLink(const Path& path, const Path& existing) const = 0;
	virtual void Delete(const Path& path) const = 0;

	// Scan Operations
	virtual void Scan(const Path& path, DirectoryScanner& scanner, std::vector<ScannedFile>& directories, std::vector<ScannedFile>& files, DirectoryScanner::Flags flags, const ScannerFilter& filter) const = 0;
	virtual void WaitForScan(DirectoryScanner& scanner) const = 0;

public:
	static constexpr DWORD kCopyAttributeMask = FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_COMPRESSED | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED | FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM;
	static constexpr DWORD kUnsupportedAttributesMask = FILE_ATTRIBUTE_ENCRYPTED | FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_SPARSE_FILE;
};

class __declspec(novtable) BaseBackupStrategy : public BackupStrategy {
public:
	BaseBackupStrategy() noexcept = default;
	BaseBackupStrategy(const BaseBackupStrategy&) = delete;
	BaseBackupStrategy(BaseBackupStrategy&&) = delete;
	virtual ~BaseBackupStrategy() noexcept = default;

public:
	BaseBackupStrategy& operator=(const BaseBackupStrategy&) = delete;
	BaseBackupStrategy& operator=(BaseBackupStrategy&&) = delete;

public:
	// Path Operations
	[[nodiscard]] bool Exists(const Path& path) const final;
	[[nodiscard]] bool IsDirectory(const Path& path) const final;

	// File Operations
	bool Compare(const Path& src, const Path& target, FileComparer& fileComparer) const final;

	// Scan Operations
	void Scan(const Path& path, DirectoryScanner& scanner, std::vector<ScannedFile>& directories, std::vector<ScannedFile>& files, DirectoryScanner::Flags flags, const ScannerFilter& filter) const final;
	void WaitForScan(DirectoryScanner& scanner) const final;
};

class DryRunBackupStrategy final : public BaseBackupStrategy {
public:
	DryRunBackupStrategy() noexcept = default;
	DryRunBackupStrategy(const DryRunBackupStrategy&) = delete;
	DryRunBackupStrategy(DryRunBackupStrategy&&) = delete;
	virtual ~DryRunBackupStrategy() noexcept = default;

public:
	DryRunBackupStrategy& operator=(const DryRunBackupStrategy&) = delete;
	DryRunBackupStrategy& operator=(DryRunBackupStrategy&&) = delete;

public:
	// File Operations
	void CreateDirectory(const Path& path, const Path& templatePath, const ScannedFile& securitySource) const final;
	void CreateDirectoryRecursive(const Path& path) const final;
	void SetAttributes(const Path& target, const ScannedFile& attributesSource) const final;
	void SetSecurity(const Path& path, const ScannedFile& securitySource) const final;
	void Rename(const Path& existingName, const Path& newName) const final;
	void Copy(const Path& source, const Path& target) const final;
	void CreateHardLink(const Path& path, const Path& existing) const final;
	void Delete(const Path& path) const final;
};

class WritingBackupStrategy final : public BaseBackupStrategy {
public:
	WritingBackupStrategy() noexcept = default;
	WritingBackupStrategy(const WritingBackupStrategy&) = delete;
	WritingBackupStrategy(WritingBackupStrategy&&) = delete;
	virtual ~WritingBackupStrategy() noexcept = default;

public:
	WritingBackupStrategy& operator=(const WritingBackupStrategy&) = delete;
	WritingBackupStrategy& operator=(WritingBackupStrategy&&) = delete;

public:
	// File Operations
	void CreateDirectory(const Path& path, const Path& templatePath, const ScannedFile& securitySource) const final;
	void CreateDirectoryRecursive(const Path& path) const final;
	void SetAttributes(const Path& target, const ScannedFile& attributesSource) const final;
	void SetSecurity(const Path& path, const ScannedFile& securitySource) const final;
	void Rename(const Path& existingName, const Path& newName) const final;
	void Copy(const Path& source, const Path& target) const final;
	void CreateHardLink(const Path& path, const Path& existing) const final;
	void Delete(const Path& path) const final;
};

}  // namespace systools
